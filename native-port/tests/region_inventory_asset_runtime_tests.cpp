// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_inventory_asset_runtime.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/server/http_route_registry.hpp"
#include "opensim/network/osd_value.hpp"
#include <iostream>
#include <thread>
using namespace opensim;
using namespace services;
namespace {
int checks = 0;
void check(bool value, const char *label) {
    ++checks;
    if (!value)
        throw std::runtime_error(label);
}
struct Backend final : SceneCapabilityBackend {
    std::shared_ptr<server::RegistryCapabilityListener> registry;
    explicit Backend(std::shared_ptr<server::RegistryCapabilityListener> r) : registry(std::move(r)) {}
    std::string scene_name() const override {
        return "Script scene";
    }
    std::optional<std::string> region_name() const override {
        return "Scripts";
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
void run(bool expiry) {
    auto registry = std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 19007, 0, {}});
    SceneCapabilities scene(std::make_shared<Backend>(registry));
    auto before = scene.events().on_register_caps.snapshot().size();
    auto agent = framework::Uuid::random(), object = framework::Uuid::random(),
         item = framework::Uuid::random();
    int updates = 0;
    RegionInventoryAssetBackend backend;
    backend.object_exists = [&](auto id) { return id == object; };
    backend.can_edit_inventory = [&](auto id, auto who) { return id == object && who == agent; };
    backend.update = [&](auto who, auto key, auto id, auto bytes) {
        check(who == agent && key == item && id == object, "captured task update identity");
        check(std::string(bytes.begin(), bytes.end()) == "actual script", "actual upload bytes forwarded");
        ++updates;
        return item;
    };
    RegionInventoryAssetRuntime runtime(scene.shared_events(), std::move(backend));
    check(scene.events().on_register_caps.snapshot().size() == before + 1, "subscribed once");
    auto circuit = std::make_shared<framework::AgentCircuitData>();
    circuit->agent_id = agent;
    circuit->circuit_code = 55;
    circuit->caps_path = "script-test";
    scene.capabilities().set_agent_caps_seeds(circuit);
    scene.capabilities().create_caps(agent, 55);
    scene.capabilities().activate_caps(55);
    auto caps = std::dynamic_pointer_cast<Caps>(scene.capabilities().get_caps_for_user(55));
    check(bool(caps), "real caps created");
    auto paths = caps->local_paths();
    for (const auto &aliases : std::vector<std::vector<std::string>>{
             {"UpdateNotecardAgentInventory", "UpdateNotecardTaskInventory"},
             {"UpdateScriptAgent", "UpdateScriptAgentInventory"},
             {"UpdateSettingsAgentInventory", "UpdateSettingsTaskInventory"},
             {"UpdateMaterialAgentInventory", "UpdateMaterialTaskInventory"},
             {"UpdateGestureAgentInventory", "UpdateGestureTaskInventory"}})
        check(paths.contains(aliases[0]) && paths.contains(aliases[1]) &&
                  paths.at(aliases[0]) == paths.at(aliases[1]),
              "exact source alias pair");
    check(paths.contains("UpdateAnimSetAgentInventory"), "source no-op capability registered");
    check(paths.contains("UpdateNotecardAgentInventory") && paths.contains("UpdateNotecardTaskInventory"),
          "both aliases advertised");
    check(paths.at("UpdateNotecardAgentInventory") == paths.at("UpdateNotecardTaskInventory"),
          "aliases share source handler path");
    std::shared_ptr<server::CapabilitySimpleHandler> initial;
    check(registry->routes.try_simple(paths.at("UpdateNotecardAgentInventory"), initial),
          "initial handler registered");
    network::HttpRequest request;
    request.method = "POST";
    request.remote_ip = "127.0.0.1";
    request.body = "<llsd><map><key>item_id</key><uuid>" + item.str() + "</uuid><key>task_id</key><uuid>" +
                   object.str() + "</uuid><key>is_script_running</key><boolean>1</boolean></map></llsd>";
    auto create = [&]() {
        auto response = initial->handle(request);
        check(response.status == 200, "initial status");
        auto value = FetchInventory2Route::deserialize_xml_json(response.body);
        check(network::osd_string(value.get("state")) == "upload", "initial upload state");
        auto url = network::osd_string(value.get("uploader"));
        auto path = url.substr(url.find('/', url.find("://") + 3));
        std::shared_ptr<server::CapabilitySimpleHandler> upload;
        check(registry->routes.try_simple(path, upload), "binary route registered");
        check(runtime.pending_uploads() == 1, "runtime retains upload and timer");
        return std::pair(path, upload);
    };
    auto [path, upload] = create();
    network::HttpRequest binary;
    binary.method = "POST";
    binary.remote_ip = "127.0.0.1";
    binary.body = "actual script";
    if (expiry) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
        while (runtime.pending_uploads() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(runtime.pending_uploads() == 0, "real30-second timer retires upload");
        std::shared_ptr<server::CapabilitySimpleHandler> missing;
        check(!registry->routes.try_simple(path, missing), "expiration removes real route");
        check(upload->handle(binary).status == 500 && updates == 0, "expired upload cannot update scene");
    } else {
        auto response = upload->handle(binary);
        check(response.status == 200 && response.body.find("<key>new_asset</key>") != response.body.npos &&
                  response.body.find("<key>compiled</key>") == response.body.npos,
              "real backend diagnostics returned");
        check(updates == 1 && runtime.pending_uploads() == 0, "one-shot update released ownership");
        check(upload->handle(binary).status == 500 && updates == 1, "duplicate cannot update twice");
        auto another = create();
        auto bad = binary;
        bad.remote_ip = "127.0.0.2";
        check(another.second->handle(bad).status == 401 && runtime.pending_uploads() == 0 && updates == 1,
              "IP mismatch consumes upload before update");
        auto abandoned = create();
        caps->deregister_handlers();
        std::shared_ptr<server::CapabilitySimpleHandler> retained;
        check(registry->routes.try_simple(abandoned.first, retained) && runtime.pending_uploads() == 1,
              "named cap deregistration does not retire child upload");
        runtime.stop();
        runtime.stop();
        check(runtime.pending_uploads() == 0, "stop retires outstanding upload");
        std::shared_ptr<server::CapabilitySimpleHandler> missing;
        check(!registry->routes.try_simple(abandoned.first, missing) &&
                  abandoned.second->handle(binary).status == 500,
              "stop removes real route and invalidates retained handler");
    }
    runtime.stop();
    check(scene.events().on_register_caps.snapshot().size() == before, "stop unsubscribes");
    scene.shutdown();
    check(registry->routes.simple_keys().empty() && registry->routes.poll_keys().empty(),
          "caps and runtime release all routes");
}
}
int main(int argc, char **argv) {
    try {
        run(argc > 1 && std::string_view(argv[1]) == "--expiry");
        std::cout << "PASS: " << checks << " inventory asset registration/upload/lifetime checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
