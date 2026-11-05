// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_memory_cache.hpp"
#include "opensim/services/region_asset_runtime.hpp"
#include <filesystem>
#include <fstream>
#include <bit>
#include <iostream>
#include <sstream>
#include <map>
#include <chrono>
using namespace opensim;
using namespace services;
namespace {
std::string key(int n) {
    return n == 7 ? framework::Uuid{}.str() : "key" + std::to_string(n);
}
std::string show(const std::shared_ptr<framework::AssetBase>& a) {
    if (!a)
        return "-";
    std::string result = a->name().value_or("") + ":";
    for (auto b : *a->data) {
        result += "0123456789abcdef"[b >> 4];
        result += "0123456789abcdef"[b & 15];
    }
    return result;
}
struct Probe {
    std::int64_t now = 0;
    int serial = 0;
    std::unique_ptr<FlotsamMemoryCache> cache;
    std::map<int, std::shared_ptr<framework::AssetBase>> held;
    std::string run(const std::string& line) {
        std::istringstream input(line);
        std::string op, result;
        int n = 0;
        input >> op;
        if (op == "init") {
            int memory, negative, negms;
            double seconds;
            input >> memory >> negative >> seconds >> negms;
            cache.reset();
            now = 0;
            serial = 0;
            held.clear();
            cache = std::make_unique<FlotsamMemoryCache>(
                FlotsamMemoryCache::Options{memory != 0, negative != 0, seconds, negms},
                [this] { return now; }, false);
            result = "init";
        } else if (op == "advance") {
            std::int64_t delta;
            input >> delta;
            now += delta;
            cache->pump();
            result = "advance";
        } else {
            input >> n;
            std::shared_ptr<framework::AssetBase> a;
            if (op == "cache") {
                a = std::make_shared<framework::AssetBase>(std::optional<std::string>(key(n)),
                                                           "asset" + std::to_string(++serial), std::int8_t(0),
                                                           "creator");
                a->data = std::make_shared<std::vector<std::uint8_t>>(
                    std::initializer_list<std::uint8_t>{static_cast<std::uint8_t>(serial), 0, 255});
                held[n] = a;
                cache->cache(a);
                result = "cache";
            } else if (op == "null") {
                cache->cache({});
                result = "null";
            } else if (op == "drop") {
                held.erase(n);
                result = "drop";
            } else if (op == "mutate") {
                auto it = held.find(n);
                if (it != held.end()) {
                    it->second->name(it->second->name().value_or("") + "X");
                    ++it->second->data->at(0);
                }
                result = "mutate";
            } else if (op == "negative") {
                cache->cache_negative(key(n));
                result = "negative";
            } else if (op == "get" || op == "from") {
                auto ok = op == "get" ? cache->get(key(n), a) : cache->get_from_memory(key(n), a);
                result = std::string(ok ? "1:" : "0:") + show(a);
            } else if (op == "cached")
                result = show(cache->get_cached(key(n)));
            else if (op == "check")
                result = cache->check(key(n)) ? "1" : "0";
            else if (op == "expire") {
                cache->expire(key(n));
                result = "expire";
            } else if (op == "memoryclear") {
                cache->clear_memory();
                result = "memoryclear";
            } else if (op == "negativeclear") {
                cache->clear_negatives();
                result = "negativeclear";
            } else if (op == "report") {
                auto lines = cache->hit_report();
                for (auto& line : lines) {
                    if (!result.empty())
                        result += ';';
                    result += line;
                }
            } else if (op == "clear") {
                cache->clear();
                result = "clear";
            } else
                throw std::runtime_error("Unknown cache operation");
        }
        auto stats = cache->statistics();
        return result + "|" + std::to_string(stats.requests) + "," + std::to_string(stats.memory_hits) + "," +
               std::to_string(stats.weak_hits);
    }
};
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--report-probe") {
            FlotsamMemoryCache::Status s;
            int memory;
            while (std::cin >> s.hits.requests >> s.hits.memory_hits >> s.hits.weak_hits >> s.files.hits >>
                   s.files.in_progress >> s.weak_entries >> s.weak_alive >> memory) {
                auto lines = FlotsamMemoryCache::hit_report(s, memory != 0);
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    if (i)
                        std::cout << ';';
                    std::cout << lines[i];
                }
                std::cout << '\n';
            }
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--config-probe") {
            auto directory = std::filesystem::temp_directory_path() /
                             ("opensim-cache-config-" + framework::Uuid::random().str());
            std::filesystem::create_directory(directory);
            struct Cleanup {
                std::filesystem::path path;
                ~Cleanup() {
                    std::error_code ec;
                    std::filesystem::remove_all(path, ec);
                }
            } cleanup{directory};
            auto file = directory / "config.ini";
            std::string seconds, negative;
            while (std::cin >> seconds >> negative) {
                try {
                    std::ofstream out(file);
                    auto db = (directory / "assets.db").generic_u8string();
                    out << "[Modules]\nAssetCaching=FlotsamAssetCache\nAssetServices=LocalAssetServicesConnector\n[AssetCache]\nFileCacheEnabled=false\nMemoryCacheEnabled=true\n";
                    if (seconds != ".")
                        out << "MemoryCacheTimeout=" << seconds << '\n';
                    if (negative != ".")
                        out << "NegativeCacheTimeout=" << negative << '\n';
                    out << "[AssetService]\nLocalServiceModule=OpenSim.Services.AssetService.dll:AssetService\nStorageProvider=OpenSim.Data.SQLite.dll\nConnectionString=URI=file:"
                        << std::string(reinterpret_cast<const char*>(db.data()), db.size())
                        << ";version=3;Pooling=false\nAssetLoaderEnabled=false\n";
                    out.close();
                    auto events = std::make_shared<SceneCapabilityEvents>(SceneCapabilityEvents::Error{});
                    RegionAssetRuntime assets(server::IniConfig::load(file), events);
                    auto cache = std::dynamic_pointer_cast<FlotsamMemoryCache>(assets.asset_cache());
                    if (!cache)
                        throw std::runtime_error("No real configured cache");
                    auto settings = cache->settings();
                    std::cout << std::bit_cast<std::uint64_t>(settings.memory_seconds) << '|'
                              << settings.negative_milliseconds << '\n';
                    assets.stop();
                } catch (...) {
                    std::cout << "ERR\n";
                }
            }
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            Probe p;
            std::string s;
            while (std::getline(std::cin, s))
                std::cout << p.run(s) << '\n';
            return 0;
        }
        {
            double tick = 10;
            std::vector<double> reads;
            auto clock = [&] {
                reads.push_back(tick);
                return tick++;
            };
            FlotsamMemoryCache ordered({true, true, .5, 120}, clock, false);
            auto states = ordered.containers();
            if (states->negative.load()->epoch() != 10 || states->memory.load()->epoch() != 11 ||
                reads != std::vector<double>{10, 11})
                throw std::runtime_error("Actual source negative-before-memory constructor clock order");
            auto initialized = std::make_shared<FlotsamMemoryCache::Containers>();
            initialized->make_key = [&](auto n) {
                return std::make_shared<FlotsamKeyEntries>(n, clock, false);
            };
            initialized->make_memory = [&](auto n) {
                return std::make_shared<FlotsamAssetEntries>(n, clock, false);
            };
            tick = 100;
            initialized->negative = initialized->make_key(2000);
            tick = 900;
            initialized->memory = initialized->make_memory(500);
            auto negative = initialized->negative.load();
            auto memory = initialized->memory.load();
            auto calls = reads.size();
            FlotsamMemoryCache adopted({true, true, .5, 120}, initialized);
            if (reads.size() != calls || adopted.containers()->negative.load() != negative ||
                adopted.containers()->memory.load() != memory)
                throw std::runtime_error("Wrapper must adopt exact initialized objects without clock reads");
            auto asset = std::make_shared<framework::AssetBase>(framework::Uuid::random(), "adopted",
                                                                std::int8_t(0), "");
            adopted.cache(asset);
            adopted.cache_negative("missing");
            tick = 2000;
            adopted.clear_memory();
            adopted.clear_negatives();
            if (memory->epoch() != 900 || negative->epoch() != 100 || memory->count() || negative->count())
                throw std::runtime_error("Console clears must preserve adopted object epochs");
            int memory_published = 0, key_published = 0;
            initialized->publish_memory = [&](auto p) {
                ++memory_published;
                if (p != initialized->memory.load())
                    throw std::runtime_error("Memory publication before slot assignment");
            };
            initialized->publish_key = [&](auto p) {
                ++key_published;
                if (p != initialized->negative.load())
                    throw std::runtime_error("Key publication before slot assignment");
            };
            tick = 5000;
            adopted.clear();
            if (initialized->memory.load() == memory || initialized->negative.load() == negative ||
                memory_published != 1 || key_published != 1 || initialized->memory.load()->epoch() != 5000 ||
                initialized->negative.load()->epoch() != 5001)
                throw std::runtime_error(
                    "Data clear must recreate memory then negative with fresh epochs and publication");
            bool disposed = false;
            try {
                negative->contains("missing");
            } catch (const FlotsamEntriesDisposed&) {
                disposed = true;
            }
            if (!disposed)
                throw std::runtime_error("Replaced negative container was not disposed");
        }
        Probe p;
        p.run("init 1 1 0.5 120");
        p.run("cache 1");
        p.run("drop 1");
        if (p.run("get 1").find("1:asset1:") != 0)
            throw std::runtime_error("Strong memory retention");
        p.run("negative 1");
        if (p.run("get 1").find("0:-") != 0 || p.run("cached 1").find("asset1:") != 0)
            throw std::runtime_error("Source negative cache getters");
        p.run("advance 2000");
        if (p.run("get 1").find("0:-") != 0)
            throw std::runtime_error("Strict expiry boundary");
        p.run("advance 2000");
        if (p.run("get 1").find("1:-") != 0)
            throw std::runtime_error("Periodic expiry");
        auto asset = std::make_shared<framework::AssetBase>(framework::Uuid::random(), "background",
                                                            std::int8_t(0), "creator");
        auto id = asset->id();
        std::weak_ptr<framework::AssetBase> weak = asset;
        FlotsamMemoryCache real({true, true, .001, 120});
        real.cache(asset);
        asset.reset();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!weak.expired() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!weak.expired())
            throw std::runtime_error("Actual periodic worker expiry");
        FlotsamMemoryCache::Status snapshot;
        snapshot.hits.requests = 32;
        snapshot.hits.weak_hits = 1;
        if (FlotsamMemoryCache::hit_report(snapshot, false)[1] !=
            "unCollected Hit Rate: 3.13% (0 entries 0 alive)")
            throw std::runtime_error("Source midpoint rate formatting");
        auto before = real.status();
        real.clear_memory();
        if (real.status().memory_entries || real.status().weak_entries != before.weak_entries)
            throw std::runtime_error("Memory-only clear changes weak dictionary");
        real.cache_negative("negative");
        if (real.clear_negatives() != 1 || real.status().negative_entries)
            throw std::runtime_error("Negative-only clear count");
        std::cout << "PASS: memory/negative cache semantics and real worker expiry\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
