// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/map_tile.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/framework/texture_entry.hpp"
#include "opensim/scene/world.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <fstream>
#include <jpeglib.h>
#include <png.h>
#include <memory>
#include <limits>
#include <stdexcept>

namespace opensim::scene {

namespace {

// KnownColor and the HTML aliases use the managed default RGB values.
constexpr std::pair<std::string_view, std::uint32_t> named_map_colors[] = {
    {"activeborder", 0xb4b4b4},
    {"activecaption", 0x99b4d1},
    {"activecaptiontext", 0x000000},
    {"aliceblue", 0xf0f8ff},
    {"antiquewhite", 0xfaebd7},
    {"appworkspace", 0xababab},
    {"aqua", 0x00ffff},
    {"aquamarine", 0x7fffd4},
    {"azure", 0xf0ffff},
    {"background", 0x000000},
    {"beige", 0xf5f5dc},
    {"bisque", 0xffe4c4},
    {"black", 0x000000},
    {"blanchedalmond", 0xffebcd},
    {"blue", 0x0000ff},
    {"blueviolet", 0x8a2be2},
    {"brown", 0xa52a2a},
    {"burlywood", 0xdeb887},
    {"buttonface", 0xf0f0f0},
    {"buttonhighlight", 0xffffff},
    {"buttonshadow", 0xa0a0a0},
    {"buttontext", 0x000000},
    {"cadetblue", 0x5f9ea0},
    {"captiontext", 0x000000},
    {"chartreuse", 0x7fff00},
    {"chocolate", 0xd2691e},
    {"control", 0xf0f0f0},
    {"controldark", 0xa0a0a0},
    {"controldarkdark", 0x696969},
    {"controllight", 0xe3e3e3},
    {"controllightlight", 0xffffff},
    {"controltext", 0x000000},
    {"coral", 0xff7f50},
    {"cornflowerblue", 0x6495ed},
    {"cornsilk", 0xfff8dc},
    {"crimson", 0xdc143c},
    {"cyan", 0x00ffff},
    {"darkblue", 0x00008b},
    {"darkcyan", 0x008b8b},
    {"darkgoldenrod", 0xb8860b},
    {"darkgray", 0xa9a9a9},
    {"darkgreen", 0x006400},
    {"darkkhaki", 0xbdb76b},
    {"darkmagenta", 0x8b008b},
    {"darkolivegreen", 0x556b2f},
    {"darkorange", 0xff8c00},
    {"darkorchid", 0x9932cc},
    {"darkred", 0x8b0000},
    {"darksalmon", 0xe9967a},
    {"darkseagreen", 0x8fbc8f},
    {"darkslateblue", 0x483d8b},
    {"darkslategray", 0x2f4f4f},
    {"darkturquoise", 0x00ced1},
    {"darkviolet", 0x9400d3},
    {"deeppink", 0xff1493},
    {"deepskyblue", 0x00bfff},
    {"desktop", 0x000000},
    {"dimgray", 0x696969},
    {"dodgerblue", 0x1e90ff},
    {"firebrick", 0xb22222},
    {"floralwhite", 0xfffaf0},
    {"forestgreen", 0x228b22},
    {"fuchsia", 0xff00ff},
    {"gainsboro", 0xdcdcdc},
    {"ghostwhite", 0xf8f8ff},
    {"gold", 0xffd700},
    {"goldenrod", 0xdaa520},
    {"gradientactivecaption", 0xb9d1ea},
    {"gradientinactivecaption", 0xd7e4f2},
    {"gray", 0x808080},
    {"graytext", 0x6d6d6d},
    {"green", 0x008000},
    {"greenyellow", 0xadff2f},
    {"highlight", 0x0078d4},
    {"highlighttext", 0xffffff},
    {"honeydew", 0xf0fff0},
    {"hotpink", 0xff69b4},
    {"hottrack", 0x0066cc},
    {"inactiveborder", 0xf4f7fc},
    {"inactivecaption", 0xbfcddb},
    {"inactivecaptiontext", 0x000000},
    {"indianred", 0xcd5c5c},
    {"indigo", 0x4b0082},
    {"info", 0xffffe1},
    {"infobackground", 0xffffe1},
    {"infotext", 0x000000},
    {"ivory", 0xfffff0},
    {"khaki", 0xf0e68c},
    {"lavender", 0xe6e6fa},
    {"lavenderblush", 0xfff0f5},
    {"lawngreen", 0x7cfc00},
    {"lemonchiffon", 0xfffacd},
    {"lightblue", 0xadd8e6},
    {"lightcoral", 0xf08080},
    {"lightcyan", 0xe0ffff},
    {"lightgoldenrodyellow", 0xfafad2},
    {"lightgray", 0xd3d3d3},
    {"lightgreen", 0x90ee90},
    {"lightgrey", 0xd3d3d3},
    {"lightpink", 0xffb6c1},
    {"lightsalmon", 0xffa07a},
    {"lightseagreen", 0x20b2aa},
    {"lightskyblue", 0x87cefa},
    {"lightslategray", 0x778899},
    {"lightsteelblue", 0xb0c4de},
    {"lightyellow", 0xffffe0},
    {"lime", 0x00ff00},
    {"limegreen", 0x32cd32},
    {"linen", 0xfaf0e6},
    {"magenta", 0xff00ff},
    {"maroon", 0x800000},
    {"mediumaquamarine", 0x66cdaa},
    {"mediumblue", 0x0000cd},
    {"mediumorchid", 0xba55d3},
    {"mediumpurple", 0x9370db},
    {"mediumseagreen", 0x3cb371},
    {"mediumslateblue", 0x7b68ee},
    {"mediumspringgreen", 0x00fa9a},
    {"mediumturquoise", 0x48d1cc},
    {"mediumvioletred", 0xc71585},
    {"menu", 0xf0f0f0},
    {"menubar", 0xf0f0f0},
    {"menuhighlight", 0x0078d4},
    {"menutext", 0x000000},
    {"midnightblue", 0x191970},
    {"mintcream", 0xf5fffa},
    {"mistyrose", 0xffe4e1},
    {"moccasin", 0xffe4b5},
    {"navajowhite", 0xffdead},
    {"navy", 0x000080},
    {"oldlace", 0xfdf5e6},
    {"olive", 0x808000},
    {"olivedrab", 0x6b8e23},
    {"orange", 0xffa500},
    {"orangered", 0xff4500},
    {"orchid", 0xda70d6},
    {"palegoldenrod", 0xeee8aa},
    {"palegreen", 0x98fb98},
    {"paleturquoise", 0xafeeee},
    {"palevioletred", 0xdb7093},
    {"papayawhip", 0xffefd5},
    {"peachpuff", 0xffdab9},
    {"peru", 0xcd853f},
    {"pink", 0xffc0cb},
    {"plum", 0xdda0dd},
    {"powderblue", 0xb0e0e6},
    {"purple", 0x800080},
    {"rebeccapurple", 0x663399},
    {"red", 0xff0000},
    {"rosybrown", 0xbc8f8f},
    {"royalblue", 0x4169e1},
    {"saddlebrown", 0x8b4513},
    {"salmon", 0xfa8072},
    {"sandybrown", 0xf4a460},
    {"scrollbar", 0xc8c8c8},
    {"seagreen", 0x2e8b57},
    {"seashell", 0xfff5ee},
    {"sienna", 0xa0522d},
    {"silver", 0xc0c0c0},
    {"skyblue", 0x87ceeb},
    {"slateblue", 0x6a5acd},
    {"slategray", 0x708090},
    {"snow", 0xfffafa},
    {"springgreen", 0x00ff7f},
    {"steelblue", 0x4682b4},
    {"tan", 0xd2b48c},
    {"teal", 0x008080},
    {"thistle", 0xd8bfd8},
    {"threeddarkshadow", 0x696969},
    {"threedface", 0xf0f0f0},
    {"threedhighlight", 0xe3e3e3},
    {"threedlightshadow", 0xffffff},
    {"tomato", 0xff6347},
    {"transparent", 0xffffff},
    {"turquoise", 0x40e0d0},
    {"violet", 0xee82ee},
    {"wheat", 0xf5deb3},
    {"white", 0xffffff},
    {"whitesmoke", 0xf5f5f5},
    {"window", 0xffffff},
    {"windowframe", 0x646464},
    {"windowtext", 0x000000},
    {"yellow", 0xffff00},
    {"yellowgreen", 0x9acd32},
};

std::string_view trim_map_color(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n\f\v";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

std::optional<std::uint32_t> parse_map_color_number(std::string_view text) {
    if (text.starts_with('#') || text.starts_with("0x") || text.starts_with("&h")) {
        text.remove_prefix(text.front() == '#' ? 1 : 2);
        std::uint32_t number = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), number, 16);
        if (result.ec == std::errc{} && result.ptr == text.data() + text.size())
            return number;
        return {};
    }

