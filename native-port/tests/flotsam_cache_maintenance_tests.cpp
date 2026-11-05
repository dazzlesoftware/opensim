// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_cache_maintenance.hpp"
#include "opensim/services/flotsam_memory_cache.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/services/local_asset_connector.hpp"
#include "opensim/services/flotsam_cache_files.hpp"
#include "opensim/services/asset_store.hpp"
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#include <sstream>
#include <iomanip>
using namespace opensim;
using namespace services;
namespace {
framework::Uuid id(int n) {
    std::ostringstream s;
    s << "00000000-0000-0000-0000-" << std::hex << std::setfill('0') << std::setw(12) << n;
    return framework::Uuid(s.str());
}
int number(framework::Uuid id) {
    return std::stoi(id.str().substr(24), nullptr, 16);
}
std::string hex(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
struct Probe {
    int profile = 0, point = 0, nth = 1, seen = 0, cancel = 0, cancel_nth = 1, cancel_seen = 0, mutation = 0,
        mut_point = 0, mut_nth = 1, mut_seen = 0;
    bool exists = false, running = true;
    std::string text;
    std::int64_t ticks = 0;
    std::vector<std::string> trace;
    std::map<std::string, std::vector<std::filesystem::path>> dirs;
    std::map<std::string, int> counts;
    std::shared_ptr<scene::UuidGathererState> state = std::make_shared<scene::UuidGathererState>();
    void log(const std::string& s) {
        trace.push_back(s);
    }
    void hook(int p) {
        if (p == cancel && ++cancel_seen == cancel_nth)
            running = false;
        if (p == mut_point && ++mut_seen == mut_nth) {
            switch (mutation) {
            case 1:
                state->gathered.clear();
                break;
            case 2:
                state->gathered.erase(id(2));
                break;
            case 3:
                state->gathered[id(90)] = 0;
                break;
            case 4:
                if (state->gathered.contains(id(3)))
                    state->gathered[id(3)] = 1;
                break;
            }
        }
        if (p == point && ++seen == nth)
            throw std::runtime_error("fault");
    }
    std::shared_ptr<FlotsamMaintenanceBackend> backend() {
        auto b = std::make_shared<FlotsamMaintenanceBackend>();
        b->combine = [this](auto& root, auto& name) {
            log("P");
            hook(1);
            return root / std::filesystem::path(name);
        };
        b->file_exists = [this](auto&) {
            log("E");
            hook(2);
            if (profile == 8)
                throw std::runtime_error("operation");
            return exists;
        };
        b->now_utc_ticks = [this] {
            log("N");
            hook(3);
            return 638712864000000000ll;
        };
        b->set_write_utc = [this](auto&, auto value) {
            log("T:" + std::to_string(value));
            hook(4);
            ticks = value;
        };
        b->write_text = [this](auto&, auto& value) {
            log("W:" + value);
            if (profile == 2) {
                exists = true;
                text = "partial";
            }
            hook(5);
            exists = true;
            text = value;
        };
        b->directories = [this](auto& p) {
            auto path = p.generic_string();
            log("D:" + path);
            hook(7);
            if (profile == 4)
                throw std::runtime_error("missing");
            auto i = dirs.find(path);
            return i == dirs.end() ? std::vector<std::filesystem::path>{} : i->second;
        };
        b->file_count = [this](auto& p) {
            auto path = p.generic_string();
            log("F:" + path);
            hook(8);
            if (profile == 5 && path == "cache")
                throw std::runtime_error("files");
            return counts[path];
        };
        b->sleep = [this](int n) {
            log("S:" + std::to_string(n));
            hook(9);
        };
        b->info = [this](auto& s) {
            log("I:" + s);
            hook(10);
        };
        b->warn = [this](auto& s) {
            log("L:" + s);
            hook(6);
        };
        b->gather = [this] {
            log("Q");
            hook(11);
            return profile == 3 ? std::shared_ptr<scene::UuidGathererState>{} : state;
        };
        b->running = [this] {
            log("R");
            hook(12);
            return running;
        };
        b->file_name = [this](auto& key) {
            auto n = number(framework::Uuid(key));
            log("K:" + std::to_string(n));
            hook(13);
            return std::filesystem::path("file" + std::to_string(n));
        };
        b->check_access = [this](auto& p) {
            auto name = p.string();
            log("A:" + name);
            hook(14);
            return profile == 5 ? false : profile == 6 ? std::stoi(name.substr(4)) % 2 == 0 : true;
        };
        b->directory_exists = [this](auto&) {
            log("DE");
            hook(16);
            return profile != 4;
        };
        b->files = [this](auto& p) {
            log("F:" + p.generic_string());
            hook(8);
            if (profile == 5 && p.generic_string() == "cache")
                throw std::runtime_error("files");
            return std::vector<std::filesystem::path>{"cache/one", "cache/two", "cache/status.fac"};
        };
        b->remove_directory = [this](auto& p) {
            log("DD:" + p.generic_string());
            hook(17);
        };
        b->remove_file = [this](auto& p) {
            log("DF:" + p.generic_string());
            hook(18);
        };
        b->get_asset = [this](auto& key) {
            log("G:" + std::to_string(number(framework::Uuid(key))));
            hook(15);
        };
        return b;
    }
    std::string run(const std::string& line) {
        std::istringstream in(line);
        int op, get, initial;
        in >> op >> profile >> point >> nth >> cancel >> cancel_nth >> mutation >> mut_point >> mut_nth >>
            get >> initial;
        running = initial != 0;
        exists = profile % 2 != 0;
        text = exists ? "old" : "";
        if (profile == 1) {
            for (int n = 0; n < 55; ++n) {
                auto d = "cache/d" + std::to_string(n);
                dirs["cache"].emplace_back(d);
                counts[d] = 1;
            }
        } else if (profile == 2) {
            dirs["cache"] = {"cache/a"};
            dirs["cache/a"] = {"cache/a/a"};
            counts["cache/a/a"] = 3;
        } else {
            dirs["cache"] = {"cache/a", "cache/b"};
            counts["cache"] = profile == 3 ? INT32_MAX : 3;
            counts["cache/a"] = 2;
            counts["cache/b"] = 3;
        }
        if (profile == 1)
            for (int n = 1; n <= 55; ++n)
                state->gathered[id(n)] = 0;
        else
            for (int n : {3, 1, 2})
                state->gathered[id(n)] = 0;
        if (profile == 2)
            state->gathered.clear();
        if (profile == 4) {
            state->gathered.erase(id(1));
            state->gathered[id(4)] = 0;
        }
        FlotsamCacheMaintenance cache("cache", backend());
        std::string result;
        try {
            if (op == 0) {
                cache.stamp(id(1));
                result = "OK";
            } else if (op == 1)
                result = std::to_string(cache.count("cache"));
            else if (op == 2)
                result = std::to_string(cache.touch(get != 0));
            else {
                cache.clear_files();
                result = "OK";
            }
        } catch (...) {
            result = "EX";
        }
        std::string events;
        for (auto& event : trace) {
            if (!events.empty())
                events += ';';
            events += event;
        }
        std::string output = result + "|" + hex(events) + "|" + (running ? "1" : "0") + ":" +
                             (exists ? "1" : "0") + ":" + std::to_string(ticks) + ":" + hex(text) + "|";
        bool first = true;
        for (auto& [key, value] : state->gathered) {
            if (!first)
                output += ',';
            first = false;
            output += std::to_string(number(key)) + ":" + std::to_string(value);
        }
        return output;
    }
};
struct ConnectorBackend : AssetConnectorBackend {
    std::shared_ptr<AssetService> service;
    std::shared_ptr<AssetService> load_service(const std::string&, const server::IniConfig*) override {
        return service;
    }
    void error(const std::string& text) override {
        throw std::runtime_error(text);
    }
    void info(const std::string&) override {
    }
    void debug(const std::string&) override {
    }
    void post(std::function<void()> call, const std::string&) override {
        call();
    }
};
struct ConnectorScene : AssetConnectorScene {
    std::shared_ptr<framework::IAssetCache> cache;
    void register_service(std::shared_ptr<AssetService>) override {
    }
    AssetConnectorCache asset_cache() override {
        return {cache, true};
    }
    std::optional<std::string> region_name() override {
        return "maintenance";
    }
};
void integration() {
    auto parent = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    auto root = parent / ("opensim-maintenance-" + framework::Uuid::random().str());
    std::filesystem::create_directory(root);
    {
        auto worker = std::make_shared<framework::ObjectJobEngine>(
            [](std::any job) { std::any_cast<std::function<void()>>(std::move(job))(); }, "NativeMaintenance",
            1000, 1);
        auto io = native_flotsam_file_backend(worker);
        auto entries = flotsam_access_entries([] { return 0.; }, false);
        auto access = std::make_shared<FlotsamFileAccess>(entries, native_flotsam_set_access_time);
        attach_flotsam_file_access(io, access);
        auto store =
            std::make_shared<FlotsamFileStore>(FlotsamFileStore::Options{root / "cache", 1, 3, true}, io);
        auto files = flotsam_cache_files(store, io);
        auto cache = std::make_shared<FlotsamMemoryCache>(
            FlotsamMemoryCache::Options{false, true, .016, 120000, true, true, 0}, [] { return 0.; }, false,
            files);
        auto db = std::make_shared<SqliteAssetStore>(framework::utf8((root / "assets.db").u16string()), true);
        auto asset = std::make_shared<framework::AssetBase>(id(1), "maintenance asset", std::int8_t(0), "");
        asset->data =
            std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{0, 1, 255});
        if (!db->store(asset))
            throw std::runtime_error("SQLite seed");
        auto service = std::make_shared<StoredAssetService>(db);
        auto relay = std::make_shared<ConnectorBackend>();
        relay->service = service;
        auto connector = std::make_shared<LocalAssetConnector>(relay);
        auto config_file = root / "config.ini";
        {
            std::ofstream out(config_file);
            out << "[Modules]\nAssetServices = LocalAssetServicesConnector\n[AssetService]\nLocalServiceModule = native-test\n";
        }
        auto config = server::IniConfig::load(config_file);
        connector->initialise(&config);
        auto scene = std::make_shared<ConnectorScene>();
        scene->cache = cache;
        connector->add_region(scene);
        connector->region_loaded(scene);
        auto refs = std::make_shared<opensim::scene::UuidGathererState>();
        refs->gathered[id(1)] = 0;
        int gets = 0;
        auto backend = FlotsamCacheMaintenance::native_backend(
            store, access, connector, [refs] { return refs; }, [] { return true; }, [](auto&, auto&) {});
        auto get = backend->get_asset;
        backend->get_asset = [&](auto& key) {
            ++gets;
            get(key);
        };
        FlotsamCacheMaintenance maintenance(root / "cache", backend);
        auto file = store->file_name(id(1).str());
        std::filesystem::create_directories(file.parent_path());
        if (maintenance.touch(false) != 1 || gets || std::filesystem::exists(file))
            throw std::runtime_error("touch(false) fetched missing asset");
        if (maintenance.touch(true) != 1 || gets != 1)
            throw std::runtime_error("touch(true) did not retrieve through actual connector");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (store->writing(id(1).str())) {
            if (std::chrono::steady_clock::now() > deadline)
                throw std::runtime_error("maintenance queued cache write timeout");
            std::this_thread::yield();
        }
        worker->dispose();
        auto loaded = store->get(id(1).str());
        if (!loaded || !loaded->data || *loaded->data != *asset->data || loaded->name() != asset->name())
            throw std::runtime_error("maintenance refetch did not populate actual NRBF file cache");
        db->erase(id(1).str());
        if (maintenance.touch(true) != 1 || gets != 1 || !std::filesystem::exists(file))
            throw std::runtime_error("existing file incorrectly fetched deleted SQL row");
        maintenance.stamp(id(2));
        if (maintenance.count(root / "cache") != 2)
            throw std::runtime_error("count omitted asset/status file");
        maintenance.clear_files();
        if (!std::filesystem::is_directory(root / "cache") || maintenance.count(root / "cache") != 0 ||
            std::filesystem::exists(file))
            throw std::runtime_error("console clear did not remove root status and tiered files");
    }
    auto verified = std::filesystem::weakly_canonical(root);
    if (verified.parent_path() != parent || !verified.filename().string().starts_with("opensim-maintenance-"))
        throw std::runtime_error("Unexpected integration cleanup target");
    std::filesystem::remove_all(verified);
}

void filesystem(const std::string& encoded, bool locked) {
    std::string raw;
    for (std::size_t i = 0; i < encoded.size(); i += 2)
        raw += static_cast<char>(std::stoi(encoded.substr(i, 2), nullptr, 16));
    auto root = std::filesystem::path(framework::utf16(raw));
    auto io = native_flotsam_file_backend({});
    auto store = std::make_shared<FlotsamFileStore>(FlotsamFileStore::Options{root, 1, 3, false}, io);
    auto access = std::make_shared<FlotsamFileAccess>(nullptr, native_flotsam_set_access_time);
    auto service =
        std::make_shared<FlotsamMemoryCache>(FlotsamMemoryCache::Options{}, [] { return 0.; }, false);
    std::vector<std::string> warnings;
    auto backend = FlotsamCacheMaintenance::native_backend(
        store, access, service, [] { return std::make_shared<scene::UuidGathererState>(); },
        [] { return true; },
        [&](auto& level, auto& message) {
            if (level == "warn")
                warnings.push_back("L:" + message);
        });
    backend->now_utc_ticks = [] { return 638712864000000000ll; };
    backend->sleep = [](int) {};
    FlotsamCacheMaintenance cache(root, backend);
    auto status = root / ("RegionStatus_" + id(1).str() + ".fac");
#ifdef _WIN32
    HANDLE blocker = INVALID_HANDLE_VALUE;
    if (locked) {
        blocker = CreateFileW(status.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
        if (blocker == INVALID_HANDLE_VALUE)
            throw std::runtime_error("status blocker");
    }
#endif
    cache.stamp(id(1));
    cache.stamp(id(1));
#ifdef _WIN32
    if (blocker != INVALID_HANDLE_VALUE)
        CloseHandle(blocker);
#endif
    bool exists = io->file_exists(status);
    std::int64_t ticks = 0;
    std::string data = "-";
    if (exists) {
#ifdef _WIN32
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!GetFileAttributesExW(status.c_str(), GetFileExInfoStandard, &info))
            throw std::runtime_error("status metadata");
        ticks = 504911232000000000ll +
                static_cast<std::int64_t>((std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) |
                                          info.ftLastWriteTime.dwLowDateTime);
#else
        struct stat st{};
        if (::stat(status.c_str(), &st) < 0)
            throw std::runtime_error("status metadata");
#ifdef __APPLE__
        ticks = 621355968000000000ll + st.st_mtimespec.tv_sec * 10000000ll + st.st_mtimespec.tv_nsec / 100;
#else
        ticks = 621355968000000000ll + st.st_mtim.tv_sec * 10000000ll + st.st_mtim.tv_nsec / 100;
#endif
#endif
        std::ifstream in(status, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(in)), {});
        data = hex(contents);
    }
    std::string warning;
    for (auto& value : warnings) {
        if (!warning.empty())
            warning += ';';
        warning += value;
    }
    std::cout << (exists ? 1 : 0) << '|' << ticks << '|' << data << '|' << cache.count(root) << '|'
              << hex(warning) << '\n';
}

}
int main(int argc, char** argv) {
    try {
        if (argc > 2 && std::string(argv[1]) == "--fs") {
            filesystem(argv[2], argc > 3 && std::string(argv[3]) == "1");
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            std::string line;
            while (std::getline(std::cin, line)) {
                Probe p;
                std::cout << p.run(line) << '\n';
            }
            return 0;
        }
        integration();
        Probe p;
        auto line = p.run("2 5 0 1 0 1 1 12 1 1 1");
        if (!line.starts_with("0|"))
            throw std::runtime_error(
                "Clear during running check must retain copied current UUID and return current count");
        std::cout << "PASS: maintenance mutation-safe traversal\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
