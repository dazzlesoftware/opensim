// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/terrain.hpp"
#include "opensim/network/gzip.hpp"
#include "opensim/network/simulation_body.hpp"
#include "dotnet_regex_sort.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <ios>
#include <mutex>
#include <stdexcept>
#define Z_PREFIX
#include "../third_party/zlib/zlib.h"
namespace opensim::scene {
namespace {
int source_int(float value) {
    if (!std::isfinite(value) || value >= 2147483648.f || value < -2147483648.f)
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<int>(value);
}
char terrain_gray(float height) {
    const auto index = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(source_int(height)) * 2u);
    return static_cast<char>(std::clamp(index, 0, 255));
}
}
std::vector<std::uint8_t> Terrain::llraw() const {
    struct Entry {
        std::uint16_t index;
        int product;
    };
    static const auto table = [] {
        std::vector<Entry> entries;
        entries.reserve(65536);
        for (int index = 0; index < 65536; ++index)
            entries.push_back({static_cast<std::uint16_t>(index), (index & 255) * (index >> 8)});
        framework::regex_detail::sort(entries, [](Entry a, Entry b) { return a.product - b.product; });
        return entries;
    }();
    const auto heights = serialized_floats();
    std::vector<std::uint8_t> bytes;
    bytes.reserve(heights.size() * 13);
    for (auto y = height_; y > 0; --y) {
        for (std::uint32_t x = 0; x < width_; ++x) {
            const float height = std::max(0.f, heights[std::size_t(y - 1) * width_ + x]) * 128.f;
            int low = 0, high = static_cast<int>(table.size()) - 1;
            while (low <= high) {
                const int middle = low + (high - low) / 2;
                if (table[middle].product == height) {
                    high = middle;
                    break;
                }
                if (table[middle].product < height)
                    low = middle + 1;
                else
                    high = middle - 1;
            }
            const auto index = table[high].index;
            const auto red = static_cast<std::uint8_t>(index), green = static_cast<std::uint8_t>(index >> 8);
            bytes.insert(bytes.end(), {red, green, 20, 0, 0, 0, 0, 255, 255, 255, 255, red, green});
        }
    }
    return bytes;
}
std::string Terrain::terragen() const {
    const auto samples = serialized_floats();
    if (samples.empty() || width_ > 32767 || height_ > 32767 ||
        std::any_of(samples.begin(), samples.end(), [](float height) { return !std::isfinite(height); }))
        throw std::invalid_argument("Invalid Terragen heightfield");
    const auto [low, high] = std::minmax_element(samples.begin(), samples.end());
    const float base = std::floor(.5f * (*high + *low));
    const float scale = std::max(.01f, std::ceil(*high - *low));
    if (base < -32768.f || base > 32767.f || scale > 32767.f)
        throw std::invalid_argument("Terragen heightfield exceeds its signed 16-bit range");
    std::string output = "TERRAGENTERRAIN ";
    const auto write = [&](std::uint32_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i)
            output.push_back(static_cast<char>(value >> (i * 8)));
    };
    for (const auto& [tag, value] :
         {std::pair{"SIZE", width_}, std::pair{"XPTS", width_}, std::pair{"YPTS", height_}}) {
        output += tag;
        write(value, 2);
        write(0, 2);
    }
    output += "SCAL";
    for (int axis = 0; axis < 3; ++axis)
        write(0x3f800000, 4);
    output += "ALTW";
    write(static_cast<std::uint32_t>(scale), 2);
    write(static_cast<std::uint32_t>(static_cast<int>(base)), 2);
    const double factor = 65536.0 / scale;
    for (float height : samples) {
        const float value = std::clamp(static_cast<float>((height - base) * factor), -32768.f, 32767.f);
        int rounded = static_cast<int>(std::floor(value));
        const float fraction = value - rounded;
        if (fraction > .5f || (fraction == .5f && rounded % 2 != 0))
            ++rounded;
        write(static_cast<std::uint32_t>(rounded), 2);
    }
    output += "EOF ";
    return output;
}
void Terrain::merge_terragen(std::span<const std::uint8_t> data, bool exact_size) {
    constexpr std::string_view signature = "TERRAGENTERRAIN ";
    if (data.size() < signature.size() || !std::equal(signature.begin(), signature.end(), data.begin())) {
        if (exact_size && (width_ != 256 || height_ != 256))
            throw std::invalid_argument("Terrain file dimensions do not match the region");
        for (std::uint32_t y = 0; y < std::min(height_, 256u); ++y)
            for (std::uint32_t x = 0; x < std::min(width_, 256u); ++x)
                set(x, y, 0.f);
        return;
    }
    data = data.subspan(signature.size());
    const auto read16 = [&] {
        if (data.size() < 2)
            throw std::ios_base::failure("Truncated Terragen terrain");
        const auto word = static_cast<std::uint16_t>(data[0] | (data[1] << 8));
        data = data.subspan(2);
        return std::bit_cast<std::int16_t>(word);
    };
    int width = 256, height = 256;
    while (data.size() >= 4) {
        const std::string_view chunk(reinterpret_cast<const char*>(data.data()), 4);
        data = data.subspan(4);
        if (chunk == "ALTW") {
            const float scale = read16() / 65536.f;
            const float base = read16();
            if (width < 0 || height < 0)
                throw std::runtime_error("Invalid Terragen heightfield dimensions");
            if (exact_size && (std::uint32_t(width) != width_ || std::uint32_t(height) != height_))
                throw std::invalid_argument("Terrain file dimensions do not match the region");
            if (std::size_t(width) * height > data.size() / 2)
                throw std::ios_base::failure("Truncated Terragen heightfield");
            const auto taint_words = (std::size_t(width / 16) * (height / 16) + 31) / 32;
            const auto samples = data;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    const float sample = base + read16() * scale;
                    if (sample != 0.f && std::size_t(x / 16 + (y / 16) * (width / 16)) / 32 >= taint_words)
                        throw std::runtime_error("Terragen heightfield exceeds its patch grid");
                }
            data = samples;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x) {
                    const float sample = base + read16() * scale;
                    if (std::uint32_t(x) < width_ && std::uint32_t(y) < height_)
                        set(x, y, sample);
                }
            return;
        }
        const auto value = read16();
        read16();
        if (chunk == "SIZE")
            width = height = value + 1;
        else if (chunk == "XPTS")
            width = value;
        else if (chunk == "YPTS")
            height = value;
    }
    throw std::ios_base::failure("Terragen terrain has no altitude chunk");
}
std::string Terrain::bitmap() const {
    const auto heights = serialized_floats();
    if (width_ == 0 || height_ == 0 || heights.size() > (std::numeric_limits<std::uint32_t>::max() - 54) / 4)
        throw std::invalid_argument("Invalid bitmap terrain dimensions");
    std::string output = "BM";
    output.reserve(54 + heights.size() * 4);
    const auto write = [&](std::uint32_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i)
            output.push_back(static_cast<char>(value >> (8 * i)));
    };
    write(static_cast<std::uint32_t>(54 + heights.size() * 4), 4);
    write(0, 4);
    write(54, 4);
    write(40, 4);
    write(width_, 4);
    write(height_, 4);
    write(1, 2);
    write(32, 2);
    write(0, 4);
    write(0, 4);
    write(3780, 4);
    write(3780, 4);
    write(0, 4);
    write(0, 4);
    for (float height : heights) {
        output.append(3, terrain_gray(height));
        output.push_back(static_cast<char>(255));
    }
    return output;
}
void Terrain::merge_bitmap(std::span<const std::uint8_t> data, bool exact_size) {
    const auto word = [&](std::size_t offset) {
        return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8);
    };
    const auto dword = [&](std::size_t offset) { return word(offset) | (word(offset + 2) << 16); };
    if (data.size() < 54 || data[0] != 'B' || data[1] != 'M')
        throw std::runtime_error("Invalid bitmap terrain");
    const auto offset = dword(10), header = dword(14), bits = word(28);
    const auto compression = dword(30);
    const bool rle = compression == 1 || compression == 2;
    const auto width = std::bit_cast<std::int32_t>(dword(18)),
               height = std::bit_cast<std::int32_t>(dword(22));
    if (header < 40 || std::uint64_t(header) + 14 > offset || offset > data.size() || width <= 0 ||
        height == 0 || word(26) != 1 ||
        (rle ? (height < 0 || bits != (compression == 1 ? 8u : 4u))
             : (compression != 0 && (compression != 3 || (bits != 16 && bits != 32)))) ||
        (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32))
        throw std::runtime_error("Unsupported bitmap terrain layout");
    const auto rows = std::uint64_t(height < 0 ? -std::int64_t(height) : height);
    if (exact_size && (std::uint32_t(width) != width_ || rows != height_))
        throw std::invalid_argument("Terrain file dimensions do not match the region");
    const auto stride = ((std::uint64_t(width) * bits + 31) / 32) * 4;
    if (!rle && rows > (data.size() - offset) / stride)
        throw std::runtime_error("Truncated bitmap terrain");
    const auto palette = std::size_t(header) + 14;
    const auto colors = bits <= 8 ? (dword(46) ? dword(46) : 1u << bits) : 0u;
    if (colors > (offset - palette) / 4)
        throw std::runtime_error("Truncated bitmap terrain palette");
    std::array<std::uint32_t, 3> masks{0x7c00, 0x03e0, 0x001f};
    if (compression == 3) {
        if (offset < 66 || (header != 40 && header < 52))
            throw std::runtime_error("Truncated bitmap color masks");
        masks = {dword(54), dword(58), dword(62)};
    }
    std::array<unsigned, 3> shifts{};
    if (bits == 16 || compression == 3) {
        std::uint32_t used = 0;
        for (std::size_t i = 0; i < masks.size(); ++i) {
            const auto mask = masks[i];
            shifts[i] = std::countr_zero(mask);
            if (!mask || (used & mask) || (bits == 16 && mask > 0xffffu))
                throw std::runtime_error("Invalid bitmap color masks");
            used |= mask;
            masks[i] >>= shifts[i];
            if ((masks[i] & (masks[i] + 1)) != 0)
                throw std::runtime_error("Non-contiguous bitmap color mask");
        }
    }
    const auto pixel = [&](const std::uint8_t* row, std::size_t x) {
        if (bits >= 24)
            return row + x * (bits / 8);
        const auto shift = 8 - bits - (x * bits) % 8;
        const auto index = (row[x * bits / 8] >> shift) & ((1u << bits) - 1);
        if (index >= colors)
            throw std::runtime_error("Invalid bitmap terrain palette index");
        return data.data() + palette + index * 4;
    };
    const auto columns = std::min<std::uint64_t>(width, width_);
    const auto merged_rows = std::min<std::uint64_t>(rows, height_);
    std::vector<std::uint8_t> indices;
    if (rle) {
        indices.resize(columns * merged_rows);
        auto stream = data.subspan(offset);
        std::uint64_t x = 0, y = 0;
        const auto take = [&] {
            if (stream.empty())
                throw std::runtime_error("Truncated bitmap RLE stream");
            const auto byte = stream.front();
            stream = stream.subspan(1);
            return byte;
        };
        for (;;) {
            const auto count = take(), value = take();
            if (!count && value == 1)
                break;
            if (!count && value == 0) {
                x = 0;
                ++y;
                continue;
            }
            if (!count && value == 2) {
                x += take();
                y += take();
                if (x > std::uint64_t(width) || y >= rows)
                    throw std::runtime_error("Bitmap RLE delta exceeds image");
                continue;
            }
            const unsigned length = count ? count : value;
            if (y >= rows || x + length > std::uint64_t(width))
                throw std::runtime_error("Bitmap RLE run exceeds image");
            std::uint8_t packed = value;
            for (unsigned i = 0; i < length; ++i, ++x) {
                if (!count && (bits == 8 || i % 2 == 0))
                    packed = take();
                const unsigned index = bits == 8 ? packed : (i % 2 ? packed & 15 : packed >> 4);
                if (index >= colors)
                    throw std::runtime_error("Invalid bitmap terrain palette index");
                if (x < columns && y < merged_rows)
                    indices[y * columns + x] = static_cast<std::uint8_t>(index);
            }
            const unsigned literal_bytes = bits == 8 ? length : (length + 1) / 2;
            if (!count && literal_bytes % 2)
                take();
        }
    }
    std::vector<float> samples;
    samples.reserve(columns * merged_rows);
    for (std::size_t y = 0; y < merged_rows; ++y) {
        const auto* row = rle ? nullptr : data.data() + offset + (height > 0 ? y : rows - y - 1) * stride;
        for (std::size_t x = 0; x < columns; ++x) {
            std::array<std::uint32_t, 3> rgb{};
            if (bits == 16 || compression == 3) {
                const auto index = static_cast<std::size_t>(row - data.data()) + x * (bits / 8);
                const auto value = bits == 16 ? word(index) : dword(index);
                for (std::size_t i = 0; i < rgb.size(); ++i)
                    rgb[i] = static_cast<std::uint32_t>(
                        (std::uint64_t((value >> shifts[i]) & masks[i]) * 255) / masks[i]);
            } else {
                const auto* sample =
                    rle ? data.data() + palette + indices[y * columns + x] * 4 : pixel(row, x);
                rgb = {sample[0], sample[1], sample[2]};
            }
            const auto [low, high] = std::minmax({rgb[0], rgb[1], rgb[2]});
            samples.push_back((low + high) / 510.f * 128.f);
        }
    }
    for (std::size_t y = 0; y < merged_rows; ++y)
        for (std::size_t x = 0; x < columns; ++x)
            set(static_cast<int>(x), static_cast<int>(y), samples[y * columns + x]);
}
std::string Terrain::png() const {
    const auto heights = serialized_floats();
    if (!width_ || !height_ || std::uint64_t(heights.size()) * 4 + height_ > 128u * 1024u * 1024u)
        throw std::invalid_argument("Invalid PNG terrain dimensions");
    std::string rows;
    rows.reserve(heights.size() * 4 + height_);
    for (auto y = height_; y > 0; --y) {
        rows.push_back(0);
        for (std::uint32_t x = 0; x < width_; ++x) {
            rows.append(3, terrain_gray(heights[std::size_t(y - 1) * width_ + x]));
            rows.push_back(static_cast<char>(255));
        }
    }
    z_stream stream{};
    if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK)
        throw std::runtime_error("Cannot initialize PNG terrain encoder");
    struct End {
        z_stream& stream;
        ~End() {
            deflateEnd(&stream);
        }
    } end{stream};
    std::string compressed(deflateBound(&stream, static_cast<uLong>(rows.size())), 0);
    stream.next_in = reinterpret_cast<Bytef*>(rows.data());
    stream.avail_in = static_cast<uInt>(rows.size());
    stream.next_out = reinterpret_cast<Bytef*>(compressed.data());
    stream.avail_out = static_cast<uInt>(compressed.size());
    if (deflate(&stream, Z_FINISH) != Z_STREAM_END)
        throw std::runtime_error("Cannot encode PNG terrain pixels");
    compressed.resize(stream.total_out);
    const auto write = [](std::string& output, std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
            output.push_back(static_cast<char>(value >> shift));
    };
    std::string output = "\x89PNG\r\n\x1a\n";
    const auto chunk = [&](std::string_view type, std::string_view payload) {
        write(output, static_cast<std::uint32_t>(payload.size()));
        const auto start = output.size();
        output.append(type);
        output.append(payload);
        write(output, crc32(0, reinterpret_cast<const Bytef*>(output.data() + start),
                            static_cast<uInt>(output.size() - start)));
    };
    std::string header;
    write(header, width_);
    write(header, height_);
    header.append("\x08\x06\0\0\0", 5);
    chunk("IHDR", header);
    chunk("sRGB", std::string_view("\0", 1));
    std::string gamma;
    write(gamma, 45455);
    chunk("gAMA", gamma);
    std::string density;
    write(density, 3779);
    write(density, 3779);
    density.push_back(1);
    chunk("pHYs", density);
    chunk("IDAT", compressed);
    chunk("IEND", {});
    return output;
}
void Terrain::merge_png(std::span<const std::uint8_t> data, bool exact_size) {
    constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (data.size() < 8 || !std::equal(signature.begin(), signature.end(), data.begin()))
        throw std::runtime_error("Invalid PNG terrain signature");
    const auto be32 = [](const std::uint8_t* bytes) {
        return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
               (std::uint32_t(bytes[2]) << 8) | bytes[3];
    };
    data = data.subspan(8);
    std::uint32_t width = 0, height = 0;
    unsigned channels = 0, type = 0, depth = 0;
    bool interlaced = false;
    std::vector<std::uint8_t> compressed, palette;
    bool ended = false;
    while (data.size() >= 12) {
        const auto length = be32(data.data());
        if (length > data.size() - 12)
            throw std::runtime_error("Truncated PNG terrain chunk");
        const auto chunk = data.subspan(8, length);
        const std::string_view tag(reinterpret_cast<const char*>(data.data() + 4), 4);
        const auto crc = crc32(0, data.data() + 4, static_cast<uInt>(length + 4));
        if (crc != be32(data.data() + 8 + length))
            throw std::runtime_error("Invalid PNG terrain checksum");
        if (tag == "IHDR") {
            if (width || length != 13 || chunk[10] || chunk[11] || chunk[12] > 1)
                throw std::runtime_error("Unsupported PNG terrain layout");
            width = be32(chunk.data());
            height = be32(chunk.data() + 4);
            type = chunk[9];
            depth = chunk[8];
            interlaced = chunk[12] == 1;
            switch (type) {
            case 0:
            case 3:
                channels = 1;
                break;
            case 2:
                channels = 3;
                break;
            case 4:
                channels = 2;
                break;
            case 6:
                channels = 4;
                break;
            default:
                throw std::runtime_error("Unsupported PNG terrain color type");
            }
            const bool packed = depth == 1 || depth == 2 || depth == 4;
            if ((type == 3 && depth == 16) || (packed && type != 0 && type != 3) ||
                (!packed && depth != 8 && depth != 16))
                throw std::runtime_error("Unsupported PNG terrain bit depth");
            if (!width || !height)
                throw std::runtime_error("Invalid PNG terrain dimensions");
        } else if (tag == "PLTE") {
            if (!length || length > 768 || length % 3)
                throw std::runtime_error("Invalid PNG terrain palette");
            palette.assign(chunk.begin(), chunk.end());
        } else if (tag == "IDAT")
            compressed.insert(compressed.end(), chunk.begin(), chunk.end());
        else if (tag == "IEND") {
            ended = true;
            break;
        }
        data = data.subspan(12 + length);
    }
    const auto filter_bytes = std::max(1u, channels * depth / 8);
    if (exact_size && (width != width_ || height != height_))
        throw std::invalid_argument("Terrain file dimensions do not match the region");
    constexpr std::uint64_t limit = 128u * 1024u * 1024u;
    if (!ended || !width || compressed.empty() || compressed.size() > std::numeric_limits<uInt>::max())
        throw std::runtime_error("Invalid or oversized PNG terrain");
    struct Pass {
        unsigned x, y, dx, dy;
    };
    constexpr Pass plain{0, 0, 1, 1};
    constexpr std::array<Pass, 7> adam7{
        {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}}};
    const auto passes = interlaced ? std::span<const Pass>(adam7) : std::span<const Pass>(&plain, 1);
    const auto extent = [](std::uint32_t size, unsigned offset, unsigned step) {
        return size <= offset ? 0u : 1 + (size - 1 - offset) / step;
    };
    const auto row_stride = [&](std::uint32_t columns) {
        return (std::uint64_t(columns) * channels * depth + 7) / 8 + 1;
    };
    std::uint64_t total = 0;
    for (const auto& pass : passes) {
        const auto columns = extent(width, pass.x, pass.dx), rows = extent(height, pass.y, pass.dy);
        if (!columns || !rows)
            continue;
        const auto stride = row_stride(columns);
        if (rows > (limit - total) / stride)
            throw std::runtime_error("Oversized PNG terrain");
        total += rows * stride;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(total));
    z_stream stream{};
    stream.next_in = compressed.data();
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = pixels.data();
    stream.avail_out = static_cast<uInt>(pixels.size());
    if (inflateInit(&stream) != Z_OK)
        throw std::runtime_error("Cannot initialize PNG terrain decoder");
    const auto status = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (status != Z_STREAM_END || stream.total_out != pixels.size())
        throw std::runtime_error("Invalid PNG terrain pixels");
    const auto paeth = [](int left, int above, int diagonal) {
        const int estimate = left + above - diagonal;
        const int a = std::abs(estimate - left), b = std::abs(estimate - above),
                  c = std::abs(estimate - diagonal);
        return a <= b && a <= c ? left : b <= c ? above : diagonal;
    };
    const auto sample_at = [&](const std::uint8_t* row, std::size_t index) {
        if (depth == 16)
            return unsigned(row[index * 2]);
        if (depth == 8)
            return unsigned(row[index]);
        const auto bit = index * depth;
        return unsigned(row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
    };
    const auto merged_width = std::min(width, width_), merged_height = std::min(height, height_);
    std::vector<float> samples(std::size_t(merged_width) * merged_height);
    std::size_t offset = 0;
    for (const auto& pass : passes) {
        const auto columns = extent(width, pass.x, pass.dx), rows = extent(height, pass.y, pass.dy);
        if (!columns || !rows)
            continue;
        const auto stride = row_stride(columns);
        for (std::size_t y = 0; y < rows; ++y) {
            auto* row = pixels.data() + offset;
            offset += stride;
            const auto filter = *row++;
            const auto* previous = y ? row - stride : nullptr;
            if (filter > 4)
                throw std::runtime_error("Invalid PNG terrain filter");
            for (std::size_t x = 0; x < stride - 1; ++x) {
                const int left = x >= filter_bytes ? row[x - filter_bytes] : 0;
                const int above = previous ? previous[x] : 0;
                const int diagonal = previous && x >= filter_bytes ? previous[x - filter_bytes] : 0;
                const int adjustment = filter == 0   ? 0
                                       : filter == 1 ? left
                                       : filter == 2 ? above
                                       : filter == 3 ? (left + above) / 2
                                                     : paeth(left, above, diagonal);
                row[x] = static_cast<std::uint8_t>(row[x] + adjustment);
            }
            const auto world_y = height - 1 - (pass.y + y * pass.dy);
            for (std::uint32_t x = 0; x < columns; ++x) {
                const auto index = std::size_t(x) * channels;
                const auto first = sample_at(row, index);
                std::pair<unsigned, unsigned> range;
                if (type == 0 || type == 4) {
                    const auto gray = depth < 8 ? first * 255 / ((1u << depth) - 1) : first;
                    range = {gray, gray};
                } else if (type == 3) {
                    if (first * 3 >= palette.size())
                        throw std::runtime_error("Invalid PNG terrain palette index");
                    const auto* color = palette.data() + first * 3;
                    range = std::minmax({unsigned(color[0]), unsigned(color[1]), unsigned(color[2])});
                } else
                    range = std::minmax({first, sample_at(row, index + 1), sample_at(row, index + 2)});
                const auto [low, high] = range;
                const auto world_x = pass.x + std::uint64_t(x) * pass.dx;
                if (world_x < merged_width && world_y < merged_height)
                    samples[world_y * merged_width + world_x] = (low + high) / 510.f * 128.f;
            }
        }
    }
    for (std::uint32_t y = 0; y < merged_height; ++y)
        for (std::uint32_t x = 0; x < merged_width; ++x)
            set(x, y, samples[std::size_t(y) * merged_width + x]);
}
void Terrain::merge_raw(std::span<const std::uint8_t> data, bool floating_point, bool exact_size) {
    const std::size_t stride = floating_point ? 4 : 13;
    const auto dimension =
        std::max<std::size_t>(256, static_cast<std::size_t>(std::sqrt(data.size() / stride)) / 256 * 256);
    if (exact_size && (dimension != width_ || dimension != height_))
        throw std::invalid_argument("Terrain file dimensions do not match the region");
    // LLRAW reads the final height pair before skipping the remaining channels.
    const std::size_t final_sample = floating_point ? 4 : 2;
    if (data.size() < final_sample || dimension * dimension - 1 > (data.size() - final_sample) / stride)
        throw std::ios_base::failure("Truncated terrain upload");
    for (std::size_t y = 0; y < std::min<std::size_t>(dimension, height_); ++y)
        for (std::size_t x = 0; x < std::min<std::size_t>(dimension, width_); ++x) {
            const auto* sample =
                data.data() + ((floating_point ? y : dimension - y - 1) * dimension + x) * stride;
            float height = sample[0] * sample[1] / 128.f;
            if (floating_point) {
                std::uint32_t bits = 0;
                for (unsigned i = 0; i < 4; ++i)
                    bits |= std::uint32_t(sample[i]) << (8 * i);
                height = std::bit_cast<float>(bits);
                if (!std::isfinite(height))
                    height = 0.f;
            }
            set(static_cast<int>(x), static_cast<int>(y), height);
        }
}
Terrain::Terrain(std::uint32_t width, std::uint32_t height) : width_(width), height_(height) {
    auto points = static_cast<std::uint64_t>(width) * height;
    if (points > heights_.max_size())
        throw std::length_error("Terrain dimensions overflow");
    heights_.resize(static_cast<std::size_t>(points), 0.f);
    auto bits = static_cast<std::uint64_t>(patches_x()) * patches_y();
    if (bits > std::numeric_limits<std::int32_t>::max())
        throw std::length_error("Terrain taint dimensions overflow");
    taints_.resize(static_cast<std::size_t>((bits + 31) / 32), 0);
}
void Terrain::initialize_island() {
    const float center_x = width_ * .5f, center_y = height_ * .5f;
    for (std::uint32_t x = 0; x < width_; ++x)
        for (std::uint32_t y = 0; y < height_; ++y) {
            const float dx = x - center_x, dy = y - center_y;
            const float distance = dx * dx + dy * dy;
            const float peak = 25.f * std::max(0.f, 1.f - distance / 2500.f);
            const float base = 10.f * std::max(0.f, 1.f - distance / 10000.f);
            set(x, y, std::max(peak, base));
        }
}
Terrain::Terrain(const Terrain& other) : width_(other.width_), height_(other.height_) {
    std::shared_lock lock(other.mutex_);
    heights_ = other.heights_;
    taints_ = other.taints_;
    tainted_count_ = other.tainted_count_;
}
void Terrain::replace_heights(const Terrain& source) {
    if (this == &source)
        return;
    if (width_ != source.width_ || height_ != source.height_)
        throw std::invalid_argument("Terrain dimensions do not match");
    std::unique_lock destination_lock(mutex_, std::defer_lock);
    std::shared_lock source_lock(source.mutex_, std::defer_lock);
    std::lock(destination_lock, source_lock);
    for (std::uint32_t x = 0; x < width_; ++x)
        for (std::uint32_t y = 0; y < height_; ++y) {
            const auto offset = std::size_t(x) * height_ + y;
            const float height = source.heights_[offset];
            if (heights_[offset] == height)
                continue;
            heights_[offset] = height;
            ++revision_;
            if (x / patch_size < patches_x() && y / patch_size < patches_y())
                taint_set_unlocked(x / patch_size + (y / patch_size) * patches_x(), true);
        }
}
std::size_t Terrain::index(int x, int y) const {
    if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= width_ || static_cast<std::uint32_t>(y) >= height_)
        throw std::out_of_range("Terrain coordinate");
    return static_cast<std::size_t>(x) * height_ + static_cast<std::size_t>(y);
}
std::size_t Terrain::patch_index(std::uint32_t px, std::uint32_t py) const {
    if (px >= patches_x() || py >= patches_y())
        throw std::out_of_range("Terrain patch");
    return static_cast<std::size_t>(px) * patch_size * height_ + py * patch_size;
}
float Terrain::get(int x, int y) const {
    std::shared_lock lock(mutex_);
    return heights_[index(x, y)];
}
void Terrain::set(int x, int y, float value) {
    std::unique_lock lock(mutex_);
    auto i = index(x, y);
    if (heights_[i] != value) {
        heights_[i] = value;
        ++revision_;
        taint_set_unlocked(x / 16 + (y / 16) * static_cast<int>(patches_x()), true);
    }
}
void Terrain::clear_land(float value) {
    std::unique_lock lock(mutex_);
    std::fill(heights_.begin(), heights_.end(), value);
    ++revision_;
}
std::uint64_t Terrain::revision() const {
    std::shared_lock lock(mutex_);
    return revision_;
}
std::string Terrain::database_blob() const {
    const auto samples = serialized_floats();
    std::string bytes;
    bytes.reserve(8 + samples.size() * 4);
    const auto write = [&](std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<char>(value >> shift));
    };
    write(width_);
    write(height_);
    for (float height : samples) {
        if (std::abs(height) < 1e8f)
            height = std::round(height * 1000.f) / 1000.f;
        write(std::bit_cast<std::uint32_t>(height));
    }
    return network::gzip_compress(bytes);
}
void Terrain::load_database_blob(int format, std::string_view blob) {
    std::string uncompressed;
    if (format == database_format) {
        uncompressed = network::inflate_gzip(blob);
        blob = uncompressed;
    }
    std::size_t offset = 0;
    const auto read = [&](unsigned bytes) {
        if (bytes > blob.size() - offset)
            throw std::invalid_argument("Truncated terrain heightfield");
        std::uint64_t result = 0;
        for (unsigned i = 0; i < bytes; ++i)
            result |= std::uint64_t(static_cast<unsigned char>(blob[offset++])) << (8 * i);
        return result;
    };
    const bool compressed = format == 27;
    const bool variable = compressed || format == 22 || format == database_format;
    if (compressed)
        read(4); // Embedded format code.
    const auto width = variable ? std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(4))) : 256;
    const auto height = variable ? std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(4))) : 256;
    const auto factor = compressed ? std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(read(4))) : 1;
    if (factor == 0)
        throw std::invalid_argument("Invalid terrain heightfield compression factor");
    std::vector<float> replacement(std::size_t(width_) * height_, default_height);
    const auto columns = std::min<std::uint32_t>(std::max(0, width), width_);
    const auto rows = std::min<std::uint32_t>(std::max(0, height), height_);
    if (variable) {
        for (std::uint64_t y = 0; y < rows; ++y)
            for (std::uint64_t x = 0; x < columns; ++x) {
                replacement[x * height_ + y] =
                    compressed
                        ? float(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(read(2)))) / factor
                        : std::bit_cast<float>(static_cast<std::uint32_t>(read(4)));
            }
    } else {
        for (std::uint32_t x = 0; x < 256; ++x)
            for (std::uint32_t y = 0; y < 256; ++y) {
                const float sample = static_cast<float>(std::bit_cast<double>(read(8)));
                if (x < columns && y < rows)
                    replacement[x * height_ + y] = sample;
            }
    }
    std::unique_lock lock(mutex_);
    heights_.swap(replacement);
    ++revision_;
    std::fill(taints_.begin(), taints_.end(), 0);
    tainted_count_ = 0;
}
framework::Vector3 Terrain::script_ground_normal(float x, float y, float offset_x, float offset_y) const {
    if (width_ == 0 || height_ == 0)
        return {0.f, 0.f, 1.f};
    std::shared_lock lock(mutex_);
    const int width = static_cast<int>(width_);
    const int height = static_cast<int>(height_);
    const int sample_x = std::clamp(source_int(x + offset_x), 0, width - 1);
    const int sample_y = std::clamp(source_int(y + offset_y), 0, height - 1);
    const int origin_x = source_int(x);
    const int origin_y = source_int(y);
    // OpenSim takes the base height at the prim, then applies the offset to the neighbors.
    const float base = origin_x >= 0 && origin_x < width && origin_y >= 0 && origin_y < height
                           ? heights_[index(origin_x, origin_y)]
                           : 0.f;
    const float east = sample_x + 1 < width ? heights_[index(sample_x + 1, sample_y)] : base;
    const float north = sample_y + 1 < height ? heights_[index(sample_x, sample_y + 1)] : base;
    const float dx = base - east;
    const float dy = base - north;
    const float inverse_length = 1.f / std::sqrt(dx * dx + dy * dy + 1.f);
    return {dx * inverse_length, dy * inverse_length, inverse_length};
}