    if (text.starts_with('+')) {
        text.remove_prefix(1);
        if (text.starts_with('-'))
            return {};
    }
    if (text.empty())
        return {};
    std::int32_t number = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    if (result.ec == std::errc{} && result.ptr == text.data() + text.size())
        return static_cast<std::uint32_t>(number);
    return {};
}

MapColor map_color_from_rgb(std::uint32_t color) {
    return {static_cast<std::uint8_t>(color >> 16), static_cast<std::uint8_t>(color >> 8),
            static_cast<std::uint8_t>(color)};
}

struct MapJpegReader {
    jpeg_decompress_struct decoder{};
    jpeg_error_mgr errors{};
    std::jmp_buf error_jump;
    MapBitmap bitmap;

    ~MapJpegReader() {
        jpeg_destroy_decompress(&decoder);
    }
};

MapBitmap read_map_jpeg(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > std::numeric_limits<unsigned long>::max())
        return {};
    auto reader = std::make_unique<MapJpegReader>();
    auto& decoder = reader->decoder;
    decoder.err = jpeg_std_error(&reader->errors);
    decoder.client_data = reader.get();
    reader->errors.error_exit = [](j_common_ptr context) {
        std::longjmp(static_cast<MapJpegReader*>(context->client_data)->error_jump, 1);
    };
    if (setjmp(reader->error_jump))
        return {};
    jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder, bytes.data(), static_cast<unsigned long>(bytes.size()));
    if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK)
        return {};
    decoder.out_color_space = JCS_RGB;
    jpeg_calc_output_dimensions(&decoder);
    const auto width = decoder.output_width;
    const auto height = decoder.output_height;
    const auto count = std::uint64_t(width) * height;
    if (count == 0 || count > std::numeric_limits<std::int32_t>::max() / 3)
        return {};
    auto& bitmap = reader->bitmap;
    bitmap.width = width;
    bitmap.height = height;
    bitmap.pixels.resize(static_cast<std::size_t>(count) * 3);
    if (!jpeg_start_decompress(&decoder) || decoder.output_components != 3)
        return {};
    while (decoder.output_scanline < height) {
        auto* scanline = bitmap.pixels.data() + std::size_t(decoder.output_scanline) * width * 3;
        if (jpeg_read_scanlines(&decoder, &scanline, 1) != 1)
            return {};
    }
    if (!jpeg_finish_decompress(&decoder))
        return {};
    return std::move(bitmap);
}

