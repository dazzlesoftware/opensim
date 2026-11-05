#include "opensim/scene/texture_layers.hpp"
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
