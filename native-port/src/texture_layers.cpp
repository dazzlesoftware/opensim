// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/texture_layers.hpp"
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/framework/asset_cache.hpp"
#include <array>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <jpeglib.h>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace opensim::scene {
namespace {
// Layout exported by the bundled openjpeg-dotnet libraries.
struct PacketIndex {
    std::int32_t start, header_end, end;
#ifdef _WIN32
    std::array<std::byte, 8> unused;
#else
    // The double distortion field has four bytes of alignment padding.
    std::array<std::byte, 12> unused;
#endif
};
struct ImageIndex {
    std::uint8_t* encoded = nullptr;
    std::int32_t length = 0, padding = 0;
    void* decoded = nullptr;
    std::int32_t width = 0, height = 0, layers = 0, resolutions = 0, components = 0, packets = 0;
    const PacketIndex* index = nullptr;
};
#ifdef _WIN32
static_assert(sizeof(PacketIndex) == 20);
#else
static_assert(sizeof(PacketIndex) == 24);
#endif
static_assert(sizeof(void*) == 8 && sizeof(ImageIndex) == 56 && offsetof(ImageIndex, index) == 48);

class Library {
#ifdef _WIN32
    HMODULE handle_;
#else
    void* handle_;
#endif
public:
    explicit Library(const std::filesystem::path& path)
#ifdef _WIN32
        : handle_(LoadLibraryExW(path.c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
#else
#if defined(__linux__)
        // The bundled x86-64 codec has an unresolved JPIP index writer.
        // Its DotNet entry points encode raw codestreams and never use it.
        : handle_(dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL))
#else
        : handle_(dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL))
#endif
#endif
    {
        if (!handle_)
            throw std::runtime_error("Cannot load the JPEG 2000 layer decoder");
    }
    ~Library() {
#ifdef _WIN32
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
    }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    using Operation = bool (*)(ImageIndex*);
    template <typename Function = Operation>
    Function operation(const char* name) const {
#ifdef _WIN32
        const auto address = GetProcAddress(handle_, name);
#else
        const auto address = dlsym(handle_, name);
#endif
        if (!address)
            throw std::runtime_error(std::string("JPEG 2000 decoder is missing ") + name);
        return reinterpret_cast<Function>(address);
    }
};

std::mutex decoder_mutex;

struct JpegEncoding {
    jpeg_compress_struct compressor{};
    jpeg_error_mgr errors{};
    std::jmp_buf error_jump;
    unsigned char* output = nullptr;
    unsigned long length = 0;

    ~JpegEncoding() {
        jpeg_destroy_compress(&compressor);
        std::free(output);
    }
};

std::string encode_jpeg(const ImageIndex& image, bool interleaved, int quality) {
    if (!image.decoded || image.width <= 0 || image.height <= 0 ||
        image.width > JPEG_MAX_DIMENSION || image.height > JPEG_MAX_DIMENSION ||
        image.components < 1 || image.components > 5)
        return {};
    const auto encoding = std::make_unique<JpegEncoding>();
    const int components = image.components < 3 ? 1 : 3;
    std::vector<JSAMPLE> row(static_cast<std::size_t>(image.width) * components);
    auto& compressor = encoding->compressor;
    compressor.err = jpeg_std_error(&encoding->errors);
    compressor.client_data = encoding.get();
    encoding->errors.error_exit = [](j_common_ptr context) {
        std::longjmp(static_cast<JpegEncoding*>(context->client_data)->error_jump, 1);
    };
    if (setjmp(encoding->error_jump))
        return {};
    jpeg_create_compress(&compressor);
    jpeg_mem_dest(&compressor, &encoding->output, &encoding->length);
    compressor.image_width = image.width;
    compressor.image_height = image.height;
    compressor.input_components = components;
    compressor.in_color_space = components == 1 ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, quality, TRUE);
    jpeg_start_compress(&compressor, TRUE);
    const auto* decoded = static_cast<const std::uint8_t*>(image.decoded);
    const auto plane_size = static_cast<std::size_t>(image.width) * image.height;
    while (compressor.next_scanline < compressor.image_height) {
        const auto offset = static_cast<std::size_t>(compressor.next_scanline) * image.width;
        for (int x = 0; x < image.width; ++x)
            for (int channel = 0; channel < components; ++channel)
                row[x * components + channel] = interleaved
                    ? decoded[(offset + x) * image.components + channel]
                    : decoded[channel * plane_size + offset + x];
        auto* scanline = row.data();
        jpeg_write_scanlines(&compressor, &scanline, 1);
    }
    jpeg_finish_compress(&compressor);
    return {reinterpret_cast<const char*>(encoding->output), encoding->length};
}

