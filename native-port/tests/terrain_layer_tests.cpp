#include "opensim/scene/terrain_layer.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <bit>
using namespace opensim::scene;
static void fill(Terrain &t, int mode, int seed) {
    for (unsigned y = 0; y < t.height(); y++)
        for (unsigned x = 0; x < t.width(); x++) {
            float f = 21.f;
            if (mode == 1)
                f = float(int((x * 17 + y * 31 + seed * 13) % 397) - 198) * .25f;
            if (mode == 2)
                f = 21.f + float((x + y + seed) % 3) * .001f;
            if (mode == 3)
                f = float(x) * .125f - float(y) * .0625f + seed;
            t.set(x, y, f);
        }
}
int main(int argc, char **) {
    try {
        if (argc > 1) {
            unsigned w, h, n;
            int mode, seed;
            while (std::cin >> w >> h >> mode >> seed >> n) {
                Terrain t(w, h);
                fill(t, mode, seed);
                std::vector<TerrainPatchCoordinate> p(n);
                for (auto &v : p)
                    std::cin >> v.x >> v.y;
                auto result = terrain_layer_packets(t, p);
                for (const auto &packet : result) {
                    std::cout << unsigned(packet.type) << ":";
                    for (auto b : packet.data)
                        std::cout << std::hex << std::setfill('0') << std::setw(2) << unsigned(b);
                    std::cout << std::dec << ";";
                }
                std::cout << "\n";
            }
            return 0;
        }
        Terrain t(256, 256);
        t.clear_land();
        std::vector<TerrainPatchCoordinate> p{{0, 0}};
        auto a = terrain_layer_packets(t, p);
        if (a.size() != 1 || a[0].type != 76 || a[0].data.size() != 14 || a[0].data[0] != 8 ||
            a[0].data[1] != 1 || a[0].data[2] != 16 || a[0].data[3] != 76)
            throw std::runtime_error("flat patch framing");
        auto empty = terrain_layer_packets(t, {});
        if (empty[0].data.size() != 5 || empty[0].data.back() != 97)
            throw std::runtime_error("empty stream");
        Terrain offset_patch(48, 32);
        offset_patch.clear_land();
        offset_patch.set(16, 31, -100.f);
        offset_patch.set(17, 31, -9.f);
        offset_patch.set(31, 16, 37.f);
        offset_patch.set(32, 16, 99.f);
        const auto [patch_low, patch_high] = offset_patch.patch_min_max(1, 1);
        if (patch_low != -9.f || patch_high != 37.f || offset_patch.patch(1, 1)[15 * 16] != -100.f)
            throw std::runtime_error(
                "Offset patch must use region stride and preserve the source first-column omission");
        offset_patch.clear_taint();
        Terrain replacement(offset_patch);
        replacement.set(20, 18, 47.f);
        replacement.set(21, 18, 48.f);
        const auto revision = offset_patch.revision();
        offset_patch.replace_heights(replacement);
        offset_patch.replace_heights(replacement);
        if (offset_patch.get(20, 18) != 47.f || offset_patch.get(21, 18) != 48.f ||
            offset_patch.revision() != revision + 2 || offset_patch.tainted_count() != 1 ||
            !offset_patch.tainted_patch(1, 1))
            throw std::runtime_error("Terrain replacement must mark only changed patches and revisions");
        p = {{16, 0}};
        bool threw = false;
        try {
            terrain_layer_packets(t, p);
        } catch (const std::out_of_range &) {
            threw = true;
        }
        if (!threw)
            throw std::runtime_error("patch bounds");
        Terrain stored(32, 16);
        stored.set(6, 7, 12.3456f);
        stored.set(7, 6, -9.8766f);
        stored.set(0, 0, 0.0625f);
        stored.set(1, 0, -0.0625f);
        stored.set(2, 0, -0.0001f);
        stored.set(3, 0, 100000000.f);
        stored.set(4, 0, 99999992.f);
        Terrain loaded(32, 16);
        loaded.load_database_blob(Terrain::database_format, stored.database_blob());
        if (loaded.get(6, 7) != 12.346f || loaded.get(7, 6) != -9.877f || loaded.tainted())
            throw std::runtime_error(
                "revision 23 terrain roundtrip preserves orientation and millimeter rounding");
        // MathF.Round(height, 3, MidpointRounding.AwayFromZero) reference bits.
        const std::uint32_t rounded[]{0x3d810625, 0xbd810625, 0x80000000, 0x4cbebc20, 0x4cbebc1f};
        for (int x = 0; x < 5; ++x)
            if (std::bit_cast<std::uint32_t>(loaded.get(x, 0)) != rounded[x])
                throw std::runtime_error("revision 23 height rounding differs from MathF.Round");
        // Revision 27 stores signed 16-bit heights in row order after four int32 fields.
        const unsigned char compressed_fixture[]{27,  0, 0, 0, 2,   0, 0,  0,   2, 0,   0,   0,
                                                 100, 0, 0, 0, 125, 0, 56, 255, 0, 128, 255, 127};
        Terrain compressed(16, 16);
        compressed.load_database_blob(
            27, {reinterpret_cast<const char *>(compressed_fixture), sizeof compressed_fixture});
        if (compressed.get(0, 0) != 1.25f || compressed.get(1, 0) != -2.f ||
            compressed.get(0, 1) != -327.68f || compressed.get(1, 1) != 327.67f ||
            compressed.get(2, 0) != Terrain::default_height || compressed.tainted())
            throw std::runtime_error("revision 27 terrain signed heights, orientation or default fill");
        const char oversized_header[]{1, 32, 0, 0, 1, 0, 0, 0, 0, 0, 0, 64};
        Terrain narrow(1, 1);
        narrow.load_database_blob(22, {oversized_header, sizeof oversized_header});
        if (narrow.get(0, 0) != 2.f)
            throw std::runtime_error("revision 22 dimensions must clamp to the region");
        const char empty_header[]{0, 0, 0, 0, 1, 0, 0, 0};
        narrow.load_database_blob(22, {empty_header, sizeof empty_header});
        if (narrow.get(0, 0) != Terrain::default_height)
            throw std::runtime_error("revision 22 empty dimensions must restore default height");
        std::string legacy(256 * 256 * 8, '\0');
        const auto legacy_sample = [&](unsigned x, unsigned y, double height) {
            const auto bits = std::bit_cast<std::uint64_t>(height);
            const auto offset = (x * 256 + y) * 8;
            for (unsigned byte = 0; byte < 8; ++byte)
                legacy[offset + byte] = static_cast<char>(bits >> (8 * byte));
        };
        legacy_sample(2, 9, -17.125);
        legacy_sample(9, 2, 124.5);
        legacy_sample(255, 255, 15.25);
        Terrain original(272, 256);
        original.load_database_blob(1, legacy);
        if (original.get(2, 9) != -17.125f || original.get(9, 2) != 124.5f ||
            original.get(255, 255) != 15.25f || original.get(256, 255) != Terrain::default_height ||
            original.tainted())
            throw std::runtime_error("legacy terrain column order, heights or default fill");
        const std::uint8_t gray16[]{137, 80,  78,  71,  13,  10,  26, 10, 0,  0,   0,   13, 73, 72,
                                    68,  82,  0,   0,   0,   2,   0,  0,  0,  1,   16,  0,  0,  0,
                                    0,   129, 217, 252, 21,  0,   0,  0,  13, 73,  68,  65, 84, 120,
                                    156, 99,  16,  50,  105, 104, 0,  0,  2,  105, 1,   71, 78, 219,
                                    211, 79,  0,   0,   0,   0,   73, 69, 78, 68,  174, 66, 96, 130};
        const std::uint8_t gray_alpha16[]{
            137, 80,  78, 71,  13,  10, 26, 10, 0,  0,  0,   13,  73,  72,  68, 82, 0,  0,  0,
            2,   0,   0,  0,   1,   16, 4,  0,  0,  0,  14,  187, 107, 66,  0,  0,  0,  17, 73,
            68,  65,  84, 120, 156, 99, 16, 50, 97, 96, 104, 104, 104, 104, 0,  0,  7,  5,  2,
            71,  128, 81, 48,  6,   0,  0,  0,  0,  73, 69,  78,  68,  174, 66, 96, 130};
        const std::uint8_t rgba16[]{137, 80,  78,  71,  13,  10,  26,  10,  0,   0,   0,   13,  73,  72,
                                    68,  82,  0,   0,   0,   2,   0,   0,   0,   1,   16,  6,   0,   0,
                                    0,   164, 178, 163, 201, 0,   0,   0,   25,  73,  68,  65,  84,  120,
                                    156, 99,  16,  50,  9,   171, 152, 181, 135, 129, 161, 161, 161, 225,
                                    127, 3,   67,  67,  3,   0,   49,  80,  6,   106, 176, 151, 200, 102,
                                    0,   0,   0,   0,   73,  69,  78,  68,  174, 66,  96,  130};
        const auto check_png = [](std::span<const std::uint8_t> png, std::uint32_t first) {
            Terrain terrain(16, 16);
            terrain.clear_land();
            terrain.merge_png(png);
            // Windows System.Drawing GetPixel().GetBrightness() * 128, before storage rounding.
            if (std::bit_cast<std::uint32_t>(terrain.get(0, 0)) != first ||
                std::bit_cast<std::uint32_t>(terrain.get(1, 0)) != 0x42808081 ||
                terrain.get(2, 0) != Terrain::default_height)
                throw std::runtime_error("16-bit PNG terrain conversion");
        };
        check_png(gray16, 0x41109091);
        check_png(gray_alpha16, 0x41109091);
        check_png(rgba16, 0x422cacad);
        Terrain raw(16, 16);
        raw.clear_land();
        std::vector<std::uint8_t> raw32(256 * 256 * 4);
        const std::uint32_t words[]{0x7fc00000, 0x7f800000, 0xff800000, 0x42280000};
        for (unsigned x = 0; x < 4; ++x)
            for (unsigned byte = 0; byte < 4; ++byte)
                raw32[4 * x + byte] = static_cast<std::uint8_t>(words[x] >> (8 * byte));
        raw.merge_raw(raw32, true);
        if (raw.get(0, 0) != 0.f || raw.get(1, 0) != 0.f || raw.get(2, 0) != 0.f || raw.get(3, 0) != 42.f)
            throw std::runtime_error("RAW32 terrain must zero non-finite samples and retain finite samples");
        Terrain llraw(256, 256);
        std::vector<std::uint8_t> compact(256 * 256 * 13 - 11);
        compact[0] = 20;
        compact[1] = 128;
        compact[compact.size() - 2] = 42;
        compact.back() = 128;
        llraw.merge_raw(compact, false);
        if (llraw.get(0, 255) != 20.f || llraw.get(255, 0) != 42.f)
            throw std::runtime_error("LLRAW terrain must read the last height pair without unused channels");
        compact.pop_back();
        bool truncated = false;
        try {
            llraw.merge_raw(compact, false);
        } catch (const std::runtime_error &) {
            truncated = true;
        }
        if (!truncated || llraw.get(255, 0) != 42.f)
            throw std::runtime_error("Incomplete LLRAW height pair must leave terrain intact");
        Terrain raw_export(16, 16);
        const float raw_heights[]{0, 21, 24, 27, 42, 10.75f, 60, 127.9f, 508, 4094};
        const std::uint16_t raw_pairs[]{0x2b00, 0x1c60, 0x3040, 0x2460, 0x20a8,
                                        0x202b, 0x20f0, 0x996b, 0xfffe, 0xffff};
        for (int x = 0; x < 10; ++x)
            raw_export.set(x, 15, raw_heights[x]);
        const auto raw_bytes = raw_export.llraw();
        for (int x = 0; x < 10; ++x) {
            const auto *sample = raw_bytes.data() + 13 * x;
            if ((sample[0] | (sample[1] << 8)) != raw_pairs[x] || sample[11] != sample[0] ||
                sample[12] != sample[1])
                throw std::runtime_error(
                    "LLRAW quantization pair differs from .NET Array.Sort and BinarySearch");
        }
        Terrain terragen(16, 16);
        terragen.clear_land(10.f);
        terragen.set(3, 7, 60.f);
        const auto ter = terragen.terragen();
        if (ter.size() != 68 + 16 * 16 * 2 || ter.substr(56, 4) != "ALTW" ||
            ter.substr(ter.size() - 4) != "EOF ")
            throw std::runtime_error("Terragen export framing");
        Terrain decoded_terragen(16, 16);
        decoded_terragen.merge_terragen({reinterpret_cast<const std::uint8_t *>(ter.data()), ter.size()},
                                        true);
        if (decoded_terragen.get(0, 0) != 10.f || decoded_terragen.get(3, 7) != 60.f - 50.f / 65536.f)
            throw std::runtime_error("Terragen export preserves orientation and signed elevation endpoints");
        Terrain terragen_ties(16, 16);
        const float elevations[]{-32.f, 32.f, 0.5f / 1024.f, 1.5f / 1024.f, -1.5f / 1024.f};
        for (int x = 0; x < 5; ++x)
            terragen_ties.set(x, 0, elevations[x]);
        const auto tie_export = terragen_ties.terragen();
        const std::string reference_elevations("\0\x80\xff\x7f\0\0\x02\0\xfe\xff", 10);
        if (tie_export.substr(60, 4) != std::string("\x40\0\0\0", 4) ||
            tie_export.substr(64, 10) != reference_elevations)
            throw std::runtime_error("Terragen elevation encoding differs from Convert.ToInt16");
        terragen_ties.clear_land(33.25f);
        const auto flat_export = terragen_ties.terragen();
        if (flat_export.substr(60, 6) != std::string("\0\0\x21\0\xff\x7f", 6))
            throw std::runtime_error(
                "Terragen flat heightfield preserves the source zero scale and floored base");
        Terrain bitmap(16, 16);
        bitmap.set(0, 0, 10.75f);
        bitmap.set(1, 0, -2.f);
        bitmap.set(0, 1, 140.f);
        bitmap.set(1, 1, 1.5f);
        // Windows System.Drawing Bitmap.Save header and bottom-up BGRA pixel order.
        const char bitmap_header[] = "BM\x36\x04\0\0\0\0\0\0\x36\0\0\0\x28\0\0\0\x10\0\0\0\x10\0\0\0"
                                     "\x01\0\x20\0\0\0\0\0\0\0\0\0\xc4\x0e\0\0\xc4\x0e\0\0\0\0\0\0\0\0\0\0";
        std::string reference_bmp(bitmap_header, sizeof bitmap_header - 1);
        for (int i = 0; i < 16 * 16; ++i)
            reference_bmp.append("\0\0\0\xff", 4);
        reference_bmp.replace(54, 4, "\x14\x14\x14\xff", 4);
        reference_bmp.replace(54 + 16 * 4, 8, "\xff\xff\xff\xff\x02\x02\x02\xff", 8);
        if (bitmap.bitmap() != reference_bmp)
            throw std::runtime_error("Terrain BMP output differs from Windows System.Drawing");
        const auto png = bitmap.png();
        Terrain decoded_png(16, 16);
        decoded_png.merge_png({reinterpret_cast<const std::uint8_t *>(png.data()), png.size()}, true);
        if (decoded_png.get(0, 0) != 20.f / 255.f * 128.f || decoded_png.get(1, 0) != 0.f ||
            decoded_png.get(0, 1) != 128.f || decoded_png.get(1, 1) != 2.f / 255.f * 128.f)
            throw std::runtime_error(
                "Terrain PNG roundtrip preserves orientation and source grayscale quantization");
        std::cout << "PASS: terrain layer framing, database roundtrip and 16-bit PNG conversion\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