MapBitmap read_map_png(std::span<const std::uint8_t> bytes) {
    struct PngImage {
        png_image image{};

        PngImage() { image.version = PNG_IMAGE_VERSION; }
        ~PngImage() { png_image_free(&image); }
    } png;

    auto& image = png.image;
    if (!png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()))
        return {};
    const bool alpha = (image.format & PNG_FORMAT_FLAG_ALPHA) != 0;
    image.format = alpha ? PNG_FORMAT_RGBA : PNG_FORMAT_RGB;
    const std::size_t channels = alpha ? 4 : 3;
    const auto count = std::uint64_t(image.width) * image.height;
    if (count == 0 || count > std::numeric_limits<std::int32_t>::max() / channels)
        return {};
    MapBitmap bitmap{image.width, image.height,
                     std::vector<std::uint8_t>(static_cast<std::size_t>(count) * channels), channels};
    if (!png_image_finish_read(&image, nullptr, bitmap.pixels.data(), 0, nullptr))
        return {};
    return bitmap;
}

bool decode_bmp_rle(MapBitmap& bitmap, std::span<const std::uint8_t> stream,
                    std::span<const std::uint8_t> palette, bool four_bit) {
    const auto write = [&](std::size_t offset, unsigned index) {
        const auto color = index * 4;
        if (color >= palette.size())
            return false;
        bitmap.pixels[offset] = palette[color + 2];
        bitmap.pixels[offset + 1] = palette[color + 1];
        bitmap.pixels[offset + 2] = palette[color];
        return true;
    };
    for (std::size_t offset = 0; offset < bitmap.pixels.size(); offset += 3)
        write(offset, 0);

    const auto unpack = [&](std::uint8_t value, unsigned pixel) -> unsigned {
        if (!four_bit)
            return value;
        return (pixel % 2 == 0 ? value >> 4 : value) & 15u;
    };
    std::uint32_t x = 0, y = 0;
    while (stream.size() >= 2) {
        const unsigned count = stream[0];
        const unsigned command = stream[1];
        stream = stream.subspan(2);
        if (count == 0 && command == 1)
            return true;
        if (y >= bitmap.height)
            return false;
        if (count == 0 && command == 0) {
            x = 0;
            ++y;
            continue;
        }
        if (count == 0 && command == 2) {
            if (stream.size() < 2 || stream[0] > bitmap.width - x ||
                stream[1] >= bitmap.height - y)
                return false;
            x += stream[0];
            y += stream[1];
            stream = stream.subspan(2);
            continue;
        }

        const bool literal = count == 0;
        const unsigned pixels = literal ? command : count;
        const unsigned payload = four_bit ? (pixels + 1) / 2 : pixels;
        const unsigned padded = (payload + 1) & ~1u;
        if (pixels > bitmap.width - x || (literal && padded > stream.size()))
            return false;
        auto offset = (std::size_t(bitmap.height - y - 1) * bitmap.width + x) * 3;
        for (unsigned pixel = 0; pixel < pixels; ++pixel) {
            const auto value = literal ? stream[four_bit ? pixel / 2 : pixel] : std::uint8_t(command);
            if (!write(offset, unpack(value, pixel)))
                return false;
            offset += 3;
        }
        if (literal)
            stream = stream.subspan(padded);
        x += pixels;
    }
    return false;
}

