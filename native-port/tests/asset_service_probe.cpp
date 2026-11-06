// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/asset_store.hpp"
#include "opensim/services/fs_asset_files.hpp"
#include "opensim/services/native_asset_storage.hpp"
#include "opensim/services/region_asset_runtime.hpp"
#include "opensim/network/web_http_clients.hpp"
#include "opensim/network/async_http_server.hpp"
#include "opensim/services/get_assets_module.hpp"
#include "opensim/server/http_route_registry.hpp"
#include "opensim/server/poll_http_service.hpp"
#include <future>
#include <iostream>
#include <sstream>
#include <thread>
using namespace opensim;
using namespace services;
std::string hex(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string unhex(const std::string &s) {
    if (s == ".")
        return "";
    std::string out;
    for (size_t i = 0; i < s.size(); i += 2)
        out += char(std::stoi(s.substr(i, 2), nullptr, 16));
    return out;
}
struct Context {
    std::string trace;
    int mode, exist;
    bool stored, deleted;
    std::string fail;
    AssetService::Asset a = std::make_shared<framework::AssetBase>(),
                        b = std::make_shared<framework::AssetBase>();
    Context() {
        a->id("00000000-0000-0000-0000-000000000001");
        b->id("00000000-0000-0000-0000-000000000002");
        a->data = std::make_shared<std::vector<uint8_t>>(std::initializer_list<uint8_t>{1});
        b->data = std::make_shared<std::vector<uint8_t>>(std::initializer_list<uint8_t>{2});
    }
    void event(const std::string &value) {
        trace += value + ";";
        if (value.starts_with(fail + ":") && !fail.empty())
            throw std::runtime_error("log");
    }
    std::string name(const AssetService::Asset &value) {
        return !value ? "N" : value == a ? "A" : value == b ? "B" : "?";
    }
};
struct Store : AssetStore {
    Context &c;
    explicit Store(Context &value) : c(value) {}
    AssetService::Asset get(framework::Uuid id) override {
        c.event("get:" + id.str());
        if (c.mode == 2)
            throw std::runtime_error("get");
        return c.mode == 1 ? nullptr : c.mode == 3 ? c.b : c.a;
    }
    bool store(AssetService::Asset a) override {
        c.event("store:" + c.name(a));
        return c.stored;
    }
    std::vector<bool> exists(const std::vector<framework::Uuid> &ids) override {
        std::string value = "exists:";
        for (auto id : ids)
            value += id.str() + ",";
        c.event(value);
        if (c.exist == 3)
            throw std::runtime_error("exists");
        return c.exist == 0 ? std::vector<bool>{} : std::vector<bool>{c.exist == 2};
    }
    std::vector<std::shared_ptr<framework::AssetMetadata>> metadata_set(int32_t, int32_t) override {
        return {};
    }
    bool erase(const std::string &id) override {
        c.event("delete:" + id);
        return c.deleted;
    }
};
int main(int argc, char **argv) try {
    if (argc == 7 && std::string_view(argv[1]) == "--fs-files") {
        const auto operation = std::string_view(argv[2]);
        const FsAssetFiles files(argv[3], argv[4], std::string_view(argv[5]) == "osgrid");
        if (operation == "stage") {
            const auto bytes = unhex(argv[6]);
            const auto hash = FsAssetFiles::digest(bytes);
            files.stage(hash, bytes);
            std::cout << hash << '\n';
        } else if (operation == "read") {
            const auto bytes = files.read(argv[6]);
            std::cout << (bytes ? hex(*bytes) : "missing") << '\n';
        } else if (operation == "flush") {
            std::cout << files.flush() << '\n';
        } else {
            throw std::invalid_argument("Unknown filesystem asset probe operation");
        }
        return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "--mysql-provider") {
        const auto config = server::IniConfig::load(argv[2]);
        auto backend = std::make_shared<NativeAssetStorageBackend>();
        const auto store = AssetStorageFactory(backend).create(&config)->database;
        const framework::Uuid id(argv[3]);
        auto asset = store->get(id);
        if (!asset || !asset->data)
            throw std::runtime_error("MySQL provider could not reload the asset");
        asset->flags(framework::Rewritable);
        asset->description("Provider replacement");
        if (!store->store(asset))
            throw std::runtime_error("MySQL provider replacement failed");
        const auto stored = store->get(id);
        if (!stored || stored->flags() != framework::Rewritable ||
            stored->description() != "Provider replacement" || !stored->data || *stored->data != *asset->data)
            throw std::runtime_error("MySQL provider did not replace the asset fields");
        const auto first = store->metadata_set(0, 1);
        if (first.size() != 1 || first[0]->full_id() != id || first[0]->flags != framework::Rewritable ||
            !first[0]->sha1 || !first[0]->sha1->empty())
            throw std::runtime_error("MySQL provider metadata pagination differed from the original");
        if (!store->erase(id.str()) || store->get(id) || !store->erase(id.str()))
            throw std::runtime_error("MySQL provider did not report successful deletes");
        return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "--pgsql-provider") {
        const auto config = server::IniConfig::load(argv[2]);
        auto backend = std::make_shared<NativeAssetStorageBackend>();
        const auto store = AssetStorageFactory(backend).create(&config)->database;
        const framework::Uuid id(argv[3]);
        auto asset = store->get(id);
        if (!asset || !asset->data)
            throw std::runtime_error("PostgreSQL provider could not reload the asset");
        const auto flags = asset->flags();
        asset->flags(framework::Rewritable);
        asset->description("Provider update");
        if (!store->store(asset))
            throw std::runtime_error("PostgreSQL provider update failed");
        const auto stored = store->get(id);
        if (!stored || stored->flags() != flags || stored->description() != "Provider update" ||
            !stored->data || *stored->data != *asset->data)
            throw std::runtime_error("PostgreSQL provider did not preserve the original update semantics");
        const auto first = store->metadata_set(0, 2);
        if (!store->metadata_set(0, 1).empty() || first.size() != 1 || first[0]->full_id() != id ||
            first[0]->description != "Provider update" || first[0]->flags != framework::Normal)
            throw std::runtime_error("PostgreSQL provider metadata pagination differed from the original");
        return 0;
    }
    if (argc == 6 && std::string_view(argv[1]) == "--asset-read-queues") {
        auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        const auto config = server::IniConfig::load(argv[2]);
        auto events = std::make_shared<SceneCapabilityEvents>(SceneCapabilityEvents::Error{});
        RegionAssetRuntime runtime(config, events, "Region asset queue isolation", {}, {}, {}, clients);
        const auto service = runtime.asset_service();
        const std::string remote = argv[3];
        const std::string local_id = argv[4];
        std::vector<std::future<AssetService::Asset>> results;
        for (int index = 0; index < 3; ++index) {
            const auto id = std::string(argv[5]) + std::to_string(index);
            auto result = std::make_shared<std::promise<AssetService::Asset>>();
            results.push_back(result->get_future());
            service->get(id, remote, false, [result](auto asset) { result->set_value(std::move(asset)); });
        }
        auto local_result = std::make_shared<std::promise<AssetService::Asset>>();
        auto local = local_result->get_future();
        if (!service->get_async(local_id, {}, [local_result](const auto&, std::any, auto asset) {
                local_result->set_value(std::move(asset));
            }) || local.wait_for(std::chrono::seconds(8)) != std::future_status::ready || !local.get())
            throw std::runtime_error("Local asset read was blocked by remote reads");
        for (auto& result : results)
            if (result.wait_for(std::chrono::seconds(8)) != std::future_status::ready || !result.get())
                throw std::runtime_error("Remote asset read did not complete");
        return 0;
    }
    if (argc == 4 && (std::string_view(argv[1]) == "--foreign-read" ||
                      std::string_view(argv[1]) == "--foreign-read-async")) {
        auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        const auto config = server::IniConfig::load(argv[2]);
        auto events = std::make_shared<SceneCapabilityEvents>(SceneCapabilityEvents::Error{});
        RegionAssetRuntime runtime(config, events, "Foreign asset read test", {}, {}, {}, clients);
        const auto service = runtime.asset_service();
        AssetService::Asset asset;
        const std::string id = argv[3];
        if (std::string_view(argv[1]) == "--foreign-read-async") {
            auto result = std::make_shared<std::promise<AssetService::Asset>>();
            auto future = result->get_future();
            if (!service->get_async(id, 42, [result, id](const auto& requested, std::any sender, auto value) {
                    if (requested == id && std::any_cast<int>(sender) == 42)
                        result->set_value(std::move(value));
                }) || future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
                throw std::runtime_error("Foreign URL read did not deliver its callback");
            asset = future.get();
        } else {
            asset = service->get_asset(id);
        }
        if (asset && asset->data && *asset->data == std::vector<std::uint8_t>{0, 1, 255})
            std::cout << asset->id();
        std::cout << '\n';
        return 0;
    }
    if (argc >= 4 && std::string_view(argv[1]) == "--foreign-exists") {
        auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        const auto config = server::IniConfig::load(argv[2]);
        auto events = std::make_shared<SceneCapabilityEvents>(SceneCapabilityEvents::Error{});
        RegionAssetRuntime runtime(config, events, "Foreign asset existence test", {}, {}, {}, clients);
        const std::vector<std::string> ids(argv + 3, argv + argc);
        for (const bool exists : runtime.asset_service()->assets_exist(ids))
            std::cout << (exists ? '1' : '0');
        std::cout << '\n';
        return 0;
    }
    if (argc == 5 && std::string_view(argv[1]) == "--grid-exists") {
        auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        const auto config = server::IniConfig::load(argv[2]);
        const auto connector = make_grid_asset_service(config, clients, [](auto) { return false; });
        for (const bool exists : connector->assets_exist({argv[3], argv[4]}))
            std::cout << (exists ? '1' : '0');
        std::cout << '\n';
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--managed-grid") {
        auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        const auto config = server::IniConfig::load(argv[2]);
        std::vector<std::function<void()>> queued;
        const auto connector = make_grid_asset_service(config, clients, [&](auto task) {
            queued.push_back(std::move(task));
            return true;
        });
        const framework::Uuid id("91919191-9191-4191-8191-919191919191");
        const auto key = id.str();
        const std::string missing = "81818181-8181-4181-8181-818181818181";
        const std::vector<std::uint8_t> expected{0, 1, 255};
        const auto metadata = connector->get_metadata(key);
        if (!metadata || metadata->id() != key || metadata->name != "Grid write & read" ||
            metadata->content_type() != "application/vnd.ll.lsltext")
            throw std::runtime_error("Managed Robust metadata differed");
        const auto data = connector->get_data(key);
        if (!data || *data != expected)
            throw std::runtime_error("Managed Robust raw data differed");
        if (connector->assets_exist({missing, key, key}) != std::vector<bool>{false, true, true})
            throw std::runtime_error("Managed Robust batch existence differed");
        std::vector<int> delivered;
        const auto callback = [&](const auto &requested, const auto &sender, const auto &asset) {
            if (requested == key && asset && asset->data && *asset->data == expected)
                delivered.push_back(std::any_cast<int>(sender));
            if (std::any_cast<int>(sender) == 42)
                throw std::runtime_error("Fixture callback failure");
        };
        if (!connector->get_async(key, 42, callback) || !connector->get_async(key, 43, callback) ||
            queued.size() != 1)
            throw std::runtime_error("Duplicate grid asset reads were not coalesced");
        queued.front()();
        queued.clear();
        if (!connector->get_async(key, 44, callback) || queued.size() != 1)
            throw std::runtime_error("Completed grid asset read remained queued");
        queued.front()();
        if (delivered != std::vector<int>{42, 43, 44})
            throw std::runtime_error("Managed Robust asynchronous asset read differed");
        if (connector->update_content(key, std::make_shared<std::vector<std::uint8_t>>(3, 7)))
            throw std::runtime_error("Managed Robust unexpectedly allowed an SQLite content update");
        if (!connector->erase(key) || connector->get_asset(key) ||
            connector->assets_exist({key}) != std::vector<bool>{false})
            throw std::runtime_error("Managed Robust did not delete the native asset");
        std::cout << "PASS: native grid connector interoperates with the managed Robust host\n";
        return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "--grid-async-retry") {
        auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        const auto config = server::IniConfig::load(argv[2]);
        std::vector<std::function<void()>> queued;
        bool accept = false;
        const auto connector = make_grid_asset_service(config, clients, [&](auto task) {
            if (!accept)
                return false;
            queued.push_back(std::move(task));
            return true;
        });
        const std::string key = argv[3];
        std::vector<int> delivered;
        const auto callback = [&](const auto& requested, std::any sender, const auto& asset) {
            const auto index = std::any_cast<int>(sender);
            if (requested == key && static_cast<bool>(asset) == (index == 3))
                delivered.push_back(index);
        };
        if (connector->get_async(key, 0, callback) || !delivered.empty())
            throw std::runtime_error("Rejected queue request was accepted");
        accept = true;
        if (!connector->get_async(key, 1, callback) || !connector->get_async(key, 2, callback) ||
            queued.size() != 1)
            throw std::runtime_error("Failed grid read was not coalesced");
        queued.front()();
        queued.clear();
        if (delivered != std::vector<int>{1, 2})
            throw std::runtime_error("Failed grid read did not notify both callers");
        if (!connector->get_async(key, 3, callback) || queued.size() != 1)
            throw std::runtime_error("Grid read could not be retried");
        queued.front()();
        if (delivered != std::vector<int>{1, 2, 3})
            throw std::runtime_error("Grid read retry did not deliver the asset");
        return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "--grid-metadata") {
        auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        const auto config = server::IniConfig::load(argv[2]);
        const auto connector = make_grid_asset_service(config, clients, [](auto) { return false; });
        const auto metadata = connector->get_metadata(argv[3]);
        if (!metadata || !metadata->sha1 || metadata->type != 10)
            throw std::runtime_error("Managed asset metadata or its SHA1 was lost");
        const auto& bytes = *metadata->sha1;
        const auto sha1 = hex({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        std::cout << metadata->creation_date.ticks() << '|' << metadata->creation_date.kind() << '|'
                  << metadata->content_type() << '|' << sha1 << '\n'
                  << serialize_remote_asset_metadata(*metadata);
        return 0;
    }
    if (argc == 4 && (std::string_view(argv[1]) == "--grid-mutate" ||
                      std::string_view(argv[1]) == "--grid-mutation-reply")) {
        auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        const auto config = server::IniConfig::load(argv[2]);
        auto events = std::make_shared<SceneCapabilityEvents>(SceneCapabilityEvents::Error{});
        RegionAssetRuntime runtime(config, events, "Grid asset mutation test", {}, {}, {}, clients);
        const framework::Uuid id(argv[3]);
        const std::vector<std::uint8_t> original{0, 1, 255};
        const auto replacement = std::make_shared<std::vector<std::uint8_t>>(3, 7);
        const auto service = runtime.asset_service();
        if (std::string_view(argv[1]) == "--grid-mutation-reply") {
            std::cout << service->update_content(id.str(), replacement) << service->erase(id.str()) << '\n';
            return 0;
        }
        const auto has_bytes = [&](const std::vector<std::uint8_t> &expected) {
            const auto received = runtime.get(id);
            return received && received->data && *received->data == expected;
        };
        const auto foreign_id = "http://foreign.example/assets/" + id.str();
        if (service->update_content(foreign_id, replacement) || service->erase(foreign_id))
            throw std::runtime_error("Region connector accepted a foreign asset mutation");
        if (!has_bytes(original))
            throw std::runtime_error("Could not read the original grid asset");
        if (service->update_content(id.str(), replacement) || !has_bytes(original))
            throw std::runtime_error("Rejected grid update changed the stored bytes");
        if (!service->update_content(id.str(), replacement) || !has_bytes(*replacement))
            throw std::runtime_error("Accepted grid update did not change the stored bytes");
        if (!service->erase(id.str()) || runtime.get(id))
            throw std::runtime_error("Deleted grid asset was still available");
        return 0;
    }
    if ((argc == 3 && (std::string_view(argv[1]) == "--grid-store" ||
                       std::string_view(argv[1]) == "--grid-store-reply")) ||
        (argc == 4 && std::string_view(argv[1]) == "--foreign-store")) {
        auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        const auto config = server::IniConfig::load(argv[2]);
        auto events = std::make_shared<SceneCapabilityEvents>(SceneCapabilityEvents::Error{});
        RegionAssetRuntime runtime(config, events, "Grid asset write test", {}, {}, {}, clients);
        const framework::Uuid id("91919191-9191-4191-8191-919191919191");
        auto asset = std::make_shared<framework::AssetBase>(id, "Grid write & read", 10, id.str());
        if (argc == 4)
            asset->id(argv[3]);
        asset->data = std::make_shared<std::vector<std::uint8_t>>(
            std::initializer_list<std::uint8_t>{0, 1, 255});
        const auto stored = runtime.store(asset);
        if (argc == 4 || std::string_view(argv[1]) == "--grid-store-reply") {
            std::cout << stored.value_or("") << '\n';
            return 0;
        }
        if (!stored || stored->empty()) {
            std::cout << "rejected\n";
            return 0;
        }
        const auto received = runtime.get(framework::Uuid(*stored));
        if (!received || received->full_id().str() != *stored || !received->data ||
            *received->data != *asset->data || received->name() != asset->name())
            throw std::runtime_error("Stored grid asset could not be read back");
        std::cout << "stored|" << *stored << '\n';
        return 0;
    }
    if (argc >= 4 && argc <= 6 && std::string_view(argv[1]) == "--remote") {
        auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
        const framework::Uuid id(argv[3]);
        AssetService::Asset asset;
        if (argc == 6 && std::string_view(argv[5]) == "--store") {
            auto config = server::IniConfig::load(argv[4]);
            auto events = std::make_shared<SceneCapabilityEvents>(SceneCapabilityEvents::Error{});
            {
                RegionAssetRuntime runtime(config, events, "Foreign asset test", {}, {}, {}, clients);
                asset = runtime.get_foreign(id, argv[2], true);
            }
            RegionAssetRuntime reopened(config, events, "Foreign asset test", {}, {}, {}, clients);
            const auto stored = reopened.get(id);
            if (!asset || !stored || !stored->data || !asset->data || *stored->data != *asset->data)
                throw std::runtime_error("Foreign asset did not survive reopening the local store");
        } else if (argc == 5) {
            auto config = server::IniConfig::load(argv[4]);
            auto events = std::make_shared<SceneCapabilityEvents>(SceneCapabilityEvents::Error{});
            RegionAssetRuntime runtime(config, events, "Foreign asset test", {}, {}, {}, clients,
                                       [url = std::string(argv[2])](auto) { return url; });
            const auto agent = framework::Uuid::random();
            auto listener =
                std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{});
            auto caps = std::make_shared<Caps>(listener, "127.0.0.1", 80, "seed", agent, "Foreign asset test",
                                               [listener] { return listener; });
            events->trigger_register(agent, caps);
            std::shared_ptr<CapabilityPollHandler> handler;
            if (!caps->try_get_poll("GetTexture", handler))
                throw std::runtime_error("Foreign texture capability was not registered");
            auto poll = std::dynamic_pointer_cast<AssetPollHandler>(handler);
            auto request = std::make_shared<AssetPollRequest>();
            request->poll = poll;
            request->request_id = framework::Uuid::random();
            request->request.method = "GET";
            request->request.path = *poll->url;
            request->request.raw_target = request->request.path + "?texture_id=" + id.str();
            struct TestChannel final : network::HttpResponseChannel {
                bool can_send() const override {
                    return true;
                }
                bool is_sending() const override {
                    return false;
                }
                bool is_closed() const override {
                    return false;
                }
                bool complete(network::HttpResponse) override {
                    return true;
                }
                bool complete_raw(network::RawHttpResponse) override {
                    return true;
                }
                int requests_remaining() const override {
                    return 1;
                }
                int idle_timeout_ms() const override {
                    return 5000;
                }
                void idle_timeout_ms(int) override {}
                bool send_chunk(std::shared_ptr<const std::vector<std::uint8_t>>, std::size_t, std::size_t,
                                std::function<void()>) override {
                    return true;
                }
                void finish_response(bool) override {}
                void on_disconnect(std::function<void()>) override {}
                void cancel() override {}
            };
            request->request.response_channel = std::make_shared<TestChannel>();
            poll->receive(request->request_id, request->request);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!poll->has(request->request_id, agent)) {
                if (std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("Foreign texture capability worker did not complete");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto response = poll->events(request->request_id, agent);
            const auto state = server::PollHttpService::response_state(
                std::any_cast<server::PollResponsePtr>(response->get("h")));
            const bool denied = !config.get("HGAssetService", "DisallowImport").empty();
            if (state->status() != (denied ? 404 : 200))
                throw std::runtime_error("Unexpected foreign texture capability status");
            if (denied) {
                std::cout << "missing\n";
                return 0;
            }
            const auto bytes = state->raw_buffer();
            if (!bytes || *bytes != std::vector<std::uint8_t>{0, 1, 255})
                throw std::runtime_error("Unexpected foreign texture capability payload");
            asset = runtime.get_foreign(id, argv[2], false);
            if (!asset)
                throw std::runtime_error("Foreign texture was not cached");
            if (runtime.get_foreign(id, argv[2], false) != asset)
                throw std::runtime_error("Foreign asset did not reuse the region cache");
        } else {
            asset = request_remote_asset(*clients, argv[2], id);
        }
        if (!asset) {
            std::cout << "missing\n";
            return 0;
        }
        std::cout << asset->id() << '|' << asset->name().value_or("") << '|' << int(asset->type()) << '|'
                  << asset->flags() << '|';
        if (asset->data && !asset->data->empty())
            std::cout << hex({reinterpret_cast<const char *>(asset->data->data()), asset->data->size()});
        std::cout << '\n';
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        Context c;
        std::string id, ops;
        int stored, deleted;
        in >> c.mode >> c.exist >> stored >> deleted >> c.fail >> id >> ops;
        c.stored = stored != 0;
        c.deleted = deleted != 0;
        if (c.fail == "-")
            c.fail = "";
        id = unhex(id);
        auto data = std::make_shared<Store>(c);
        StoredAssetService service(data,
                                   {[&](const auto &message) { c.event("warn:" + message); },
                                    [&](const auto &message, const auto &) { c.event("error:" + message); }});
        std::string result;
        for (char op : ops) {
            std::string value = "-";
            try {
                switch (op) {
                case 'G':
                    value = c.name(service.get_asset(id));
                    break;
                case 'C':
                    value = c.name(service.get_cached(id));
                    break;
                case 'M': {
                    auto m = service.get_metadata(id);
                    value = !m ? "N" : m == c.a->metadata() ? "A" : m == c.b->metadata() ? "B" : "?";
                    break;
                }
                case 'D': {
                    auto bytes = service.get_data(id);
                    value = !bytes ? "N" : bytes == c.a->data ? "A" : bytes == c.b->data ? "B" : "?";
                    break;
                }
                case 'A':
                case 'R':
                case 'Q': {
                    AssetService::Retrieved cb;
                    if (op != 'Q')
                        cb = [&](const auto &key, std::any sender, auto asset) {
                            c.event("callback:" + key + ":" + std::any_cast<std::string>(sender) + ":" +
                                    c.name(asset));
                            if (op == 'R')
                                throw std::runtime_error("callback");
                        };
                    value = service.get_async(id, std::string("sender"), cb) ? "1" : "0";
                    break;
                }
                case 'F':
                    service.get(id, "foreign", true, [&](auto asset) { c.event("simple:" + c.name(asset)); });
                    break;
                case 'S':
                case 's':
                    value = service.store(op == 's' ? nullptr : c.a).value_or("N");
                    break;
                case 'U':
                    value = service.update_content(id, {}) ? "1" : "0";
                    break;
                case 'X':
                    value = service.erase(id) ? "1" : "0";
                    break;
                case 'E':
                case 'e': {
                    auto values = service.assets_exist(op == 'e' ? std::vector<std::string>{}
                                                                 : std::vector<std::string>{id, c.a->id()});
                    value = "";
                    for (bool b : values)
                        value += b ? '1' : '0';
                    break;
                }
                case 'T':
                    c.a->metadata({});
                    break;
                }
            } catch (...) {
                value = "EX";
            }
            result += std::string(1, op) + ":" + hex(value) + ";";
        }
        std::cout << result << '|' << hex(c.trace) << '\n';
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << "\n";
    return 1;
}
