// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_asset_runtime.hpp"
#include <pugixml.hpp>
#include "opensim/framework/texture_entry.hpp"
#include <charconv>
#include <sstream>
#include "opensim/services/get_assets_module.hpp"
#include "opensim/services/local_asset_connector.hpp"
#include "opensim/services/native_asset_storage.hpp"
#include "opensim/services/flotsam_memory_cache.hpp"
#include "opensim/services/flotsam_cache_module.hpp"
#include "opensim/services/flotsam_file_access.hpp"
#include "opensim/services/native_flotsam_config.hpp"
#include "opensim/services/native_flotsam_datetime.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/web_http_clients.hpp"
#include "opensim/network/osd_service_requester.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/http_host.hpp"
#include "opensim/services/user_account.hpp"
#include <bit>
#include <algorithm>
#include <exception>
#include <chrono>
#include <thread>
namespace opensim::services {
std::string serialize_baked_appearance(const framework::WearableCacheItems &cache) {
    pugi::xml_document document;
    auto root = document.append_child("BakedAppearance");
    for (const bool extended : {false, true})
        for (const auto &item : cache) {
            if (!item || !item->texture_asset || (item->texture_index > 26) != extended)
                continue;
            auto bake = root.append_child(extended ? "BESetA" : "BakedTexture");
            bake.append_attribute("TextureIndex") = item->texture_index;
            bake.append_attribute("CacheId") = item->cache_id.str().c_str();
            const auto &asset = *item->texture_asset;
            auto row = bake.append_child("AssetBase");
            const auto add = [&](const char *field, const std::string &value) {
                row.append_child(field).text().set(value.c_str());
            };
            if (asset.data) {
                network::Value bytes;
                bytes.binary_data = asset.data;
                add("Data", network::binary_base64(bytes));
            }
            row.append_child("FullID").append_child("Guid").text().set(asset.full_id().str().c_str());
            add("ID", asset.id());
            add("Name", asset.name().value_or(""));
            add("Description", asset.description().value_or(""));
            if (const auto creator = asset.creator_id())
                add("CreatorID", *creator);
            add("Type", std::to_string(asset.type()));
            add("Local", asset.local() ? "true" : "false");
            add("Temporary", asset.temporary() ? "true" : "false");
            add("UploadAttempts", std::to_string(asset.upload_attempts));
            std::string flags;
            for (const auto &[bit, name] :
                 {std::pair{framework::Maptile, "Maptile"}, std::pair{framework::Rewritable, "Rewritable"},
                  std::pair{framework::Collectable, "Collectable"}}) {
                if (asset.flags() & bit) {
                    if (!flags.empty())
                        flags += ' ';
                    flags += name;
                }
            }
            add("Flags", flags.empty() ? "Normal" : flags);
        }
    std::ostringstream output;
    document.save(output, "", pugi::format_raw | pugi::format_no_declaration);
    return output.str();
}
std::shared_ptr<framework::AssetBase> deserialize_remote_asset(std::string_view xml) {
    pugi::xml_document document;
    if (!document.load_buffer(xml.data(), xml.size()))
        return {};
    const auto root = document.document_element();
    const auto is_nil = [](pugi::xml_node node) {
        for (const auto attribute : node.attributes()) {
            const std::string_view name = attribute.name();
            if (!name.ends_with(":nil"))
                continue;
            const auto ns = "xmlns:" + std::string(name.substr(0, name.size() - 4));
            for (auto scope = node; scope; scope = scope.parent()) {
                if (auto declaration = scope.attribute(ns.c_str())) {
                    if (std::string_view(declaration.value()) == "http://www.w3.org/2001/XMLSchema-instance")
                        return attribute.as_bool();
                    break;
                }
            }
        }
        return false;
    };
    if (std::string_view(root.name()) != "AssetBase" || is_nil(root))
        return {};
    auto asset = std::make_shared<framework::AssetBase>();
    const auto integer = [](pugi::xml_node node) {
        const auto text = framework::trim_utf8(node.child_value());
        int result{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw std::invalid_argument("Invalid asset integer");
        return result;
    };
    try {
        for (const auto node : root.children()) {
            const std::string_view field = node.name();
            const bool nil = is_nil(node);
            const auto text = [&]() -> std::optional<std::string> {
                return nil ? std::nullopt : std::optional<std::string>(node.child_value());
            };
            if (field == "Data") {
                if (nil) {
                    asset->data.reset();
                    continue;
                }
                network::Value bytes;
                bytes.kind = network::Value::Kind::Binary;
                bytes.text = node.child_value();
                asset->data = network::osd_binary(bytes);
            } else if (field == "FullID") {
                framework::Uuid id;
                if (!framework::try_parse_sdk_uuid(node.child_value("Guid"), id))
                    return {};
                asset->full_id(id);
            } else if (field == "ID")
                asset->id(text());
            else if (field == "Name")
                asset->name(text());
            else if (field == "Description")
                asset->description(text());
            else if (field == "CreatorID")
                asset->creator_id(text());
            else if (field == "Type") {
                const int type = integer(node);
                if (type < -128 || type > 127)
                    return {};
                asset->type(static_cast<std::int8_t>(type));
            } else if (field == "UploadAttempts")
                asset->upload_attempts = integer(node);
            else if (field == "Local")
                asset->local(node.text().as_bool());
            else if (field == "Temporary")
                asset->temporary(node.text().as_bool());
            else if (field == "Flags") {
                std::istringstream values(node.child_value());
                std::string flag;
                std::int32_t flags = framework::Normal;
                while (values >> flag) {
                    if (flag == "Maptile")
                        flags |= framework::Maptile;
                    else if (flag == "Rewritable")
                        flags |= framework::Rewritable;
                    else if (flag == "Collectable")
                        flags |= framework::Collectable;
                    else if (flag != "Normal")
                        return {};
                }
                asset->flags(flags);
            }
        }
    } catch (const std::exception &) {
        return {};
    }
    return asset;
}
framework::WearableCacheItems deserialize_baked_appearance(std::string_view xml) {
    pugi::xml_document document;
    if (!document.load_buffer(xml.data(), xml.size()))
        return {};
    const auto root = document.child("BakedAppearance");
    framework::WearableCacheItems result;
    for (const auto bake : root.children()) {
        const std::string_view name = bake.name();
        if (name != "BakedTexture" && name != "BESetA")
            continue;
        const auto index = bake.attribute("TextureIndex").as_uint(45);
        if (index >= framework::TextureEntry::max_faces)
            continue;
        std::ostringstream xml_asset;
        bake.child("AssetBase").print(xml_asset, "", pugi::format_raw);
        auto asset = deserialize_remote_asset(xml_asset.str());
        if (!asset || asset->full_id().is_zero() || !asset->data)
            continue;
        auto item = std::make_shared<framework::WearableCacheItem>();
        item->texture_index = index;
        framework::try_parse_sdk_uuid(bake.attribute("CacheId").value(), item->cache_id);
        item->texture_id = asset->full_id();
        item->texture_asset = std::move(asset);
        result.push_back(std::move(item));
    }
    return result;
}
std::shared_ptr<framework::AssetBase> request_remote_asset(const network::WebHttpClients &clients,
                                                           const std::string &server,
                                                           const framework::Uuid &id) {
    using Clock = std::chrono::steady_clock;
    try {
        auto backend = framework::native_config_uri_object_backend();
        auto url = framework::utf16_replacing(server + "/assets/" + id.str());
        auto uri =
            backend->create(framework::nini_writer_text(url), framework::ConfigUriObjectKind::Absolute);
        framework::ConfigXmlHttpOperation operation;
        operation.method = "GET";
        operation.headers = {{"Keep-Alive", framework::nini_writer_text(u"timeout=30, max=10")},
                             {"Connection", framework::nini_writer_text(u"Keep-Alive")}};
        std::optional<std::int64_t> length;
        operation.response_headers = [&length](const auto &response) {
            std::vector<std::u16string> values;
            for (const auto &field : response.headers)
                if (framework::ordinal_equals_ignore_case(field.name, "Content-Length"))
                    values.push_back(field.value);
            length = framework::config_http_content_length(values);
        };
        auto deadline = std::make_shared<Clock::time_point>(Clock::time_point::max());
        operation.body_interrupted = [deadline] { return Clock::now() >= *deadline; };
        auto client = clients.get_new_global_client(0);
        auto stream = client->send_headers(std::move(uri), std::move(operation), true);
        struct Close {
            std::shared_ptr<framework::ConfigXmlStream> stream;
            ~Close() {
                try {
                    stream->close();
                } catch (...) {
                }
            }
        } close{stream};
        if (!length || *length == 0)
            return {};
        const auto timeout = network::osd_service_receive_timeout(length, true);
        if (timeout < -1)
            return {};
        if (timeout >= 0)
            *deadline = Clock::now() + std::chrono::milliseconds(timeout);
        const auto body = stream->copy_to_memory();
        if (body.empty())
            return {};
        return deserialize_remote_asset({reinterpret_cast<const char *>(body.data()), body.size()});
    } catch (const std::exception &) {
        return {};
    }
}
namespace {
std::shared_ptr<FlotsamStaticConfig> shared_cache_configuration() {
    static auto state = std::make_shared<FlotsamStaticConfig>();
    return state;
}
std::shared_ptr<std::recursive_mutex> shared_cache_configuration_gate() {
    static auto gate = std::make_shared<std::recursive_mutex>();
    return gate;
}
bool white(const std::string &text) {
    auto chars = framework::utf16_replacing(text);
    return std::all_of(chars.begin(), chars.end(), framework::unicode_whitespace);
}
struct Backend final : AssetsModuleBackend, AssetConnectorBackend {
    std::function<void(std::string)> log;
    RegionAssetRuntime::StructuredLog structured_log;
    std::shared_ptr<AssetStorageState> storage_state = std::make_shared<AssetStorageState>();
    std::shared_ptr<NativeAssetStorageBackend> storage;
    std::shared_ptr<ConfiguredAssetService> service;
    framework::ObjectJobEngine callbacks;
    explicit Backend(std::function<void(std::string)> logger, RegionAssetRuntime::StructuredLog typed)
        : log(std::move(logger)), structured_log(std::move(typed)),
          callbacks([](std::any value) { std::any_cast<std::function<void()>>(value)(); },
                    "RegionAssetCallbacks", 1000, 3,
                    [this](const auto &name, const auto &error) { job_error(name, error); }) {
        // Capture the logger by value: storage may outlive the runtime through caps.
        storage = std::make_shared<NativeAssetStorageBackend>(
            [logger = log, typed = structured_log](const auto &level, const auto &text) {
                if (typed)
                    typed(level, text);
                else if (logger)
                    logger(level + ": " + text);
            });
    }
    void emit(const std::string &level, const std::string &text) noexcept {
        try {
            if (structured_log)
                structured_log(level, text);
            else if (log)
                log(text);
        } catch (...) {
        }
    }
    void warn(const std::string &text) override {
        emit("warn", text);
    }
    void error(const std::string &text) override {
        emit("error", text);
    }
    void info(const std::string &text) override {
        emit("info", text);
    }
    void debug(const std::string &text) override {
        emit("debug", text);
    }
    void job_error(const std::string &name, const std::exception &error) override {
        emit("error", name + ": " + error.what());
    }
    void post(std::function<void()> callback, const std::string &) override {
        callbacks.enqueue(std::move(callback));
    }
    bool try_post(std::function<void()> callback, const std::string &) override {
        return callbacks.enqueue(std::move(callback));
    }
    std::shared_ptr<AssetService> load_service(const std::string &module,
                                               const server::IniConfig *config) override {
        if (module != "OpenSim.Services.AssetService.dll:AssetService")
            throw std::invalid_argument("Unsupported local AssetService module: " + module);
        AssetStorageFactory factory(storage, storage_state);
        service = factory.create(config);
        return service;
    }
};
struct CacheBackend final : FlotsamCacheModuleBackend {
    std::shared_ptr<NativeAssetStorageBackend> storage;
    explicit CacheBackend(std::shared_ptr<NativeAssetStorageBackend> value) : storage(std::move(value)) {}
    std::shared_ptr<DefaultAssetLoader> load_loader(const std::string &name) override {
        return storage->load_loader(name);
    }
    void cleanup_elapsed(std::shared_ptr<FlotsamCacheLifecycle>) override {
        throw std::runtime_error("Native protected file-cache cleanup is not implemented");
    }
};
struct AssetCircuitDirectory final : AssetsUserManagement {
    RegionAssetRuntime::AssetServerLookup lookup;
    explicit AssetCircuitDirectory(RegionAssetRuntime::AssetServerLookup value) : lookup(std::move(value)) {}
    std::optional<std::string> user_server_url(framework::Uuid agent, const std::string &key) override {
        return key == "AssetServerURI" ? lookup(agent) : std::nullopt;
    }
};
struct Scene final : AssetsModuleScene, AssetConnectorScene, FlotsamCacheScene {
    std::shared_ptr<SceneCapabilityEvents> source;
    std::shared_ptr<AssetService> registered;
    std::shared_ptr<framework::IAssetCache> cache;
    std::optional<std::string> name;
    framework::Uuid id;
    std::atomic<bool> stopping = false;
    Scene(std::shared_ptr<SceneCapabilityEvents> events, std::optional<std::string> region,
          framework::Uuid region_id)
        : source(std::move(events)), name(std::move(region)), id(region_id) {}
    void register_service(std::shared_ptr<AssetService> service) override {
        registered = std::move(service);
    }
    AssetConnectorCache asset_cache() override {
        return {cache, true};
    }
    std::optional<std::string> region_name() override {
        return name;
    }
    SceneCapabilityEvents &events() override {
        return *source;
    }
    bool shutting_down() const override {
        return stopping;
    }
    std::shared_ptr<server::AssetRetrievalService> asset_service() override {
        return registered;
    }
    std::shared_ptr<AssetsUserManagement> directory;
    std::shared_ptr<AssetsUserManagement> user_management() override {
        return directory;
    }
    std::shared_ptr<AssetsExternalCaps> external_caps() override {
        return {};
    }
    void register_cache(std::shared_ptr<framework::IAssetCache> value) override {
        cache = std::move(value);
    }
    void unregister_cache(std::shared_ptr<framework::IAssetCache> value) override {
        if (cache == value)
            cache.reset();
    }
    std::shared_ptr<AssetService> request_asset_service() override {
        return registered;
    }
    framework::Uuid cache_region_id() override {
        return id;
    }
};
}
struct RegionAssetRuntime::State {
    std::shared_ptr<FlotsamConsoleBackend> cache_dates;
    std::shared_ptr<Backend> backend;
    std::shared_ptr<Scene> scene;
    std::shared_ptr<LocalAssetConnector> connector;
    std::shared_ptr<GetAssetsModule> module;
    std::shared_ptr<FlotsamCacheModule> cache_module;
    std::shared_ptr<CacheBackend> cache_backend;
    std::shared_ptr<FlotsamInstanceConfig> cache_config;
    NativeFlotsamContainers cache_containers;
    std::shared_ptr<std::vector<FlotsamConfigCommand>> cache_commands =
        std::make_shared<std::vector<FlotsamConfigCommand>>();
    std::shared_ptr<AssetsModuleState> module_state = std::make_shared<AssetsModuleState>();
    std::shared_ptr<network::WebHttpClients> foreign_http;
    std::string disallow_import;
    std::atomic<bool> stopped = false;
    void stop() {
        if (stopped.exchange(true))
            return;
        std::exception_ptr first;
        auto attempt = [&](auto action) {
            try {
                action();
            } catch (...) {
                if (!first)
                    first = std::current_exception();
            }
        };
        if (scene)
            scene->stopping = true;
        if (module && scene && int32_t(module_state->scenes) > 0)
            attempt([&] { module->remove_region(scene); });
        // Handler waits for connector callbacks: callback pool and database stay live
        // until the asset request workers have joined.
        auto worker = std::dynamic_pointer_cast<framework::ObjectJobEngine>(module_state->worker.load());
        if (module)
            attempt([&] { module->close(); });
        if (worker)
            while (worker->active_threads() != 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::shared_ptr<framework::ObjectJobEngine> file_worker;
        if (cache_module)
            file_worker = std::dynamic_pointer_cast<framework::ObjectJobEngine>(
                cache_module->shared_state()->worker.load());
        if (cache_module && scene)
            attempt([&] { cache_module->remove_region(scene); });
        if (cache_module)
            attempt([&] { cache_module->close(); });
        if (file_worker)
            while (file_worker->active_threads() != 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (connector && scene)
            attempt([&] { connector->remove_region(scene); });
        if (connector)
            attempt([&] { connector->close(); });
        if (backend)
            attempt([&] { backend->callbacks.dispose(); });
        if (backend)
            while (backend->callbacks.active_threads() != 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (backend && backend->service && backend->service->database)
            attempt([&] { backend->service->database->dispose(); });
        module_state->handler.store(nullptr);
        module_state->service.store(nullptr);
        if (scene)
            scene->registered.reset();
        cache_module.reset();
        cache_backend.reset();
        module.reset();
        connector.reset();
        scene.reset();
        backend.reset();
        if (first)
            std::rethrow_exception(first);
    }
    ~State() {
        try {
            stop();
        } catch (...) {
        }
    }
};
RegionAssetRuntime::RegionAssetRuntime(const server::IniConfig &config,
                                       std::shared_ptr<SceneCapabilityEvents> events,
                                       std::optional<std::string> region_name,
                                       std::function<void(std::string)> log, framework::Uuid region_id,
                                       StructuredLog structured_log,
                                       std::shared_ptr<network::WebHttpClients> http,
                                       AssetServerLookup foreign_server)
    : state_(std::make_unique<State>()) {
    if (!events)
        throw std::invalid_argument("Null region asset capability events");
    std::shared_ptr<framework::IAssetCache> cache;
    FlotsamCacheModule::Options cache_options;
    if (config.get("Modules", "AssetCaching") == "FlotsamAssetCache") {
        auto typed_source = native_flotsam_config_source(
            std::shared_ptr<const server::IniConfig>(&config, [](const server::IniConfig *) {}));
        state_->cache_dates = std::make_shared<FlotsamConsoleBackend>();
        bind_native_invariant_flotsam_datetime(*state_->cache_dates);
        state_->cache_config = std::make_shared<FlotsamInstanceConfig>();
        auto common = shared_cache_configuration();
        auto logger = [log, structured_log](const auto &text) {
            if (structured_log)
                structured_log("normal", text);
            else if (log)
                log(text);
        };
        state_->cache_containers = native_flotsam_cache_containers(
            state_->cache_config, common, {}, true, logger, logger,
            [commands = state_->cache_commands](const auto &command) { commands->push_back(command); },
            shared_cache_configuration_gate());
        std::lock_guard configuration_lock(*state_->cache_containers.gate);
        FlotsamCacheConfig initializer(state_->cache_config, common, state_->cache_containers.backend);
        initializer.initialise(typed_source);
        if (state_->cache_config->file)
            throw std::invalid_argument(
                "Native Flotsam file cache is not implemented; production hosting remains unavailable");
        auto settings = state_->cache_containers.entries->live_options();
        auto files = std::make_shared<FlotsamMemoryCache::FileOperations>();
        files->debug = logger;
        files->warn = [log, structured_log](const auto &text) {
            if (structured_log)
                structured_log("warn", text);
            else if (log)
                log(text);
        };
        // Static access settings can be changed by another initialization. Resolve
        // the current tracker on every touch; even a memory-only instance must keep
        // the original caught-error timestamp behavior when that static flag changes.
        files->touch = [instance = state_->cache_config, common,
                        gate = state_->cache_containers.gate](const std::string &id) {
            FlotsamFileStore::Options paths;
            {
                std::lock_guard lock(*gate);
                paths.directory =
                    std::filesystem::path(framework::utf16_replacing(instance->directory.value_or("")));
                paths.tiers = common->tiers;
                paths.tier_length = common->tier_length;
            }
            FlotsamFileStore names(paths, native_flotsam_file_backend({}));
            auto name = names.file_name(id);
            auto key = framework::utf8(name.u16string());
            try {
                std::shared_ptr<FlotsamKeyEntries> tracker;
                {
                    std::lock_guard lock(*gate);
                    tracker = std::static_pointer_cast<FlotsamKeyEntries>(common->access_entries);
                }
                if (!tracker)
                    return;
                if (!tracker->contains(key)) {
                    native_flotsam_set_access_time(name);
                    {
                        std::lock_guard lock(*gate);
                        tracker = std::static_pointer_cast<FlotsamKeyEntries>(common->access_entries);
                    }
                    if (tracker)
                        tracker->add(key, 900000);
                }
            } catch (...) {
            }
        };
        cache = std::make_shared<FlotsamMemoryCache>(settings, state_->cache_containers.entries, files);
        cache_options.enabled = state_->cache_config->enabled;
        cache_options.file = state_->cache_config->file;
        cache_options.expiration_ticks = state_->cache_config->expiration_ticks;
        cache_options.cleanup_ticks = state_->cache_config->cleanup_ticks;
        cache_options.loader = state_->cache_config->loader;
        cache_options.loader_arguments = state_->cache_config->loader_arguments.value_or("");
    }
    state_->backend = std::make_shared<Backend>(std::move(log), std::move(structured_log));
    state_->scene = std::make_shared<Scene>(std::move(events), std::move(region_name), region_id);
    if (cache) {
        state_->cache_backend = std::make_shared<CacheBackend>(state_->backend->storage);
        state_->cache_module = std::make_shared<FlotsamCacheModule>(std::move(cache_options),
                                                                    std::move(cache), state_->cache_backend);
        state_->cache_module->post_initialise();
        state_->cache_module->add_region(state_->scene);
    }
    state_->connector = std::make_shared<LocalAssetConnector>(state_->backend);
    if (config.get("Modules", "AssetServices") == "RegionAssetConnector") {
        const auto module = config.get("AssetService", "LocalGridAssetService");
        if (module.empty())
            throw std::invalid_argument("RegionAssetConnector requires AssetService.LocalGridAssetService");
        const auto foreign = config.get("AssetService", "HypergridAssetService");
        if (!foreign.empty()) {
            if (foreign != "OpenSim.Services.Connectors.dll:HGAssetServiceConnector" || !http)
                throw std::invalid_argument("Unsupported Hypergrid asset service: " + foreign);
            state_->foreign_http = std::move(http);
            state_->disallow_import = config.get("HGAssetService", "DisallowImport");
        }
        state_->connector->initialise_service(&config, module);
    } else {
        state_->connector->initialise(&config);
    }
    if (state_->foreign_http)
        state_->connector->foreign_fetch([this](const auto &id, const auto &server, bool local) {
            framework::Uuid asset;
            return framework::try_parse_sdk_uuid(id, asset) ? get_foreign(asset, server, local) : nullptr;
        });
    if (state_->foreign_http && foreign_server)
        state_->scene->directory = std::make_shared<AssetCircuitDirectory>(std::move(foreign_server));
    state_->connector->add_region(state_->scene);
    if (!state_->scene->registered)
        throw std::invalid_argument(
            "Region assets require configured LocalAssetServicesConnector and local AssetService");
    state_->connector->region_loaded(state_->scene);
    if (state_->cache_module)
        state_->cache_module->region_loaded(state_->scene);
    state_->module = std::make_shared<GetAssetsModule>(state_->backend, state_->module_state);
    state_->module->initialise(&config);
    state_->module->add_region(state_->scene);
    state_->module->region_loaded(state_->scene);
}
std::shared_ptr<framework::AssetBase> RegionAssetRuntime::get(const framework::Uuid &id) {
    if (!state_ || state_->stopped || !state_->scene || !state_->scene->registered)
        throw std::runtime_error("Region asset service stopped");
    return state_->scene->registered->get_asset(id.str());
}
std::shared_ptr<framework::AssetBase>
RegionAssetRuntime::get_foreign(const framework::Uuid &id, const std::optional<std::string> &server,
                                bool store_local) {
    if (!state_ || state_->stopped || !state_->scene || !state_->scene->registered)
        throw std::runtime_error("Region asset service stopped");
    if (!state_->foreign_http)
        return state_->scene->registered->get_foreign(id.str(), server, store_local);
    auto cache = state_->scene->cache;
    if (cache)
        if (auto cached = cache->get_cached(id.str()))
            return cached;
    auto asset = state_->backend->service->get_asset(id.str());
    if (!asset && server && !server->empty()) {
        framework::HttpHost remote(*server, true);
        if (remote.resolved())
            asset = request_remote_asset(*state_->foreign_http, remote.uri, id);
        if (asset) {
            static constexpr std::pair<int, std::string_view> names[] = {{-2, "OSMaterial"},
                                                                         {-1, "Unknown"},
                                                                         {0, "Texture"},
                                                                         {1, "Sound"},
                                                                         {2, "CallingCard"},
                                                                         {3, "Landmark"},
                                                                         {5, "Clothing"},
                                                                         {6, "Object"},
                                                                         {7, "Notecard"},
                                                                         {8, "Folder"},
                                                                         {10, "LSLText"},
                                                                         {11, "LSLBytecode"},
                                                                         {12, "TextureTGA"},
                                                                         {13, "Bodypart"},
                                                                         {17, "SoundWAV"},
                                                                         {18, "ImageTGA"},
                                                                         {19, "ImageJPEG"},
                                                                         {20, "Animation"},
                                                                         {21, "Gesture"},
                                                                         {22, "Simstate"},
                                                                         {24, "Link"},
                                                                         {25, "LinkFolder"},
                                                                         {26, "MarketplaceFolder"},
                                                                         {49, "Mesh"},
                                                                         {56, "Settings"},
                                                                         {57, "Material"},
                                                                         {58, "GLTF"},
                                                                         {59, "GLTF_bin"}};
            auto type = std::find_if(std::begin(names), std::end(names),
                                     [&](const auto &pair) { return pair.first == asset->type(); });
            std::istringstream imports(state_->disallow_import);
            for (std::string entry; type != std::end(names) && std::getline(imports, entry, ',');)
                if (framework::ordinal_equals_ignore_case(framework::trim_utf8(entry), type->second)) {
                    asset.reset();
                    break;
                }
            if (asset && store_local)
                state_->backend->service->store(asset);
        }
    }
    if (cache) {
        if (asset)
            cache->cache(asset);
        else
            cache->cache_negative(id.str());
    }
    return asset;
}
std::optional<std::string> RegionAssetRuntime::store(std::shared_ptr<framework::AssetBase> asset) {
    if (!state_ || state_->stopped || !state_->scene || !state_->scene->registered)
        throw std::runtime_error("Region asset service stopped");
    return state_->scene->registered->store(std::move(asset));
}
bool RegionAssetRuntime::default_assets_command(bool remove) {
    if (!state_ || state_->stopped)
        throw std::runtime_error("Region asset service stopped");
    if (!state_->cache_module)
        return false;
    auto logger = [backend = state_->backend](const std::string &text) { backend->emit("normal", text); };
    if (remove)
        state_->cache_module->delete_default_assets(logger);
    else
        state_->cache_module->load_default_assets(logger);
    return true;
}
std::vector<std::string> RegionAssetRuntime::cache_command_names() const {
    std::vector<std::string> names;
    if (state_ && !state_->stopped)
        for (const auto &command : *state_->cache_commands)
            names.push_back(command.command);
    return names;
}
std::vector<framework::ConsoleCommandDescription> RegionAssetRuntime::cache_command_descriptions() const {
    std::vector<framework::ConsoleCommandDescription> commands;
    if (state_ && !state_->stopped)
        for (const auto &command : *state_->cache_commands)
            commands.push_back(
                {command.category, command.command, command.usage, command.description, "", command.shared});
    return commands;
}
bool RegionAssetRuntime::cache_console(const std::vector<std::string> &args,
                                       std::function<void(const std::string &)> output) {
    if (!state_ || state_->stopped)
        throw std::runtime_error("Region asset service stopped");
    if (!state_->cache_module || args.size() < 2)
        return false;
    if (!output)
        throw std::invalid_argument("Missing cache console output");
    const auto &action = args[1];
    if (action == "cachedefaultassets" || action == "deletedefaultassets")
        return default_assets_command(action == "deletedefaultassets");
    auto cache = std::dynamic_pointer_cast<FlotsamMemoryCache>(state_->scene->cache);
    if (!cache)
        throw std::runtime_error("Flotsam cache unavailable");
    auto settings = cache->settings();
    if (action == "expire") {
        // File-enabled construction is guarded above until scene gathering and heap
        // providers exist. Use the same source expire operation for memory-only hosts.
        if (settings.file)
            throw std::runtime_error("File cache console hosting incomplete");
        struct Output final : FlotsamConsoleOutput {
            std::function<void(const std::string &)> sink;
            explicit Output(std::function<void(const std::string &)> value) : sink(std::move(value)) {}
            void output(const std::string &value) override {
                sink(value);
            }
        };
        auto arguments = std::make_shared<FlotsamCacheConsole::Arguments>();
        for (const auto &value : args)
            arguments->emplace_back(value);
        FlotsamCacheConsole::expire(arguments, state_->cache_config, state_->cache_module->lifecycle(),
                                    state_->cache_dates, std::make_shared<Output>(std::move(output)));
        return true;
    }
    if (action == "status") {
        if (settings.memory)
            output("[FLOTSAM ASSET CACHE] Memory Cache: " + std::to_string(cache->status().memory_entries) +
                   " assets");
        else
            output("[FLOTSAM ASSET CACHE] Memory cache disabled");
        // File-enabled startup remains rejected until the complete maintenance host
        // and scene/environment providers are connected.
        if (settings.file)
            throw std::runtime_error("File cache console hosting incomplete");
        output("[FLOTSAM ASSET CACHE] File cache disabled");
        for (const auto &line : cache->hit_report())
            output(line);
        return true;
    }
    if (action == "clearnegatives") {
        if (settings.negative)
            output("Flotsam cache of negatives cleared (" + std::to_string(cache->clear_negatives()) +
                   " entries)");
        else
            output("Flotsam cache of negatives not enabled");
        return true;
    }
    if (action == "clear") {
        bool memory = args.size() == 2, file = memory;
        for (auto value : args) {
            for (auto &c : value)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + ('a' - 'A'));
            if (value == "memory")
                memory = true;
            else if (value == "file")
                file = true;
        }
        if (memory) {
            if (settings.memory) {
                cache->clear_memory();
                output("Memory cache cleared.");
            } else
                output("Memory cache not enabled.");
        }
        if (file) {
            if (settings.file)
                throw std::runtime_error("File cache console hosting incomplete");
            else
                output("File cache not enabled.");
        }
        if (settings.negative)
            cache->clear_negatives();
        return true;
    }
    return false;
}
RegionAssetRuntime::~RegionAssetRuntime() = default;
std::shared_ptr<framework::IAssetCache> RegionAssetRuntime::asset_cache() const {
    return state_ && state_->scene ? state_->scene->cache : std::shared_ptr<framework::IAssetCache>{};
}
void RegionAssetRuntime::stop() {
    state_->stop();
}
}