MapBitmap read_map_bmp(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 54)
        return {};
    const auto u16 = [&](std::size_t offset) {
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8);
    };
    const auto u32 = [&](std::size_t offset) {
        return u16(offset) | (u16(offset + 2) << 16);
    };
    const auto offset = u32(10);
    const auto header_size = u32(14);
    const auto width = static_cast<std::int32_t>(u32(18));
    const auto signed_height = static_cast<std::int32_t>(u32(22));
    const auto bits = u16(28);
    const auto compression = u32(30);
    constexpr std::uint32_t rle8 = 1, rle4 = 2, bitfields = 3, alpha_bitfields = 6;
    const bool compressed = (compression == rle8 && bits == 8) || (compression == rle4 && bits == 4);
    const bool has_masks = compression == bitfields || compression == alpha_bitfields;
    const auto palette_offset = 14 + std::uint64_t(header_size);
    const bool indexed = bits == 1 || bits == 4 || bits == 8;
    if (header_size < 40 || offset < palette_offset || offset > bytes.size() ||
        width <= 0 || signed_height == 0 || u16(26) != 1 ||
        (!indexed && bits != 16 && bits != 24 && bits != 32) ||
        (compression != 0 && !compressed && (!has_masks || (bits != 16 && bits != 32))) ||
        (compressed && signed_height < 0))
        return {};

    std::array<std::uint32_t, 4> masks{};
    std::array<unsigned, 4> shifts{};
    std::array<std::uint32_t, 4> maxima{};
    std::size_t output_channels = 3;
    if (has_masks) {
        const bool alpha_mask_slot = compression == alpha_bitfields || header_size >= 56;
        if (offset < (alpha_mask_slot ? 70u : 66u) ||
            (header_size != 40 && header_size < (alpha_mask_slot ? 56u : 52u)))
            return {};
        for (std::size_t channel = 0; channel < (alpha_mask_slot ? 4u : 3u); ++channel)
            masks[channel] = u32(54 + channel * 4);
        if (masks[3] != 0)
            output_channels = 4;
        else if (compression == alpha_bitfields)
            return {};

        std::uint32_t used = 0;
        for (std::size_t channel = 0; channel < output_channels; ++channel) {
            const auto mask = masks[channel];
            if (mask == 0 || (mask & used) || static_cast<unsigned>(std::bit_width(mask)) > bits)
                return {};
            shifts[channel] = std::countr_zero(mask);
            maxima[channel] = mask >> shifts[channel];
            if ((maxima[channel] & (maxima[channel] + 1)) != 0)
                return {};
            used |= mask;
        }
    }

    const auto height = static_cast<std::uint32_t>(std::abs(std::int64_t(signed_height)));
    const auto count = std::uint64_t(width) * height;
    std::uint32_t palette_size = 0;
    if (indexed) {
        const auto maximum_colors = 1u << bits;
        palette_size = u32(46);
        if (palette_size == 0)
            palette_size = maximum_colors;
        if (palette_size > maximum_colors || palette_size * 4 > offset - palette_offset)
            return {};
    }
    const auto stride = ((std::uint64_t(width) * bits + 31) / 32) * 4;
    if (count > std::numeric_limits<std::int32_t>::max() / output_channels ||
        (!compressed && stride * height > bytes.size() - offset))
        return {};

    MapBitmap bitmap{static_cast<std::uint32_t>(width), height,
                     std::vector<std::uint8_t>(static_cast<std::size_t>(count) * output_channels),
                     output_channels};
    if (compressed) {
        auto stream = bytes.subspan(offset);
        const auto declared_size = u32(34);
        if (declared_size > stream.size())
            return {};
        if (declared_size != 0)
            stream = stream.first(declared_size);
        const auto palette = bytes.subspan(palette_offset, palette_size * 4);
        if (!decode_bmp_rle(bitmap, stream, palette, compression == rle4))
            return {};
        return bitmap;
    }
    for (std::uint32_t row = 0; row < height; ++row) {
        const auto source_row = signed_height < 0 ? row : height - row - 1;
        const auto* source = bytes.data() + offset + source_row * stride;
        auto* target = bitmap.pixels.data() + std::size_t(row) * width * output_channels;
        for (std::int32_t column = 0; column < width; ++column) {
            const auto bit_offset = std::size_t(column) * bits;
            const auto* sample = source + bit_offset / 8;
            if (indexed) {
                const auto shift = 8 - bits - (bit_offset % 8);
                const auto index = (*sample >> shift) & ((1u << bits) - 1);
                if (index >= palette_size)
                    return {};
                sample = bytes.data() + palette_offset + index * 4;
            }
            if (has_masks) {
                const auto sample_offset = offset + source_row * stride + bit_offset / 8;
                const auto value = bits == 16 ? u16(sample_offset) : u32(sample_offset);
                for (std::size_t channel = 0; channel < output_channels; ++channel) {
                    const auto component = (value & masks[channel]) >> shifts[channel];
                    target[channel] = static_cast<std::uint8_t>(
                        (std::uint64_t(component) * 255 + maxima[channel] / 2) / maxima[channel]);
                }
            } else if (bits == 16) {
                const unsigned value = sample[0] | (unsigned(sample[1]) << 8);
                for (unsigned channel = 0; channel < 3; ++channel) {
                    const auto component = (value >> (10 - channel * 5)) & 31;
                    target[channel] = static_cast<std::uint8_t>((component << 3) | (component >> 2));
                }
            } else {
                target[0] = sample[2];
                target[1] = sample[1];
                target[2] = sample[0];
            }
            target += output_channels;
        }
    }
    return bitmap;
}

} // namespace

std::optional<MapColor> parse_map_color(std::string_view text) {
    const bool shorthand = text.size() == 4 && text.front() == '#';
    text = trim_map_color(text);
    if (text.empty())
        return {};
    std::string lower(text);
    for (auto& c : lower)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));

    if (const auto named = std::ranges::find(named_map_colors, lower, [](const auto& color) { return color.first; });
        named != std::end(named_map_colors))
        return map_color_from_rgb(named->second);

    if (lower.find(',') != std::string::npos) {
        std::string_view components(lower);
        std::array<std::uint8_t, 4> values{};
        std::size_t count = 0;
        while (count < values.size()) {
            const auto comma = components.find(',');
            const auto value = parse_map_color_number(trim_map_color(components.substr(0, comma)));
            if (!value || *value > 255)
                return {};
            values[count++] = static_cast<std::uint8_t>(*value);
            if (comma == std::string_view::npos) {
                if (count == 3)
                    return MapColor{values[0], values[1], values[2]};
                if (count == 4)
                    return MapColor{values[1], values[2], values[3]};
                return {};
            }
            components.remove_prefix(comma + 1);
        }
        return {};
    }

    const auto value = parse_map_color_number(lower);
    if (!value)
        return {};
    if (!shorthand)
        return map_color_from_rgb(*value);
    return MapColor{static_cast<std::uint8_t>((*value >> 8) * 17),
                    static_cast<std::uint8_t>(((*value >> 4) & 15) * 17),
                    static_cast<std::uint8_t>((*value & 15) * 17)};
}

MapBitmap load_static_map_bitmap(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto length = stream.tellg();
    if (length < 2 || length > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("Could not read static map image");
    stream.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length)))
        throw std::runtime_error("Could not read static map image");

    MapBitmap bitmap;
    if (bytes[0] == 0xff && bytes[1] == 0xd8)
        bitmap = read_map_jpeg(bytes);
    else if (bytes[0] == 'B' && bytes[1] == 'M')
        bitmap = read_map_bmp(bytes);
    else if (bytes.size() >= 8 && png_sig_cmp(bytes.data(), 0, 8) == 0)
        bitmap = read_map_png(bytes);
    if (bitmap.pixels.empty())
        throw std::runtime_error("Invalid or unsupported static map image");
    return bitmap;
}