std::string encode_map_jpeg(const ImageIndex& image) {
    return encode_jpeg(image, false, 95);
}

bool has_codestream_signature(std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint8_t, 12> jp2{0, 0, 0, 12, 'j', 'P', ' ', ' ', 13, 10, 135, 10};
    return (bytes.size() >= 4 && bytes[0] == 0xff && bytes[1] == 0x4f &&
            bytes[2] == 0xff && bytes[3] == 0x51) ||
           (bytes.size() >= jp2.size() && std::memcmp(bytes.data(), jp2.data(), jp2.size()) == 0);
}

std::vector<std::int32_t> read_layer_record(const framework::AssetBase& asset, std::size_t texture_size) {
    if (!asset.data || asset.data->empty())
        return {};
    std::string_view text(reinterpret_cast<const char*>(asset.data->data()), asset.data->size());
    std::vector<std::int32_t> ends;
    const auto number = [](std::string_view value, std::int32_t& result) {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
    };
    while (!text.empty()) {
        const auto newline = text.find('\n');
        const auto line = text.substr(0, newline);
        text = newline == text.npos ? std::string_view{} : text.substr(newline + 1);
        if (line.empty())
            continue;
        const auto first = line.find('|');
        const auto second = first == line.npos ? line.npos : line.find('|', first + 1);
        std::int32_t start, end;
        if (second == line.npos || line.find('|', second + 1) != line.npos ||
            !number(line.substr(0, first), start) ||
            !number(line.substr(first + 1, second - first - 1), end) ||
            start < 0 || end < start - 1 || end > static_cast<std::int64_t>(texture_size) ||
            (!ends.empty() && end < ends.back()))
            return {};
        ends.push_back(end);
    }
    return ends;
}
}

namespace {

template <typename Result>
auto open_map_decoder(const std::filesystem::path& path, Result (*convert)(const ImageIndex&)) {
    auto library = std::make_shared<Library>(std::filesystem::canonical(path));
    const auto allocate = library->operation("DotNetAllocEncoded");
    const auto decode = library->operation("DotNetDecode");
    const auto release = library->operation("DotNetFree");
    return [library, allocate, decode, release, convert](std::span<const std::uint8_t> bytes) -> Result {
        if (bytes.size() > std::numeric_limits<std::int32_t>::max() || !has_codestream_signature(bytes))
            return {};
        std::lock_guard lock(decoder_mutex);
        ImageIndex image;
        image.length = static_cast<std::int32_t>(bytes.size());
        struct Release {
            ImageIndex& image;
            Library::Operation release;
            ~Release() { release(&image); }
        } cleanup{image, release};
        if (!allocate(&image) || !image.encoded)
            return {};
        std::memcpy(image.encoded, bytes.data(), bytes.size());
        return decode(&image) ? convert(image) : Result{};
    };
}

MapBitmap copy_map_bitmap(const ImageIndex& image) {
    if (!image.decoded || image.width <= 0 || image.height <= 0 ||
        image.components < 1 || image.components > 5)
        return {};
    const auto pixels = std::uint64_t(image.width) * image.height;
    const std::size_t channels = image.components == 2 || image.components >= 4 ? 4 : 3;
    if (pixels > std::uint64_t(std::numeric_limits<std::int32_t>::max()) / channels)
        return {};

    MapBitmap bitmap{static_cast<std::uint32_t>(image.width), static_cast<std::uint32_t>(image.height),
                     std::vector<std::uint8_t>(static_cast<std::size_t>(pixels) * channels), channels};
    const auto* decoded = static_cast<const std::uint8_t*>(image.decoded);
    const std::size_t alpha = image.components == 2 ? 1 : image.components - 1;
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto plane = image.components < 3 ? 0 : channel;
            bitmap.pixels[pixel * channels + channel] = decoded[plane * pixels + pixel];
        }
        if (channels == 4)
            bitmap.pixels[pixel * channels + 3] = decoded[alpha * pixels + pixel];
    }
    return bitmap;
}