float Terrain::height_at(float x, float y) const {
    std::shared_lock lock(mutex_);
    auto read = [&](int xx, int yy) { return heights_[index(xx, yy)]; };
    int ix, iy;
    int mix = static_cast<int>(width_) - 1, miy = static_cast<int>(height_) - 1;
    float dx, dy;
    if (x <= 0) {
        if (y <= 0)
            return read(0, 0);
        iy = source_int(y);
        if (iy >= miy)
            return read(0, miy);
        dy = y - iy;
        float h = read(0, iy);
        ++iy;
        return h + (read(0, iy) - h) * dy;
    }
    ix = source_int(x);
    if (ix >= mix) {
        if (y <= 0)
            return read(mix, 0);
        iy = source_int(y);
        if (y >= miy)
            return read(mix, miy);
        dy = y - iy;
        float h = read(mix, iy);
        ++iy;
        return h + (read(mix, iy) - h) * dy;
    }
    dx = x - ix;
    if (y <= 0) {
        float h = read(ix, 0);
        ++ix;
        return h + (read(ix, 0) - h) * dx;
    }
    iy = source_int(y);
    if (iy >= miy) {
        float h = read(ix, miy);
        ++ix;
        return h + (read(ix, miy) - h) * dx;
    }
    dy = y - iy;
    float h0 = read(ix, iy), h1, h2;
    if (dy > dx) {
        ++iy;
        h2 = read(ix, iy);
        h1 = (h2 - h0) * dy;
        ++ix;
        h2 = (read(ix, iy) - h2) * dx;
    } else {
        ++ix;
        h2 = read(ix, iy);
        h1 = (h2 - h0) * dx;
        ++iy;
        h2 = (read(ix, iy) - h2) * dy;
    }
    return h0 + h1 + h2;
}
std::array<float, 256> Terrain::patch(std::uint32_t px, std::uint32_t py, float sub, float premult) const {
    std::shared_lock lock(mutex_);
    auto start = patch_index(px, py);
    std::array<float, 256> result{};
    std::size_t k = 0;
    for (std::size_t y = 0; y < 16; ++y)
        for (std::size_t x = 0; x < 16; ++x)
            result[k++] = (heights_[start + x * height_ + y] - sub) * premult;
    return result;
}
std::pair<float, float> Terrain::patch_min_max(std::uint32_t px, std::uint32_t py) const {
    std::shared_lock lock(mutex_);
    auto start = patch_index(px, py);
    float min = heights_[start], max = min;
    // Preserve TerrainData.GetPatchMinMax's first-column count (15): its final
    // point is omitted, while the remaining 15 columns each scan all16 points.
    for (std::size_t x = 0; x < 16; ++x)
        for (std::size_t y = 0; y < (x == 0 ? 15u : 16u); ++y) {
            float v = heights_[start + x * height_ + y];
            if (v > max)
                max = v;
            else if (v < min)
                min = v;
        }
    return {min, max};
}
std::vector<float> Terrain::serialized_floats() const {
    std::shared_lock lock(mutex_);
    std::vector<float> out;
    out.reserve(heights_.size());
    for (std::uint32_t y = 0; y < height_; ++y)
        for (std::uint32_t x = 0; x < width_; ++x)
            out.push_back(heights_[static_cast<std::size_t>(x) * height_ + y]);
    return out;
}
std::vector<float> Terrain::compressed_map() const {
    std::shared_lock lock(mutex_);
    return heights_;
}
void Terrain::taint_set_unlocked(int bit, bool value) {
    if (bit < 0 || static_cast<std::size_t>(bit / 32) >= taints_.size())
        throw std::out_of_range("Terrain taint");
    auto& word = taints_[static_cast<std::size_t>(bit / 32)];
    auto mask = std::uint32_t(1) << (bit & 31);
    bool old = (word & mask) != 0;
    if (old == value)
        return;
    if (value) {
        word |= mask;
        ++tainted_count_;
    } else {
        word &= ~mask;
        --tainted_count_;
    }
}
bool Terrain::taint_get_unlocked(int bit, bool clear) {
    if (bit < 0 || static_cast<std::size_t>(bit / 32) >= taints_.size())
        throw std::out_of_range("Terrain taint");
    auto& word = taints_[static_cast<std::size_t>(bit / 32)];
    auto mask = std::uint32_t(1) << (bit & 31);
    if (!(word & mask))
        return false;
    if (clear) {
        word ^= mask;
        --tainted_count_;
    }
    return true;
}
void Terrain::clear_taint() {
    std::unique_lock lock(mutex_);
    std::fill(taints_.begin(), taints_.end(), 0);
    tainted_count_ = 0;
}
void Terrain::taint_all() {
    std::unique_lock lock(mutex_);
    std::fill(taints_.begin(), taints_.end(), ~std::uint32_t(0));
    tainted_count_ = static_cast<std::int32_t>(patches_x() * patches_y());
}
bool Terrain::tainted() const {
    std::shared_lock lock(mutex_);
    return tainted_count_ > 0;
}
std::int32_t Terrain::tainted_count() const {
    std::shared_lock lock(mutex_);
    return tainted_count_;
}
bool Terrain::tainted_at(int x, int y, bool clear) {
    std::unique_lock lock(mutex_);
    return taint_get_unlocked(x / 16 + (y / 16) * static_cast<int>(patches_x()), clear);
}
bool Terrain::tainted_patch(int px, int py, bool clear) {
    std::unique_lock lock(mutex_);
    return taint_get_unlocked(px + py * static_cast<int>(patches_x()), clear);
}
int Terrain::next_taint(int start) {
    std::unique_lock lock(mutex_);
    auto logical = patches_x() * patches_y();
    if (tainted_count_ <= 0 || start < 0 || static_cast<std::uint32_t>(start) >= logical)
        return -1;
    for (std::size_t bit = static_cast<std::size_t>(start); bit < taints_.size() * 32; ++bit)
        if (taint_get_unlocked(static_cast<int>(bit), true))
            return static_cast<int>(bit);
    return -1;
}
Terrain Terrain::clone() const {
    Terrain copy(width_, height_);
    std::shared_lock lock(mutex_);
    copy.heights_ = heights_;
    return copy;
}
}