void write_map_bitmap_png(const MapBitmap& bitmap, const std::filesystem::path& path) {
    const auto count = std::uint64_t(bitmap.width) * bitmap.height;
    if ((bitmap.channels != 3 && bitmap.channels != 4) || count == 0 ||
        count > std::numeric_limits<std::size_t>::max() / bitmap.channels ||
        bitmap.pixels.size() != count * bitmap.channels)
        throw std::invalid_argument("Invalid map image dimensions");
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = bitmap.width;
    image.height = bitmap.height;
    image.format = bitmap.channels == 4 ? PNG_FORMAT_RGBA : PNG_FORMAT_RGB;
    png_alloc_size_t length = 0;
    if (!png_image_write_to_memory(&image, nullptr, &length, 0, bitmap.pixels.data(), 0, nullptr))
        throw std::runtime_error("Could not encode the local map PNG");
    std::vector<std::uint8_t> data(length);
    if (!png_image_write_to_memory(&image, data.data(), &length, 0, bitmap.pixels.data(), 0, nullptr))
        throw std::runtime_error("Could not encode the local map PNG");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(length)))
        throw std::runtime_error("Could not write the local map PNG");
}

std::vector<std::uint8_t> render_shaded_map_tile(
    std::span<const float> heights,
    std::uint32_t width,
    std::uint32_t height,
    float water_height,
    std::array<std::uint8_t, 3> water_color) {
    const auto count = std::uint64_t(width) * height;
    if (width == 0 || height == 0 || count != heights.size() ||
        count > std::numeric_limits<std::size_t>::max() / 3)
        throw std::invalid_argument("Map tile dimensions do not match the terrain");

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(count) * 3);
    const auto terrain = [&](std::uint32_t x, std::uint32_t y) {
        return heights[static_cast<std::size_t>(y) * width + x];
    };
    const auto pixel = [&](std::uint32_t x, std::uint32_t y) {
        const auto row = height - 1 - y;
        return pixels.data() + (static_cast<std::size_t>(row) * width + x) * 3;
    };
    const auto finite_height = [](float value) { return std::isfinite(value) ? value : 0.f; };

    for (std::uint32_t x = 0; x < width; ++x) {
        for (std::uint32_t y = 0; y < height; ++y) {
            const auto current = terrain(x, y);
            auto* rgb = pixel(x, y);
            if (!(current > water_height)) {
                std::ranges::copy(water_color, rgb);
                continue;
            }
            const auto level = static_cast<std::uint8_t>(std::clamp(finite_height(current), 0.f, 255.f));
            rgb[0] = level;
            rgb[1] = 100;
            rgb[2] = level;

            // The original shadow darkens the previously drawn southwest pixel.
            if (x < 2 || x + 1 >= width || y == 0)
                continue;
            const auto northeast = y + 1 < height ? finite_height(terrain(x + 1, y + 1)) : 0.f;
            const auto difference = finite_height(current) - northeast;
            if (difference >= -0.3f)
                continue;
            int shadow = static_cast<int>(std::min(-difference * 4.5f, 255.f)) + 1;
            const auto fraction = std::fmod(difference, 1.f);
            if (fraction != 0.f && std::isfinite(fraction))
                shadow += std::abs(static_cast<int>(fraction * 5.f) - 1);
            auto* southwest = pixel(x - 1, y - 1);
            for (std::size_t channel = 0; channel < 3; ++channel)
                southwest[channel] = static_cast<std::uint8_t>(std::max(0, southwest[channel] - shadow));
        }
    }
    return pixels;
}

namespace {

struct Hsv {
    float hue, saturation, value;
};

Hsv to_hsv(MapColor color) {
    const float r = color[0] / 255.f;
    const float g = color[1] / 255.f;
    const float b = color[2] / 255.f;
    const float maximum = std::max({r, g, b});
    const float difference = maximum - std::min({r, g, b});
    float hue = 0;
    if (difference != 0) {
        if (maximum == r)
            hue = (g - b) / difference * 60.f;
        else if (maximum == g)
            hue = (b - r) / difference * 60.f + 120.f;
        else
            hue = (r - g) / difference * 60.f + 240.f;
    }
    if (hue < 0)
        hue += 360.f;
    return {hue, maximum == 0 ? 0.f : difference / maximum, maximum};
}

MapColor to_rgb(Hsv hsv) {
    const auto channel = [](float value) {
        return static_cast<std::uint8_t>(std::clamp(value * 255.f, 0.f, 255.f));
    };
    const float sector = hsv.hue / 60.f;
    const auto index = static_cast<int>(sector);
    const float fraction = sector - index;
    const auto p = channel(hsv.value * (1.f - hsv.saturation));
    const auto q = channel(hsv.value * (1.f - hsv.saturation * fraction));
    const auto t = channel(hsv.value * (1.f - (1.f - fraction) * hsv.saturation));
    const auto v = channel(hsv.value);
    switch (index % 6) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
    }
}

Hsv interpolate_hsv(Hsv& from, const Hsv& to, float ratio) {
    if (ratio <= 0.f)
        return from;
    if (ratio >= 1.f)
        return to;
    // The legacy renderer keeps the adjusted hue for later pixels.
    if (from.hue - to.hue > 180.f)
        from.hue -= 360.f;
    else if (to.hue - from.hue > 180.f)
        from.hue += 360.f;
    return {from.hue * (1.f - ratio) + to.hue * ratio,
            from.saturation * (1.f - ratio) + to.saturation * ratio,
            from.value * (1.f - ratio) + to.value * ratio};
}