std::optional<std::array<std::uint8_t, 3>> average_texture_color(const ImageIndex& image) {
    if (!image.decoded || image.width <= 0 || image.height <= 0 ||
        image.components < 1 || image.components > 5)
        return {};
    const auto count = std::uint64_t(image.width) * image.height;
    if (count > std::numeric_limits<std::int32_t>::max() / image.components)
        return {};
    const auto* decoded = static_cast<const std::uint8_t*>(image.decoded);
    std::array<std::uint8_t, 3> color;
    for (std::size_t channel = 0; channel < color.size(); ++channel) {
        const auto plane = image.components < 3 ? 0 : channel;
        const auto* samples = decoded + plane * count;
        std::uint64_t sum = 0;
        for (std::size_t pixel = 0; pixel < count; ++pixel)
            sum += samples[pixel];
        color[channel] = static_cast<std::uint8_t>(sum / count);
    }
    return color;
}

} // namespace

std::string encode_map_bitmap_jpeg(const MapBitmap& bitmap) {
    if (bitmap.channels != 3 || bitmap.width > JPEG_MAX_DIMENSION || bitmap.height > JPEG_MAX_DIMENSION ||
        bitmap.pixels.size() != std::uint64_t(bitmap.width) * bitmap.height * bitmap.channels)
        return {};
    ImageIndex image;
    image.width = static_cast<std::int32_t>(bitmap.width);
    image.height = static_cast<std::int32_t>(bitmap.height);
    image.components = 3;
    image.decoded = const_cast<std::uint8_t*>(bitmap.pixels.data());
    return encode_jpeg(image, true, 75);
}

MapJpegEncoder open_map_jpeg_encoder(const std::filesystem::path& path) {
    return open_map_decoder(path, encode_map_jpeg);
}

MapTextureAverage open_map_texture_average(const std::filesystem::path& path) {
    return open_map_decoder(path, average_texture_color);
}

MapBitmapDecoder open_map_bitmap_decoder(const std::filesystem::path& path) {
    return open_map_decoder(path, copy_map_bitmap);
}

MapJpeg2000Encoder open_map_jpeg2000_encoder(const std::filesystem::path& path) {
    auto library = std::make_shared<Library>(std::filesystem::canonical(path));
    const auto allocate = library->operation("DotNetAllocDecoded");
    const auto encode = library->operation<bool (*)(ImageIndex*, bool)>("DotNetEncode");
    const auto release = library->operation("DotNetFree");
    return [library, allocate, encode, release](std::span<const std::uint8_t> interleaved,
                                               std::uint32_t width, std::uint32_t height,
                                               std::size_t channels, bool lossless) -> std::vector<std::uint8_t> {
        const auto pixels = std::uint64_t(width) * height;
        if ((channels != 3 && channels != 4) || width == 0 || height == 0 ||
            pixels > std::numeric_limits<std::int32_t>::max() / channels ||
            interleaved.size() != pixels * channels)
            return {};
        std::lock_guard lock(decoder_mutex);
        ImageIndex image;
        image.width = static_cast<std::int32_t>(width);
        image.height = static_cast<std::int32_t>(height);
        image.components = static_cast<std::int32_t>(channels);
        struct Release {
            ImageIndex& image;
            Library::Operation release;
            ~Release() { release(&image); }
        } cleanup{image, release};
        if (!allocate(&image) || !image.decoded)
            return {};
        auto* decoded = static_cast<std::uint8_t*>(image.decoded);
        for (std::size_t channel = 0; channel < channels; ++channel)
            for (std::size_t pixel = 0; pixel < pixels; ++pixel)
                decoded[channel * pixels + pixel] = interleaved[pixel * channels + channel];
        if (!encode(&image, lossless) || !image.encoded || image.length <= 0)
            return {};
        return {image.encoded, image.encoded + image.length};
    };
}

