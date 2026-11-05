// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/asset_store.hpp"
#include "opensim/services/region_asset_runtime.hpp"
#include "opensim/network/web_http_clients.hpp"
#include "opensim/network/async_http_server.hpp"
#include "opensim/services/get_assets_module.hpp"
#include "opensim/server/http_route_registry.hpp"
#include "opensim/server/poll_http_service.hpp"
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