double terrain_noise(std::int64_t x, std::int64_t y) {
    // The managed hash wraps at 32 bits.
    auto n = static_cast<std::uint32_t>(x + y * 749);
    n = (n << 13) ^ n;
    return 1.0 - ((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu) / 1073741824.0;
}

double smooth_terrain_noise(std::int64_t x, std::int64_t y) {
    const double corners = terrain_noise(x - 1, y - 1) + terrain_noise(x + 1, y - 1) +
                           terrain_noise(x - 1, y + 1) + terrain_noise(x + 1, y + 1);
    const double sides = terrain_noise(x - 1, y) + terrain_noise(x + 1, y) +
                         terrain_noise(x, y - 1) + terrain_noise(x, y + 1);
    return corners / 16 + sides / 8 + terrain_noise(x, y) / 4;
}

double interpolated_terrain_noise(double x, double y) {
    const auto ix = static_cast<std::int64_t>(x);
    const auto iy = static_cast<std::int64_t>(y);
    const auto fx = x - ix;
    const auto fy = y - iy;
    const auto south = smooth_terrain_noise(ix, iy) * (1 - fx) + smooth_terrain_noise(ix + 1, iy) * fx;
    const auto north = smooth_terrain_noise(ix, iy + 1) * (1 - fx) + smooth_terrain_noise(ix + 1, iy + 1) * fx;
    return south * (1 - fy) + north * fy;
}

float smooth_step(float value) {
    return value * value * (3.f - 2.f * value);
}

float interpolate_elevation(const std::array<float, 4>& corners, float x, float y) {
    return corners[0] * (1.f - y) * (1.f - x) + corners[1] * (1.f - y) * x +
           corners[2] * y * (1.f - x) + corners[3] * y * x;
}

} // namespace

std::vector<std::uint8_t> render_textured_map_tile(
    std::span<const float> heights, std::uint32_t width, std::uint32_t height, float water_height,
    const std::array<float, 4>& low, const std::array<float, 4>& high, const TexturedMapPalette& palette) {
    const auto count = std::uint64_t(width) * height;
    if (width == 0 || height == 0 || count != heights.size() ||
        count > std::numeric_limits<std::size_t>::max() / 3)
        throw std::invalid_argument("Map tile dimensions do not match the terrain");

    const auto sample_height = [&](std::uint32_t x, std::uint32_t y) {
        const auto offset = std::size_t(y) * width + x;
        float result = heights[offset];
        if (x + 1 < width && y + 1 < height)
            result = static_cast<float>(result * .444 + (heights[offset + 1] + heights[offset + width]) * .222 +
                                        heights[offset + width + 1] * .112);
        return std::isfinite(result) ? result : 0.f;
    };
    std::array<Hsv, 4> colors;
    std::transform(palette.land.begin(), palette.land.end(), colors.begin(), to_hsv);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(count) * 3);
    for (std::uint32_t x = 0; x < width; ++x) {
        // The original computes these ratios with integer operands.
        const float column = x + 1 == width && width > 1 ? 1.f : 0.f;
        for (std::uint32_t y = 0; y < height; ++y) {
            const float elevation = sample_height(x, y);
            auto color = palette.water;
            if (elevation > water_height) {
                const float row = y + 1 == height && height > 1 ? 1.f : 0.f;
                auto lower = interpolate_elevation(low, column, row);
                auto upper = interpolate_elevation(high, column, row);
                if (upper < lower)
                    std::swap(lower, upper);
                const float small = static_cast<float>(smooth_terrain_noise(std::int64_t(x) + 33, std::int64_t(y) + 43));
                const float large = static_cast<float>(interpolated_terrain_noise(x / 8.0, y / 8.0)) * .5f + .5f;
                const float varied = elevation + small * 1.5f + 1.5f + smooth_step(smooth_step(large)) * 10.f;
                auto hsv = colors.front();
                if (varied >= upper && varied > lower) {
                    hsv = colors.back();
                } else if (varied > lower && varied < upper) {
                    const float ratio = (varied - lower) / (upper - lower);
                    const auto band = std::min(2, static_cast<int>(ratio * 3.f));
                    hsv = interpolate_hsv(colors[band], colors[band + 1], ratio * 3.f - band);
                }
                if (x + 1 < width && y + 1 < height) {
                    const float shade = (elevation - sample_height(x + 1, y + 1)) * .06f;
                    if (shade > .02f) {
                        hsv.saturation = std::max(0.f, hsv.saturation - shade * .18f);
                        hsv.value = std::min(1.f, hsv.value + shade * .18f);
                    } else if (shade < -.02f) {
                        hsv.saturation = std::max(0.f, hsv.saturation + shade);
                        hsv.value = std::max(0.f, hsv.value + shade);
                    }
                }
                color = to_rgb(hsv);
            }
            const auto offset = (std::size_t(height - y - 1) * width + x) * color.size();
            std::copy(color.begin(), color.end(), pixels.begin() + offset);
        }
    }
    return pixels;
}

