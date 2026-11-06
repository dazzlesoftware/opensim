#include "opensim/scene/texture_layers.hpp"
#include "opensim/scene/map_tile.hpp"
#include "opensim/services/flotsam_cache_files.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc != 3)
        return 1;
    try {
        std::ifstream fixture(argv[2], std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(fixture), {}};
        const auto next_boundary = opensim::scene::open_texture_layer_decoder(argv[1], true);
        const auto packet_boundary = opensim::scene::open_texture_layer_decoder(argv[1], false);
        const auto encode_map = opensim::scene::open_map_jpeg2000_encoder(argv[1]);
        const auto jpeg_map = opensim::scene::open_map_jpeg_encoder(argv[1]);
        const std::vector<float> heights(256 * 256, 21.f);
        const auto pixels = opensim::scene::render_shaded_map_tile(heights, 256, 256, 20.f);
        const auto encoded_map = encode_map(pixels, 256, 256, 3, true);
        const auto jpeg = jpeg_map(encoded_map);
        if (encoded_map.size() < 4 || encoded_map[0] != 0xff || encoded_map[1] != 0x4f ||
            !jpeg.starts_with("\xff\xd8") || !jpeg.ends_with("\xff\xd9") ||
            !encode_map(pixels, 128, 256, 3, true).empty())
            throw std::runtime_error("Generated terrain tile did not round-trip through the native codecs");
        const auto average = opensim::scene::open_map_texture_average(argv[1]);
        const std::array<std::uint8_t, 3> expected_color{165, 100, 33};
        std::vector<std::uint8_t> flat_texture;
        flat_texture.reserve(64 * 64 * expected_color.size());
        for (int pixel = 0; pixel < 64 * 64; ++pixel)
            flat_texture.insert(flat_texture.end(), expected_color.begin(), expected_color.end());
        if (average(encode_map(flat_texture, 64, 64, 3, true)) != expected_color ||
            average(std::vector<std::uint8_t>{1, 2, 3}))
            throw std::runtime_error("JPEG 2000 terrain texture average differs");
        const auto decode_bitmap = opensim::scene::open_map_bitmap_decoder(argv[1]);
        std::vector<std::uint8_t> rgba_texture;
        rgba_texture.reserve(64 * 64 * 4);
        for (int pixel = 0; pixel < 64 * 64; ++pixel)
            rgba_texture.insert(rgba_texture.end(), {165, 100, 33, 96});
        const auto rgba_bitmap = decode_bitmap(encode_map(rgba_texture, 64, 64, 4, true));
        if (rgba_bitmap.width != 64 || rgba_bitmap.height != 64 ||
            rgba_bitmap.channels != 4 || rgba_bitmap.pixels != rgba_texture)
            throw std::runtime_error("JPEG 2000 RGBA map bitmap differs");
        using opensim::scene::TextureLayer;
        if (next_boundary(bytes) != std::vector<TextureLayer>{{0, 580}, {581, 1987}, {1988, 7983},
                                                              {7984, 31969}, {31970, 36044}} ||
            packet_boundary(bytes) != std::vector<TextureLayer>{{135, 580}, {581, 1987}, {1988, 7983},
                                                                {7984, 31969}, {31970, 36041}} ||
            !next_boundary(std::vector<std::uint8_t>(3000, 42)).empty()) {
            std::cerr << "JPEG 2000 layer boundaries differ from the reference decoders\n";
            return 1;
        }
        int decodes = 0;
        const auto cached = opensim::scene::cache_texture_layer_decoder([&](auto input) {
            ++decodes;
            return next_boundary(input);
        });
        const auto valid_id = opensim::framework::Uuid::random();
        auto first = std::async(std::launch::async, [&] { return cached(valid_id, bytes); });
        auto second = std::async(std::launch::async, [&] { return cached(valid_id, bytes); });
        const auto first_result = first.get();
        const auto second_result = second.get();
        const auto invalid_id = opensim::framework::Uuid::random();
        const std::vector<std::uint8_t> invalid(3000, 42);
        if (first_result.empty() || first_result != second_result ||
            !cached(invalid_id, invalid).empty() || !cached(invalid_id, invalid).empty() ||
            decodes != 2) {
            std::cerr << "Texture layer cache did not reuse the decoder result\n";
            return 1;
        }
        const auto directory = std::filesystem::temp_directory_path() /
            ("opensim-texture-layer-test-" + opensim::framework::Uuid::random().str());
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Could not create texture layer test directory");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{directory};
        const auto indexed_bmp = directory / "indexed.bmp";
        const std::array<std::uint8_t, 70> indexed_bytes{
            'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 62, 0, 0, 0,
            40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 1, 0,
            0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 255, 0, 255, 0, 0, 0,
            0x80, 0xff, 0xff, 0xff, 0x40, 0xff, 0xff, 0xff,
        };
        {
            std::ofstream file(indexed_bmp, std::ios::binary);
            file.write(reinterpret_cast<const char*>(indexed_bytes.data()), indexed_bytes.size());
        }
        const auto bitmap = opensim::scene::load_static_map_bitmap(indexed_bmp);
        if (bitmap.width != 2 || bitmap.height != 2 || bitmap.channels != 3 ||
            bitmap.pixels != std::vector<std::uint8_t>{255, 0, 0, 0, 0, 255, 0, 0, 255, 255, 0, 0})
            throw std::runtime_error("Indexed BMP palette, padding or orientation differs");

        const auto check_bmp = [&](std::span<const std::uint8_t> bytes, std::size_t channels,
                                   std::vector<std::uint8_t> pixels,
                                   std::uint32_t width = 2, std::uint32_t height = 1) {
            const auto path = directory / "fixture.bmp";
            {
                std::ofstream file(path, std::ios::binary);
                file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
            const auto decoded = opensim::scene::load_static_map_bitmap(path);
            if (decoded.width != width || decoded.height != height || decoded.channels != channels ||
                decoded.pixels != pixels)
                throw std::runtime_error("BMP fixture pixels differ");
        };
        const std::array<std::uint8_t, 70> rgb565{
            0x42, 0x4d, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x00,
            0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0xff, 0xff,
            0xff, 0xff, 0x01, 0x00, 0x10, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0xe0, 0x07,
            0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xe0, 0x07,
        };
        check_bmp(rgb565, 3, {255, 0, 0, 0, 255, 0});
        const std::array<std::uint8_t, 78> rgba{
            0x42, 0x4d, 0x4e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00,
            0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0xff, 0xff,
            0xff, 0xff, 0x01, 0x00, 0x20, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff,
            0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0x11, 0x33,
            0xaa, 0x80, 0xee, 0x22, 0x44, 0xff,
        };
        check_bmp(rgba, 4, {17, 51, 170, 128, 238, 34, 68, 255});

        const std::array<std::uint8_t, 78> rle4{
            0x42, 0x4d, 0x4e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x00,
            0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x02, 0x00,
            0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0c, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x05, 0x12, 0x00, 0x00, 0x00, 0x05,
            0x21, 0x21, 0x20, 0x00, 0x00, 0x01,
        };
        check_bmp(rle4, 3, {0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 0, 0}, 5, 2);
        const std::array<std::uint8_t, 80> rle8{
            0x42, 0x4d, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x00,
            0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x02, 0x00,
            0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0e, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0x01, 0x01, 0x00, 0x02, 0x01, 0x01,
            0x00, 0x03, 0x02, 0x01, 0x02, 0x00, 0x00, 0x01,
        };
        check_bmp(rle8, 3, {0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 5, 2);

        using namespace opensim::services;
        const auto open_storage = [&] {
            auto backend = native_flotsam_file_backend({});
            backend->worker_available = [] { return true; };
            backend->enqueue = [](auto write) { write(); };
            auto store = std::make_shared<FlotsamFileStore>(
                FlotsamFileStore::Options{directory}, backend);
            FlotsamMemoryCache::Options settings;
            settings.file = true;
            return std::make_shared<FlotsamMemoryCache>(
                settings, FlotsamMemoryCache::Clock{}, false, flotsam_cache_files(store, backend));
        };
        auto storage = open_storage();
        const auto create_decoder = [&] {
            return opensim::scene::cache_texture_layer_decoder([&](auto input) {
                ++decodes;
                return next_boundary(input);
            }, [&] { return storage; });
        };
        const auto written = create_decoder()(valid_id, bytes);
        storage.reset();
        storage = open_storage();
        const auto key = "j2k" + valid_id.str();
        const auto record = storage->get_cached(key);
        const std::string expected = "0|580|580\n581|1987|1406\n1988|7983|5995\n"
                                     "7984|31969|23985\n31970|36044|4074";
        if (!record || !record->data || !record->local() || !record->temporary() ||
            std::string(record->data->begin(), record->data->end()) != expected ||
            written != create_decoder()(valid_id, bytes) || decodes != 3) {
            std::cerr << "Legacy layer record did not round trip through the file-backed asset cache\n";
            return 1;
        }
        record->data = std::make_shared<std::vector<std::uint8_t>>(1, '!');
        storage->cache(record, true);
        storage.reset();
        storage = open_storage();
        if (create_decoder()(valid_id, bytes) != written || decodes != 4) {
            std::cerr << "Malformed persisted layer record was not regenerated\n";
            return 1;
        }
        storage.reset();
        storage = open_storage();
        if (create_decoder()(valid_id, bytes) != written || decodes != 4) {
            std::cerr << "Regenerated layer record did not survive reopening the asset cache\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
