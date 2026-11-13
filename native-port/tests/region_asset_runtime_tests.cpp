// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_asset_runtime.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/server/http_route_registry.hpp"
#include "opensim/scene/uuid_gatherer.hpp"
#include <filesystem>
#include <algorithm>
#include <sqlite3.h>
#include <fstream>
#include <iostream>
#include <future>
#include <thread>
#include <utility>
using namespace opensim;
using namespace opensim::services;
namespace {
int checks = 0;
void check(bool value, const char *label) {
    ++checks;
    if (!value)
        throw std::runtime_error(label);
}
std::string text(const std::filesystem::path &path) {
    auto value = path.u8string();
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}
struct TemporaryDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("opensim-region-assets-" + framework::Uuid::random().str());
    TemporaryDirectory() {
        std::filesystem::create_directory(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
struct Backend final : SceneCapabilityBackend {
    std::shared_ptr<server::RegistryCapabilityListener> registry;
    explicit Backend(std::shared_ptr<server::RegistryCapabilityListener> listener)
        : registry(std::move(listener)) {}
    std::string scene_name() const override {
        return "Asset test scene";
    }
    std::optional<std::string> region_name() const override {
        return "Asset test region";
    }
    std::optional<std::string> external_host() const override {
        return "127.0.0.1";
    }
    std::shared_ptr<CapabilityRuntimeListener> listener() const override {
        return registry;
    }
    bool check_client(framework::Uuid, const network::HttpRequest &) override {
        return true;
    }
    void debug(const std::string &) override {}
    void warn(const std::string &) override {}
    void error(const std::string &) override {}
    void console(const std::string &) override {}
    void add_command(CapabilityCommand) override {}
};
server::IniConfig config(const TemporaryDirectory &directory, const std::string &filename, bool connector,
                         const std::string &provider = "OpenSim.Data.SQLite.dll") {
    auto path = directory.path / filename;
    std::ofstream file(path, std::ios::binary);
    file
        << "[Modules]\nAssetServices=" << (connector ? "LocalAssetServicesConnector" : "")
        << "\n[AssetService]\nLocalServiceModule=OpenSim.Services.AssetService.dll:AssetService\nStorageProvider="
        << provider << "\nConnectionString=URI=file:" << text(directory.path / "assets.db")
        << ";version=3;Pooling=false"
        << "\nAssetLoaderEnabled=false\n[ClientStack.LindenCaps]\nCap_GetTexture=localhost\nCap_GetMesh=localhost\nCap_GetMesh2=localhost\nCap_GetAsset=localhost\n";
    file.close();
    if (!file)
        throw std::runtime_error("Cannot write test configuration");
    return server::IniConfig::load(path);
}
void cache_profiles() {
    TemporaryDirectory directory;
    config(directory, "cache.ini", true);
    auto file = directory.path / "cache.ini";
    {
        std::ofstream out(file, std::ios::app);
        out << "\n[Modules]\nAssetServices=LocalAssetServicesConnector\nAssetCaching=FlotsamAssetCache\n[AssetCache]\nFileCacheEnabled=false\nMemoryCacheEnabled=true\nMemoryCacheTimeout=0.25\n";
    }
    auto settings = server::IniConfig::load(file);
    auto local = framework::Uuid::random();
    for (int pass = 0; pass < 2; ++pass) {
        auto registry = std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 19002, 0, {}});
        SceneCapabilities scene(std::make_shared<Backend>(registry));
        RegionAssetRuntime assets(settings, scene.shared_events());
        check(bool(assets.asset_cache()), "selected real memory cache exposed");
        if (pass == 0) {
            auto a = std::make_shared<framework::AssetBase>(local, "local bake", std::int8_t(0), "creator");
            a->local(true);
            a->temporary(true);
            a->data =
                std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{0, 255, 1});
            check(assets.store(a) == local.str() && assets.get(local) == a &&
                      assets.asset_cache()->get_cached(local.str()) == a,
                  "real connector retains local asset in cache without ordinary persistence");
        } else
            check(!assets.get(local),
                  "local cached asset is absent from actual SQLite after cache/runtime restart");
        std::vector<std::string> lines;
        auto output = [&](const std::string &text) { lines.push_back(text); };
        check(assets.cache_console({"fcache", "expire", "invalid-date"}, output) &&
                  lines.back() == "expire is not a valid date & time",
              "production native console rejects invalid dates");
        check(assets.cache_console({"fcache", "expire", "9999-12-31"}, output) &&
                  lines.back() == "expire date & time must be in past",
              "production native console rejects future dates");
        check(assets.cache_console({"fcache", "expire", "2000-01-01"}, output) &&
                  lines.back() == "File cache not active, not clearing.",
              "production native console accepts invariant past date");
        check(assets.cache_console({"fcache", "expire", "now"}, output) &&
                  lines.back() == "Flotsam assets check already running",
              "production source disabled-file gate persists");
        assets.stop();
        scene.shutdown();
    }
    {
        std::ofstream out(file, std::ios::app);
        out << "\n[AssetCache]\nFileCacheEnabled=true\n";
    }
    auto registry = std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 19003, 0, {}});
    SceneCapabilities scene(std::make_shared<Backend>(registry));
    auto before = scene.events().on_register_caps.snapshot().size();
    bool rejected = false;
    try {
        RegionAssetRuntime assets(server::IniConfig::load(file), scene.shared_events());
    } catch (const std::invalid_argument &e) {
        rejected = std::string(e.what()).find("requires live scene asset gathering") != std::string::npos;
    }
    check(rejected && scene.events().on_register_caps.snapshot().size() == before,
          "automatic expiry requires live scene asset protection");
    const auto cache_directory = directory.path / "disk-cache";
    {
        std::ofstream out(file, std::ios::app);
        out << "\n[AssetCache]\nFileCleanupTimer=0\nMemoryCacheEnabled=false\nCacheDirectory="
            << text(cache_directory) << '\n';
    }
    const auto disk_settings = server::IniConfig::load(file);
    const auto disk_file = cache_directory / local.str().substr(0, 3) / local.str();
    {
        RegionAssetRuntime assets(disk_settings, scene.shared_events());
        auto bake = std::make_shared<framework::AssetBase>(local, "disk bake", std::int8_t(0), "creator");
        bake->local(true);
        bake->temporary(true);
        bake->data = std::make_shared<std::vector<std::uint8_t>>(3, 42);
        check(assets.store(bake) == local.str(), "local asset accepted by disk cache");
        for (int attempt = 0; attempt < 200 && !std::filesystem::exists(disk_file); ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(std::filesystem::is_regular_file(disk_file), "Flotsam worker wrote the tiered cache file");
        assets.stop();
    }
    {
        RegionAssetRuntime assets(disk_settings, scene.shared_events());
        const auto bake = assets.get(local);
        check(bake && bake->local() && bake->data && *bake->data == std::vector<std::uint8_t>(3, 42),
              "fresh region asset runtime reads the local asset from the disk cache");
        const auto status_file = cache_directory / ("RegionStatus_" + local.str() + ".fac");
        std::ofstream(status_file) << "Cache status fixture";
        auto status = std::make_shared<std::promise<bool>>();
        auto reported = status->get_future();
        auto output = [status, local, counted = false](const std::string& line) mutable {
            if (line == "[FLOTSAM ASSET CACHE]   File Cache: 2 assets")
                counted = true;
            if (line.starts_with("[FLOTSAM ASSET CACHE] Region: " + local.str() + "."))
                status->set_value(counted);
        };
        check(assets.cache_console({"fcache", "status"}, output) &&
                  reported.wait_for(std::chrono::seconds(2)) == std::future_status::ready && reported.get(),
              "disk status counts cache files and reports the region stamp");
        assets.asset_cache()->expire(local.str());
        check(!std::filesystem::exists(disk_file) && !assets.get(local),
              "explicit asset expiry removes the file and the cached asset");
        std::string cleared;
        check(assets.cache_console({"fcache", "clear", "file"}, [&](const auto& line) { cleared = line; }) &&
                  cleared == "File cache cleared." &&
                  std::filesystem::is_directory(cache_directory) && std::filesystem::is_empty(cache_directory),
              "disk clear removes the cache tier and root region stamp");
        assets.stop();
    }
    {
        std::ofstream out(file, std::ios::app);
        out << "\n[AssetCache]\nFileCacheEnabled=true\nFileCleanupTimer=0.00001\nFileCacheTimeout=0.000001\n"
            << "CacheDirectory=" << text(cache_directory) << '\n';
    }
    const auto stale = framework::Uuid::random().str();
    const auto stale_file = cache_directory / stale.substr(0, 3) / stale;
    for (const auto& entry : {disk_file, stale_file}) {
        std::filesystem::create_directories(entry.parent_path());
        std::ofstream(entry) << "cache cleanup fixture";
    }
    {
        std::string cleanup_log;
        std::mutex cleanup_log_mutex;
        RegionAssetRuntime assets(
            server::IniConfig::load(file), scene.shared_events(), {}, {}, local,
            [&](const auto&, const auto& message) {
                std::lock_guard lock(cleanup_log_mutex);
                cleanup_log += message + '\n';
            }, {}, {},
            [local](opensim::scene::UuidGatherer& gatherer) { gatherer.add_gathered(local, 0); });
        for (int attempt = 0; attempt < 200 && std::filesystem::exists(stale_file); ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assets.quiesce_cache_cleanup();
        if (std::filesystem::exists(stale_file) || !std::filesystem::exists(disk_file)) {
            std::lock_guard lock(cleanup_log_mutex);
            std::cerr << cleanup_log;
        }
        check(!std::filesystem::exists(stale_file) && std::filesystem::exists(disk_file),
              "automatic expiry keeps a scene-referenced file and removes an unreferenced file");
        check(std::filesystem::exists(cache_directory / ("RegionStatus_" + local.str() + ".fac")),
              "automatic expiry stamps the scanned region");
        assets.stop();
    }
    {
        const auto referenced = framework::Uuid::random();
        const auto destination = cache_directory / referenced.str().substr(0, 3) / referenced.str();
        std::promise<void> allow_scan;
        auto scan_ready = allow_scan.get_future().share();
        RegionAssetRuntime assets(
            disk_settings, scene.shared_events(), {}, {}, local, {}, {}, {},
            [referenced, scan_ready](opensim::scene::UuidGatherer& gatherer) {
                if (scan_ready.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
                    throw std::runtime_error("Cache scan ran on the console thread");
                gatherer.add_gathered(referenced, 0);
            });
        auto texture = std::make_shared<framework::AssetBase>(referenced, "scene texture", std::int8_t(0), "");
        texture->data = std::make_shared<std::vector<std::uint8_t>>(3, 42);
        check(assets.store(texture) == referenced.str(), "store a persistent scene texture");
        const auto await_file = [&] {
            for (int attempt = 0; attempt < 200 && !std::filesystem::exists(destination); ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return std::filesystem::is_regular_file(destination);
        };
        check(await_file(), "initial scene texture reached disk cache");
        assets.asset_cache()->expire(referenced.str());
        check(!std::filesystem::exists(destination), "prefetch fixture starts with a cache miss");
        auto completion = std::make_shared<std::promise<std::string>>();
        auto result = completion->get_future();
        check(assets.cache_console({"fcache", "assets"}, [completion](const auto& line) {
                  if (line.starts_with("Completed check"))
                      completion->set_value(line);
              }),
              "fcache assets returns while the scan is still pending");
        std::string overlap;
        check(assets.cache_console({"fcache", "assets"}, [&](const auto& line) { overlap = line; }) &&
                  overlap == "Flotsam assets check already running",
              "fcache rejects an overlapping scan while the worker is active");
        allow_scan.set_value();
        check(result.wait_for(std::chrono::seconds(2)) == std::future_status::ready &&
                  result.get() == "Completed check with 1 assets." && await_file(),
              "fcache assets fetches and caches a scene texture from persistent storage");
        auto entered = std::make_shared<std::promise<void>>();
        auto entered_future = entered->get_future();
        std::promise<void> release;
        auto released = release.get_future().share();
        check(assets.cache_console({"fcache", "status"}, [entered, released, first = true](const auto&) mutable {
                  if (std::exchange(first, false)) {
                      entered->set_value();
                      released.wait_for(std::chrono::seconds(2));
                  }
              }) && entered_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready,
              "status starts on the console worker");
        auto shutdown = std::async(std::launch::async, [&] { assets.quiesce_cache_cleanup(); });
        const auto waits_for_status = shutdown.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout;
        release.set_value();
        shutdown.get();
        check(waits_for_status, "shutdown waits for an active console callback");
        assets.stop();
    }
    scene.shutdown();
}
void run() {
    TemporaryDirectory directory;
    auto settings = config(directory, "assets.ini", true);
    // Each pass owns fresh connector/module state; the SQLite database survives.
    for (int pass = 0; pass < 2; ++pass) {
        auto registry = std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 19000, 0, {}});
        SceneCapabilities capabilities(std::make_shared<Backend>(registry));
        auto registrations = capabilities.events().on_register_caps.snapshot().size();
        std::vector<std::pair<std::string, std::string>> logs;
        RegionAssetRuntime assets(
            settings, capabilities.shared_events(), "Asset region", {}, {},
            [&](const auto &level, const auto &message) { logs.emplace_back(level, message); });
        check(capabilities.events().on_register_caps.snapshot().size() == registrations + 1,
              "asset module subscribes once");
        framework::Uuid stored_id("00000000-0000-0000-0000-00000000a501");
        std::vector<std::uint8_t> stored_bytes{'L', 'S', 'L', 0, 0xff};
        if (pass == 0) {
            auto asset = std::make_shared<framework::AssetBase>(stored_id, "Stored script", std::int8_t(10),
                                                                std::string("creator"));
            asset->description("Actual script bytes");
            asset->data = std::make_shared<std::vector<std::uint8_t>>(stored_bytes);
            check(assets.store(asset) == stored_id.str(), "real local service stores script asset");
        }
        auto loaded = assets.get(stored_id);
        check(loaded && loaded->type() == 10 && loaded->data && *loaded->data == stored_bytes,
              "stored script bytes survive actual database/runtime restart");

        auto circuit = std::make_shared<framework::AgentCircuitData>();
        circuit->agent_id = framework::Uuid("00000000-0000-0000-0000-000000000007");
        circuit->circuit_code = 123;
        circuit->caps_path = "asset-test";
        capabilities.capabilities().set_agent_caps_seeds(circuit);
        capabilities.capabilities().create_caps(circuit->agent_id, circuit->circuit_code);
        capabilities.capabilities().activate_caps(circuit->circuit_code);
        auto caps = std::dynamic_pointer_cast<Caps>(
            capabilities.capabilities().get_caps_for_user(circuit->circuit_code));
        check(bool(caps), "real circuit caps created");
        check(registry->routes.simple_keys().size() == 1, "single seed route registered");
        check(registry->routes.poll_keys().size() == 5, "four asset routes plus event queue registered");
        for (auto name : {"GetTexture", "GetMesh", "GetMesh2", "ViewerAsset"}) {
            std::shared_ptr<CapabilityPollHandler> poll;
            check(caps->try_get_poll(name, poll) && bool(poll), "asset poll capability exists");
        }
        std::shared_ptr<server::CapabilitySimpleHandler> seed;
        check(registry->routes.try_simple("/CAPS/asset-test0000", seed), "source seed path exists");
        network::HttpRequest request;
        request.method = "POST";
        request.content_type = "application/llsd+xml";
        request.body =
            "<llsd><array><string>GetTexture</string><string>GetMesh</string><string>GetMesh2</string><string>ViewerAsset</string></array></llsd>";
        auto response = seed->handle(request);
        check(response.status == 200, "seed negotiation succeeds");
        for (auto name : {"GetTexture", "GetMesh", "GetMesh2", "ViewerAsset"})
            check(response.body.find(name) != std::string::npos, "seed advertises asset name");
        if (pass == 1) {
            sqlite3 *fixture = nullptr;
            check(sqlite3_open(text(directory.path / "assets.db").c_str(), &fixture) == SQLITE_OK,
                  "open owned fault-injection database");
            auto dropped = sqlite3_exec(fixture, "DROP TABLE assets", nullptr, nullptr, nullptr);
            sqlite3_close(fixture);
            check(dropped == SQLITE_OK, "inject asset query failure in owned fixture");
            check(!assets.get(stored_id), "asset service preserves caught storage failure result");
            check(std::any_of(logs.begin(), logs.end(),
                              [](const auto &entry) {
                                  return entry.first == "error" &&
                                         entry.second.starts_with(
                                             "[ASSET SERVICE]: Exception getting asset ");
                              }),
                  "asset runtime retains error severity without flattening it into message text");
        }
        assets.stop();
        assets.stop();
        bool closed = false;
        try {
            assets.get(stored_id);
        } catch (const std::exception &) {
            closed = true;
        }
        check(closed, "closed asset runtime rejects compiler retrieval");

        check(capabilities.events().on_register_caps.snapshot().size() == registrations,
              "asset stop unsubscribes idempotently");
        capabilities.shutdown();
        capabilities.shutdown();
        check(registry->routes.simple_keys().empty() && registry->routes.poll_keys().empty(),
              "owners release all capability routes");
        check(std::filesystem::is_regular_file(directory.path / "assets.db") &&
                  std::filesystem::file_size(directory.path / "assets.db") > 0,
              "real SQLite database exists across runtime restart");
    }
    for (auto invalid : {config(directory, "disabled.ini", false),
                         config(directory, "unsupported.ini", true, "Unsupported.Asset.Provider")}) {
        auto registry = std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 19001, 0, {}});
        SceneCapabilities capabilities(std::make_shared<Backend>(registry));
        auto before = capabilities.events().on_register_caps.snapshot().size();
        bool failed = false;
        try {
            RegionAssetRuntime assets(invalid, capabilities.shared_events());
        } catch (const std::exception &) {
            failed = true;
        }
        check(failed, "invalid asset service configuration rejects construction");
        check(capabilities.events().on_register_caps.snapshot().size() == before,
              "failed construction leaves no asset subscription");
        check(registry->routes.simple_keys().empty() && registry->routes.poll_keys().empty(),
              "failed construction advertises no routes");
        capabilities.shutdown();
    }
}
}
int main() {
    try {
        run();
        cache_profiles();
        std::cout << "PASS: " << checks << " region asset runtime checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