namespace {

struct MapPoint {
    double x, y;
};

struct MapVolume {
    std::array<MapPoint, 8> vertices;
    float elevation;
    MapColor color;
};

bool finite_vector(framework::Vector3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void fill_map_face(std::span<std::uint8_t> pixels, std::uint32_t width, std::uint32_t height,
                   const MapVolume& volume, const std::array<unsigned, 4>& face) {
    double top = volume.vertices[face.front()].y;
    double bottom = top;
    for (auto corner : face) {
        top = std::min(top, volume.vertices[corner].y);
        bottom = std::max(bottom, volume.vertices[corner].y);
    }
    const auto first_row = static_cast<std::uint32_t>(std::clamp(top, 0.0, double(height)));
    const auto end_row = static_cast<std::uint32_t>(std::clamp(bottom, 0.0, double(height)));
    for (auto y = first_row; y < end_row; ++y) {
        const double scanline = y;
        std::array<double, 4> intersections;
        intersections.fill(std::numeric_limits<double>::infinity());
        std::size_t count = 0;
        for (std::size_t i = 0; i < face.size(); ++i) {
            const auto a = volume.vertices[face[i]];
            const auto b = volume.vertices[face[(i + 1) % face.size()]];
            if ((a.y <= scanline && scanline < b.y) || (b.y <= scanline && scanline < a.y))
                intersections[count++] = a.x + (scanline - a.y) / (b.y - a.y) * (b.x - a.x);
        }
        std::sort(intersections.begin(), intersections.end());
        for (std::size_t i = 1; i < count; i += 2) {
            const auto first = std::clamp(std::ceil(intersections[i - 1]), 0.0, double(width));
            const auto end = std::clamp(std::ceil(intersections[i]), 0.0, double(width));
            auto x = static_cast<std::uint32_t>(first);
            for (; x < static_cast<std::uint32_t>(end); ++x) {
                const auto offset = (std::size_t(y) * width + x) * volume.color.size();
                std::copy(volume.color.begin(), volume.color.end(), pixels.begin() + offset);
            }
        }
    }
}

} // namespace

void draw_map_object_volumes(
    std::span<std::uint8_t> pixels, std::span<const float> heights,
    std::uint32_t width, std::uint32_t height, std::span<const SceneObject> objects) {
    const auto pixel_count = std::uint64_t(width) * height;
    if (width == 0 || height == 0 || pixel_count != heights.size() ||
        pixel_count > std::numeric_limits<std::size_t>::max() / 3 || pixels.size() != pixel_count * 3)
        throw std::invalid_argument("Map tile dimensions do not match the terrain");

    std::map<std::uint32_t, const SceneObject*> roots;
    std::map<std::uint32_t, std::vector<const SceneObject*>> groups;
    for (const auto& object : objects) {
        if (object.is_attachment)
            continue;
        if (!object.parent_id)
            roots.emplace(object.local_id, &object);
        groups[object.parent_id ? object.parent_id : object.local_id].push_back(&object);
    }

    constexpr std::array<std::array<int, 3>, 8> signs{{
        {1, -1, 1}, {1, 1, 1}, {1, -1, -1}, {1, 1, -1},
        {-1, 1, 1}, {-1, 1, -1}, {-1, -1, 1}, {-1, -1, -1},
    }};
    std::vector<MapVolume> volumes;
    for (auto& [root_id, parts] : groups) {
        const auto root = roots.find(root_id);
        if (root == roots.end())
            continue;
        std::stable_sort(parts.begin(), parts.end(), [](const auto* a, const auto* b) {
            if (bool(a->parent_id) != bool(b->parent_id))
                return a->parent_id == 0;
            return a->link_order < b->link_order;
        });
        MapColor group_color{128, 128, 128};
        for (const auto* part : parts) {
            const auto size = part->scale;
            if (!finite_vector(size) || std::max({size.x, size.y, size.z}) <= 1.f)
                continue;
            constexpr std::uint8_t grass = 95, new_tree = 111, tree = 255;
            if (part->shape.pcode == grass || part->shape.pcode == new_tree || part->shape.pcode == tree)
                continue;

            try {
                const framework::TextureEntry textures{part->texture_entry};
                if (!textures.default_texture)
                    continue;
                const auto rgba = textures.default_texture->color();
                MapColor color;
                for (std::size_t channel = 0; channel < color.size(); ++channel)
                    color[channel] = static_cast<std::uint8_t>(255 - static_cast<int>(rgba[channel] * 255.f));
                if (color != MapColor{255, 255, 255})
                    group_color = color;
            } catch (const std::out_of_range&) {
                // Malformed texture entries leave the previous color in place.
            }

            auto position = part->position;
            auto rotation = part->rotation;
            if (part->parent_id) {
                const auto& parent = *root->second;
                const auto offset = framework::sdk_rotate_vector(position, parent.rotation);
                position = {parent.position.x + offset.x, parent.position.y + offset.y,
                            parent.position.z + offset.z};
                rotation = framework::sdk_multiply_quaternion(parent.rotation, rotation);
            }
            if (!finite_vector(position) || position.x < 0 || position.x >= width ||
                position.y < 0 || position.y >= height)
                continue;
            const auto terrain = heights[std::size_t(position.y) * width + std::size_t(position.x)];
            if (!(position.z < terrain + 256.f))
                continue;
            const framework::Vector3 half{size.x * .5f, size.y * .5f, size.z * .5f};
            const auto rotated_half = framework::sdk_rotate_vector(half, rotation);
            if (!finite_vector(rotated_half))
                continue;
            const auto x_extent = std::abs(rotated_half.x);
            const auto y_extent = std::abs(rotated_half.y);
            if (std::trunc(position.x - x_extent) < 0 || std::trunc(position.x + x_extent) >= width ||
                std::trunc(position.y - y_extent) < 0 || std::trunc(position.y + y_extent) >= height)
                continue;

            MapVolume volume{{}, position.z, group_color};
            bool valid = true;
            for (std::size_t index = 0; index < signs.size(); ++index) {
                const auto& sign = signs[index];
                const auto vertex = framework::sdk_rotate_vector(
                    {half.x * sign[0], half.y * sign[1], half.z * sign[2]}, rotation);
                if (!finite_vector(vertex)) {
                    valid = false;
                    break;
                }
                // The original projects the Y coordinate relative to map width.
                volume.vertices[index] = {std::trunc(double(position.x + vertex.x)),
                                          std::trunc(double(float(width - 1) - (position.y + vertex.y)))};
            }
            if (valid)
                volumes.push_back(volume);
        }
    }
    std::stable_sort(volumes.begin(), volumes.end(), [](const auto& a, const auto& b) {
        return a.elevation < b.elevation;
    });
    constexpr std::array<std::array<unsigned, 4>, 6> faces{{
        {0, 1, 3, 2}, {1, 4, 5, 3}, {4, 6, 7, 5},
        {6, 0, 2, 7}, {0, 6, 4, 1}, {3, 5, 7, 2},
    }};
    for (const auto& volume : volumes)
        for (const auto& face : faces)
            fill_map_face(pixels, width, height, volume, face);
}

std::vector<std::uint8_t> render_parcel_map_tile(
    std::span<const std::uint8_t> sale_cells, std::uint32_t width, std::uint32_t height) {
    constexpr std::uint32_t land_unit = 4;
    constexpr std::array<std::uint8_t, 4> sale_color{249, 223, 9, 192};
    const auto cells_x = width / land_unit;
    const auto cells_y = height / land_unit;
    const auto pixel_count = std::uint64_t(width) * height;
    if (sale_cells.size() != std::uint64_t(cells_x) * cells_y ||
        pixel_count > std::numeric_limits<std::size_t>::max() / sale_color.size() ||
        std::ranges::none_of(sale_cells, [](auto cell) { return cell != 0; }))
        return {};

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(pixel_count) * sale_color.size());
    for (std::uint32_t y = 0; y < cells_y; ++y) {
        // The managed overlay positions rows relative to the region width.
        const auto top = std::int64_t(width) - std::int64_t(y + 1) * land_unit;
        for (std::uint32_t x = 0; x < cells_x; ++x) {
            if (!sale_cells[std::size_t(y) * cells_x + x])
                continue;
            for (std::uint32_t dy = 0; dy < land_unit; ++dy) {
                const auto row = top + dy;
                if (row < 0 || row >= height)
                    continue;
                auto* pixel = rgba.data() + (std::size_t(row) * width + x * land_unit) * sale_color.size();
                for (std::uint32_t dx = 0; dx < land_unit; ++dx) {
                    std::copy(sale_color.begin(), sale_color.end(), pixel);
                    pixel += sale_color.size();
                }
            }
        }
    }
    return rgba;
}