TextureLayerDecoder open_texture_layer_decoder(const std::filesystem::path& path,
                                               bool use_next_layer_boundary) {
    auto library = std::make_shared<Library>(std::filesystem::canonical(path));
    const auto allocate = library->operation("DotNetAllocEncoded");
    const auto decode = library->operation("DotNetDecodeWithInfo");
    const auto release = library->operation("DotNetFree");
    return [library, allocate, decode, release, use_next_layer_boundary](auto bytes)
        -> std::vector<TextureLayer> {
        if (bytes.size() > std::numeric_limits<std::int32_t>::max() || !has_codestream_signature(bytes))
            return {};

        std::lock_guard lock(decoder_mutex);
        ImageIndex image;
        image.length = static_cast<std::int32_t>(bytes.size());
        struct Release {
            ImageIndex& image;
            Library::Operation release;
            ~Release() { release(&image); }
        } cleanup{image, release};
        if (!allocate(&image) || !image.encoded)
            return {};
        std::memcpy(image.encoded, bytes.data(), bytes.size());
        if (!decode(&image) || !image.index || image.layers <= 0 || image.resolutions <= 0 ||
            image.components <= 0 || image.packets <= 0)
            return {};
        const auto packets_per_layer = image.packets / image.layers;
        if (image.packets % image.layers != 0 ||
            static_cast<std::int64_t>(image.resolutions) * image.components != packets_per_layer)
            return {};

        std::vector<TextureLayer> layers;
        layers.reserve(image.layers);
        for (int layer = 0; layer < image.layers; ++layer) {
            const auto first = layer * packets_per_layer;
            const auto last = first + packets_per_layer - 1;
            const auto start = image.index[first].start;
            const std::int64_t end = use_next_layer_boundary
                ? (last + 1 == image.packets ? image.length :
                   static_cast<std::int64_t>(image.index[last + 1].start) - 1)
                : image.index[last].end;
            if (start < 0 || start >= end || end > image.length ||
                (!layers.empty() && start <= layers.back().end))
                return {};
            layers.push_back({use_next_layer_boundary && layer == 0 ? 0 : start,
                              static_cast<std::int32_t>(end)});
        }
        return layers;
    };
}

CachedTextureLayerDecoder cache_texture_layer_decoder(
    TextureLayerDecoder decoder, std::function<std::shared_ptr<framework::IAssetCache>()> asset_cache,
    framework::Uuid creator) {
    struct Cache {
        std::mutex mutex;
        framework::ExpiringCacheOS<framework::Uuid, std::vector<std::int32_t>, framework::UuidHash> entries;
    };
    auto cache = std::make_shared<Cache>();
    return [cache, decoder = std::move(decoder), asset_cache = std::move(asset_cache), creator](
               const framework::Uuid& id, auto bytes) {
        std::lock_guard lock(cache->mutex);
        std::vector<std::int32_t> ends;
        if (cache->entries.try_get_value(id, ends))
            return ends;
        const auto storage = asset_cache ? asset_cache() : nullptr;
        const auto key = "j2k" + id.str();
        if (storage) {
            std::shared_ptr<framework::AssetBase> record;
            storage->get(key, record);
            if (record) {
                ends = read_layer_record(*record, bytes.size());
                if (!ends.empty()) {
                    cache->entries.add_or_update(id, ends, 60);
                    return ends;
                }
                storage->expire(key);
            }
        }
        auto layers = decoder(bytes);
        if (layers.empty() && storage && bytes.size() <= std::numeric_limits<std::int32_t>::max()) {
            constexpr std::array fractions{0.f, .02f, .05f, .20f, .50f};
            const auto length = static_cast<std::int32_t>(bytes.size());
            for (std::size_t i = 0; i < fractions.size(); ++i) {
                const auto start = static_cast<std::int32_t>(static_cast<float>(length) * fractions[i]);
                const auto end = i + 1 == fractions.size() ? length :
                    static_cast<std::int32_t>(static_cast<float>(length) * fractions[i + 1]) - 1;
                layers.push_back({start, end});
            }
        }
        std::string serialized;
        for (const auto& [start, end] : layers) {
            ends.push_back(end);
            if (storage) {
                if (!serialized.empty())
                    serialized += '\n';
                serialized += std::to_string(start) + '|' + std::to_string(end) + '|' +
                              std::to_string(end - start);
            }
        }
        if (storage && !serialized.empty()) {
            auto record = std::make_shared<framework::AssetBase>(
                key, key, static_cast<std::int8_t>(framework::AssetType::Notecard), creator.str());
            record->local(true);
            record->temporary(true);
            record->data = std::make_shared<std::vector<std::uint8_t>>(serialized.begin(), serialized.end());
            storage->cache(record);
        }
        cache->entries.add_or_update(id, ends, 60);
        return ends;
    };
}
}
