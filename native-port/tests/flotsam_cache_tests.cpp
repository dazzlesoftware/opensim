// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_cache_files.hpp"
#include "opensim/services/flotsam_file_access.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
using namespace opensim;
using namespace services;
using Asset = FlotsamMemoryCache::Asset;
using Path = std::filesystem::path;
namespace {
std::string key(int n) {
    return n == 7 ? framework::Uuid{}.str() : "key" + std::to_string(n);
}
std::string label(const std::string& k) {
    framework::Uuid id;
    return framework::try_parse_sdk_uuid(k, id) && !id.is_zero() ? "R" : k;
}
std::string group(const std::string& k) {
    return k == key(7) ? "root" : k == key(0) || k == key(2) || k == key(4) || k == key(6) ? "a" : "b";
}
std::string bytes(const framework::AssetBytes& data) {
    if (!data)
        return "-";
    std::string s;
    for (auto b : *data) {
        s += "0123456789abcdef"[b >> 4];
        s += "0123456789abcdef"[b & 15];
    }
    return s;
}
std::string show(const Asset& a) {
    return a ? label(a->id()) + ":" + a->name().value_or("") + ":" + bytes(a->data) : "-";
}
Asset clone(const Asset& a) {
    auto copy = std::make_shared<framework::AssetBase>(std::optional<std::string>(a->id()), a->name(),
                                                       a->type(), a->creator_id());
    copy->data = a->data ? std::make_shared<std::vector<std::uint8_t>>(*a->data) : nullptr;
    return copy;
}
struct Probe {
    double now = 0;
    int serial = 0, point = 0;
    bool rename = false, root_exists = true;
    std::map<std::string, Asset> disk;
    std::map<int, Asset> held;
    std::vector<std::string> trace;
    std::unique_ptr<FlotsamMemoryCache> cache;
    void add(const std::string& s) {
        trace.push_back(s);
    }
    void fail(int p) {
        if (point == p || (point == 11 && p == 5))
            throw std::runtime_error("fault");
    }
    Asset make(int n) {
        auto a = std::make_shared<framework::AssetBase>(std::optional<std::string>(key(n)),
                                                        "asset" + std::to_string(++serial), std::int8_t(0),
                                                        "creator");
        a->data = std::make_shared<std::vector<std::uint8_t>>(
            std::initializer_list<std::uint8_t>{static_cast<std::uint8_t>(serial), 0, 255});
        return a;
    }
    std::shared_ptr<FlotsamMemoryCache::FileOperations> operations() {
        auto backend = native_flotsam_file_backend({});
        backend->directory_exists = [this](auto&) {
            add("E");
            fail(6);
            return root_exists;
        };
        backend->list_directories = [this](auto& root) {
            add("D");
            fail(7);
            return std::vector<Path>{root / "a", root / "b"};
        };
        backend->remove_directory_tree = [this](auto& dir) {
            auto name = dir.filename().string();
            add("R:" + name);
            fail(name == "a" ? 8 : 9);
            for (auto it = disk.begin(); it != disk.end();)
                if (group(it->first) == name)
                    it = disk.erase(it);
                else
                    ++it;
        };
        auto store = std::make_shared<FlotsamFileStore>(FlotsamFileStore::Options{Path("cache"), 1, 3, false},
                                                        backend, std::make_shared<FlotsamFileState>());
        auto files = flotsam_cache_files(
            store, backend,
            [this](auto& s) {
                add("L:" + s);
                fail(10);
            },
            [this](auto& s) {
                add("W:" + s);
                fail(11);
            });
        files->cache = [this](auto& id, auto& a, bool replace) {
            add("F:" + label(id) + ":" + (replace ? "1" : "0"));
            fail(1);
            disk[id] = clone(a);
            if (rename)
                a->id(key(6));
        };
        files->read = [this](auto& id) {
            add("G:" + label(id));
            fail(3);
            auto it = disk.find(id);
            return it == disk.end() ? Asset{} : clone(it->second);
        };
        files->check = [this](auto& id) {
            add("H:" + label(id));
            try {
                fail(4);
                return disk.contains(id);
            } catch (...) {
                return false;
            }
        };
        files->touch = [this](auto& id) {
            add("P:" + label(id));
            fail(2);
            add("T:" + label(id));
        };
        files->expire = [this](auto& id) {
            add("P:" + label(id));
            fail(2);
            add("X:" + label(id));
            fail(5);
            disk.erase(id);
        };
        return files;
    }
    std::string execute(const std::string& line) {
        std::istringstream in(line);
        std::string op;
        in >> op;
        int n = 0;
        if (op != "init" && op != "advance" && op != "jump")
            in >> n;
        Asset a;
        if (op == "init") {
            int memory, negative, file, touch, negms, log;
            double seconds;
            in >> memory >> negative >> file >> touch >> seconds >> negms >> log >> now;
            cache.reset();
            serial = point = 0;
            held.clear();
            disk.clear();
            rename = false;
            root_exists = true;
            cache = std::make_unique<FlotsamMemoryCache>(
                FlotsamMemoryCache::Options{memory != 0, negative != 0, seconds, negms, file != 0, touch != 0,
                                            log},
                [this] { return now; }, false, operations());
            return "init";
        }
        if (op == "fault") {
            point = n;
            return "fault";
        }
        if (op == "rename") {
            rename = n != 0;
            return "rename";
        }
        if (op == "root") {
            root_exists = n != 0;
            return "root";
        }
        if (op == "seed") {
            disk[key(n)] = make(n);
            return "seed";
        }
        if (op == "cache" || op == "replace") {
            a = make(n);
            held[n] = a;
            cache->cache(a, op == "replace");
            return "cache";
        }
        if (op == "null") {
            cache->cache({});
            return "null";
        }
        if (op == "drop") {
            held.erase(n);
            return "drop";
        }
        if (op == "mutate") {
            auto it = held.find(n);
            if (it != held.end()) {
                it->second->name(it->second->name().value_or("") + "X");
                if (it->second->data && !it->second->data->empty())
                    ++it->second->data->at(0);
            }
            return "mutate";
        }
        if (op == "negative") {
            cache->cache_negative(key(n));
            return "negative";
        }
        if (op == "get" || op == "from") {
            try {
                bool ok = op == "get" ? cache->get(key(n), a) : cache->get_from_memory(key(n), a);
                return std::string(ok ? "1:" : "0:") + show(a);
            } catch (...) {
                return "EX:" + show(a);
            }
        }
        if (op == "asset")
            return show(cache->get_asset(key(n)));
        if (op == "cached")
            return show(cache->get_cached(key(n)));
        if (op == "check")
            return cache->check(key(n)) ? "1" : "0";
        if (op == "meta" || op == "meta-mutate") {
            auto m = cache->get_metadata(key(n));
            if (m && op == "meta-mutate")
                m->name = m->name.value_or("") + "M";
            return m ? m->name.value_or("-") : "-";
        }
        if (op == "data" || op == "data-mutate") {
            auto data = cache->get_data(key(n));
            if (data && !data->empty() && op == "data-mutate")
                ++data->at(0);
            return bytes(data);
        }
        if (op == "async" || op == "async-null") {
            AssetService::Retrieved callback;
            if (op == "async")
                callback = [this](auto& id, std::any sender, Asset asset) {
                    add("C:" + label(id) + ":" + std::to_string(std::any_cast<int>(sender)) + ":" +
                        show(asset));
                    fail(12);
                };
            return cache->get_async(key(n), 63, callback) ? "1" : "0";
        }
        if (op == "foreign")
            return show(cache->get_foreign(key(n), "foreign", true));
        if (op == "foreign-async") {
            cache->get(key(n), "foreign", true, [this](auto) { add("BAD"); });
            return "foreign";
        }
        if (op == "exists") {
            std::string s;
            for (bool v : cache->assets_exist({key(n), key(7), key(0)}))
                s += v ? '1' : '0';
            return s;
        }
        if (op == "store") {
            auto it = held.find(n);
            if (it == held.end())
                it = held.emplace(n, make(n)).first;
            a = it->second;
            auto id = cache->store(a);
            return "store:" + std::string(id == a->id() ? "1" : "0") + ":" +
                   (a->full_id() != framework::Uuid{} ? "1" : "0");
        }
        if (op == "store-null") {
            cache->store({});
            return "unexpected";
        }
        if (op == "update" || op == "update-null")
            return cache->update_content(key(n), op == "update"
                                                     ? std::make_shared<std::vector<std::uint8_t>>(
                                                           std::initializer_list<std::uint8_t>{9, 8, 7})
                                                     : nullptr)
                       ? "1"
                       : "0";
        if (op == "delete")
            return cache->erase(key(n)) ? "1" : "0";
        if (op == "expire") {
            cache->expire(key(n));
            return "expire";
        }
        if (op == "clear") {
            cache->clear();
            return "clear";
        }
        if (op == "advance" || op == "jump") {
            double amount;
            in >> amount;
            now += amount;
            if (op == "advance")
                cache->pump();
            return op == "advance" ? "advance" : "jump";
        }
        throw std::runtime_error("Unknown combined cache operation");
    }
    std::string run(const std::string& line) {
        trace.clear();
        std::string result;
        try {
            result = execute(line);
        } catch (...) {
            result = "EX";
        }
        auto stats = cache->statistics();
        std::string t;
        for (auto& s : trace) {
            if (!t.empty())
                t += ';';
            t += s;
        }
        std::vector<std::string> values;
        for (auto& [id, a] : disk)
            values.push_back(show(a));
        std::sort(values.begin(), values.end());
        std::string d;
        for (auto& s : values) {
            if (!d.empty())
                d += ',';
            d += s;
        }
        return result + "|" + t + "|" + std::to_string(stats.requests) + "," +
               std::to_string(stats.memory_hits) + "," + std::to_string(stats.weak_hits) + "|" + d;
    }
};
void actual() {
    auto root = std::filesystem::temp_directory_path() /
                ("opensim-composed-cache-" + framework::Uuid::random().str());
    std::filesystem::create_directory(root);
    struct Cleanup {
        Path p;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(p, e);
        }
    } cleanup{root};
    auto jobs = std::make_shared<framework::ObjectJobEngine>(
        [](std::any job) { std::any_cast<std::function<void()>>(job)(); }, "ComposedFlotsamCache", 1000, 1);
    struct StopJobs {
        std::shared_ptr<framework::ObjectJobEngine> q;
        ~StopJobs() {
            q->dispose();
            while (q->active_threads())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } stop{jobs};
    auto backend = native_flotsam_file_backend(jobs);
    auto access =
        std::make_shared<FlotsamFileAccess>(flotsam_access_entries(), native_flotsam_set_access_time);
    attach_flotsam_file_access(backend, access);
    auto store = std::make_shared<FlotsamFileStore>(FlotsamFileStore::Options{root, 1, 3, true}, backend,
                                                    std::make_shared<FlotsamFileState>());
    auto files = flotsam_cache_files(store, backend);
    FlotsamMemoryCache cache({false, true, .016, 120000, true, true, 2}, {}, true, files);
    auto a = std::make_shared<framework::AssetBase>(framework::Uuid::random(), "Disk roundtrip",
                                                    std::int8_t(7), "creator");
    a->data = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{1, 0, 255});
    auto id = a->id();
    auto wait = [&](const std::string& key) {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (store->writing(key) && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (store->writing(key))
            throw std::runtime_error("Combined file write timeout");
    };
    cache.cache(a);
    wait(id);
    std::weak_ptr<framework::AssetBase> original = a;
    a.reset();
    auto release = [&](std::weak_ptr<framework::AssetBase> weak) {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!weak.expired() && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!weak.expired())
            throw std::runtime_error("Queued asset was not released");
    };
    release(original);
    Asset value;
    if (!cache.get_from_memory(id, value) || value)
        throw std::runtime_error("Memory getter unexpectedly read disk");
    value = cache.get_asset(id);
    if (!value || value->name() != std::optional<std::string>("Disk roundtrip") || !value->data ||
        *value->data != std::vector<std::uint8_t>{1, 0, 255})
        throw std::runtime_error("Combined actual file retrieval");
    cache.cache_negative(id);
    if (cache.get_asset(id) || !cache.get_cached(id))
        throw std::runtime_error("Combined negative cache bypass");
    cache.cache(value);
    wait(id);
    if (!cache.update_content(
            id, std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{9, 8})))
        throw std::runtime_error("Combined content update");
    wait(id);
    std::weak_ptr<framework::AssetBase> updated = value;
    value.reset();
    release(updated);
    if (*cache.get_data(id) != std::vector<std::uint8_t>{9, 8})
        throw std::runtime_error("Replaced disk data");
    if (!cache.erase(id) || store->check(id))
        throw std::runtime_error("Combined expire file removal");
    {
        std::ofstream file(root / "root-file");
        file << "keep";
    }
    std::filesystem::create_directories(root / "a" / "nested");
    {
        std::ofstream file(root / "a" / "nested" / "asset");
        file << "remove";
    }
    cache.clear();
    if (!std::filesystem::exists(root / "root-file") || std::filesystem::exists(root / "a"))
        throw std::runtime_error("Clear must remove only subdirectories");
    bool called = false;
    cache.get("absent", "foreign", true, [&](auto) { called = true; });
    if (called || cache.get_foreign("absent", "foreign", true))
        throw std::runtime_error("Foreign source no-op methods");
    bool miss_called = false;
    if (!cache.get_async("absent", 63,
                         [&](auto&, std::any sender, auto asset) {
                             miss_called = std::any_cast<int>(sender) == 63 && !asset;
                         }) ||
        !miss_called)
        throw std::runtime_error("Ordinary miss must invoke original callback");
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            Probe p;
            std::string line;
            while (std::getline(std::cin, line))
                std::cout << p.run(line) << '\n';
        } else {
            actual();
            std::cout
                << "PASS: composed cache actual queue/file retrieval, replacement, expiration, directory clearing and source service behavior\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