std::vector<std::uint8_t> resize_map_tile(
    std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height,
    std::uint32_t target_width, std::uint32_t target_height, std::size_t channels) {
    const auto count = std::uint64_t(width) * height;
    const auto target_count = std::uint64_t(target_width) * target_height;
    if (channels != 3 && channels != 4)
        throw std::invalid_argument("Invalid map image channels");
    const auto limit = std::numeric_limits<std::size_t>::max() / channels;
    if (count == 0 || count > limit || pixels.size() != count * channels ||
        target_count == 0 || target_count > limit)
        throw std::invalid_argument("Invalid map image dimensions");

    const auto mirrored = [](std::int64_t coordinate, std::int64_t size) {
        const auto period = 2 * size;
        coordinate = (coordinate % period + period) % period;
        return coordinate < size ? coordinate : period - 1 - coordinate;
    };
    const auto cubic_weight = [](double distance) {
        const auto x = std::abs(distance);
        if (x < 1)
            return ((1.5 * x - 2.5) * x) * x + 1;
        if (x < 2)
            return ((-0.5 * x + 2.5) * x - 4) * x + 2;
        return 0.0;
    };

    std::vector<std::uint8_t> result(static_cast<std::size_t>(target_count) * channels);
    for (std::uint32_t y = 0; y < target_height; ++y) {
        const auto source_y = (y + 0.5) * height / target_height - 0.5;
        const auto base_y = static_cast<std::int64_t>(std::floor(source_y));
        for (std::uint32_t x = 0; x < target_width; ++x) {
            const auto source_x = (x + 0.5) * width / target_width - 0.5;
            const auto base_x = static_cast<std::int64_t>(std::floor(source_x));
            std::array<double, 4> color{};
            for (auto sy = base_y - 1; sy <= base_y + 2; ++sy) {
                const auto row_weight = cubic_weight(source_y - sy);
                for (auto sx = base_x - 1; sx <= base_x + 2; ++sx) {
                    const auto weight = row_weight * cubic_weight(source_x - sx);
                    const auto offset = (mirrored(sy, height) * width + mirrored(sx, width)) * channels;
                    for (std::size_t channel = 0; channel < channels; ++channel)
                        color[channel] += pixels[offset + channel] * weight;
                }
            }
            const auto offset = (static_cast<std::size_t>(y) * target_width + x) * channels;
            for (std::size_t channel = 0; channel < channels; ++channel)
                result[offset + channel] = static_cast<std::uint8_t>(
                    std::clamp(std::lround(color[channel]), 0L, 255L));
        }
    }
    return result;
}

void composite_map_bitmap(MapBitmap& canvas, MapBitmap tile, std::int64_t x, std::int64_t y,
                          std::uint32_t width, std::uint32_t height) {
    const auto left = std::max<std::int64_t>(0, x);
    const auto top = std::max<std::int64_t>(0, y);
    const auto right = std::min<std::int64_t>(canvas.width, x + width);
    const auto bottom = std::min<std::int64_t>(canvas.height, y + height);
    if (left >= right || top >= bottom || tile.pixels.empty())
        return;
    if (tile.width != width || tile.height != height)
        tile.pixels = resize_map_tile(tile.pixels, tile.width, tile.height, width, height, tile.channels);

    for (auto row = top; row < bottom; ++row) {
        for (auto column = left; column < right; ++column) {
            const auto source = (std::size_t(row - y) * width + std::size_t(column - x)) * tile.channels;
            const auto target = (std::size_t(row) * canvas.width + std::size_t(column)) * 3;
            const unsigned alpha = tile.channels == 4 ? tile.pixels[source + 3] : 255;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                canvas.pixels[target + channel] = static_cast<std::uint8_t>(
                    (tile.pixels[source + channel] * alpha +
                     canvas.pixels[target + channel] * (255 - alpha) + 127) / 255);
            }
        }
    }
}

} // namespace opensim::scene
