// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/local_simulation.hpp"
#include "opensim/server/agent_access.hpp"
#include "opensim/server/object_handler.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/network/osd_json.hpp"
#include <future>
#include <iostream>
using namespace opensim;
framework::Uuid id(int n) {
    return framework::Uuid("00000000-0000-0000-0000-00000000000" + std::to_string(n));
}
#include "scene_access_http_backend.hpp"
#include "scene_admission_http_backend.hpp"
struct Object : services::SimulationObject {
    framework::Uuid object_uuid() const override {
        return framework::Uuid("00000000-0000-0000-0000-000000000007");
    }
    std::string xml, extra, state;
    bool changed = false;
    explicit Object(std::string text) : xml(std::move(text)) {
    }
    std::string to_xml2() override {
        return xml;
    }
    std::string extra_to_xml_string() override {
        return extra;
    }
    void extra_from_xml_string(const std::string& value) override {
        if (value == "throw")
            throw std::runtime_error("extra");
        extra = value;
    }
    bool has_group_changed() const override {
        return changed;
    }
    void has_group_changed(bool value) override {
        changed = value;
    }
    std::shared_ptr<services::SimulationObject> clone_for_new_scene() override {
        return std::make_shared<Object>(*this);
    }
    std::optional<std::string> state_snapshot() override {
        return state;
    }
    void set_state(const std::optional<std::string>& value, framework::AgentTransferScene*) override {
        if (value == "throw")
            throw std::runtime_error("state");
        state = value.value_or("");
    }
};
struct Scene : services::LocalSimulationScene,
               services::SimulationAgentArrival,
               std::enable_shared_from_this<Scene> {
    int number;
    std::mutex mutex;
    bool registered = false;
    framework::AgentCircuitManager circuits;
    std::unique_ptr<HttpAccessBackend> access_backend;
    std::unique_ptr<services::SceneAccess> access_policy;
    std::unique_ptr<HttpAdmissionBackend> admission_backend;
    std::unique_ptr<services::SceneAdmission> admission;
    void configure_access(const server::IniConfig& config) {
        if (config.boolean("Access", "Enabled", false) || config.boolean("Admission", "Enabled", false)) {
            access_backend = std::make_unique<HttpAccessBackend>(number, circuits, config);
            access_policy = std::make_unique<services::SceneAccess>(*access_backend);
            if (config.boolean("Admission", "Enabled", false)) {
                admission_backend =
                    std::make_unique<HttpAdmissionBackend>(*access_backend, *access_policy, config);
                admission = std::make_unique<services::SceneAdmission>(*admission_backend);
            }
        }
    }
    int queries = 0, created = 0, data_updates = 0, positions = 0, closed = 0, arrived = 0, deserialized = 0,
        objects = 0;
    std::shared_ptr<Object> last_object;
    framework::Vector3 object_position;
    std::optional<std::string> last_auth;
    framework::Vector3 last_position;
    explicit Scene(int n) : number(n) {
    }
    framework::Uuid region_id() const override {
        return id(number);
    }
    std::optional<std::string> region_name() const override {
        return "Scene" + std::to_string(number);
    }
    std::uint32_t region_size_x() const override {
        return 256;
    }
    std::uint32_t region_size_y() const override {
        return 256;
    }
    std::uint64_t region_handle() const override {
        return static_cast<std::uint64_t>(number) * 256;
    }
    void register_simulation_service(services::LocalSimulationConnector*) override {
        registered = true;
    }
    void unregister_simulation_service(services::LocalSimulationConnector*) override {
        registered = false;
    }
    bool new_user_connection(const std::shared_ptr<framework::AgentCircuitData>& circuit, std::uint32_t flags,
                             const services::GridRegion* source, std::string& reason) override {
        std::lock_guard lock(mutex);
        ++created;
        if (admission)
            return admission->new_user_connection(circuit, flags, source, reason);
        if (!circuit || circuit->agent_id == id(9)) {
            reason = "controlled rejection";
            return false;
        }
        circuits.add_new_circuit(circuit);
        reason = "";
        return true;
    }
    bool incoming_update_child_agent(const std::shared_ptr<framework::AgentData>& data) override {
        std::lock_guard lock(mutex);
        ++data_updates;
        if (data)
            last_position = data->position;
        return number == 1;
    }
    bool incoming_update_child_agent(const std::shared_ptr<framework::AgentPosition>& data) override {
        std::lock_guard lock(mutex);
        ++positions;
        if (data) {
            last_position = data->position;
            auto circuit = circuits.get_agent_circuit_data(data->agent_id);
            if (circuit)
                circuit->start_position = data->position;
        }
        return number == 1;
    }
    bool query_access(const framework::Uuid& agent, const std::optional<std::string>& home, bool teleport,
                      const framework::Vector3& position, std::vector<framework::Uuid>* features,
                      std::string& reason) override {
        std::lock_guard lock(mutex);
        ++queries;
        if (access_policy)
            return access_policy->query_access(agent, home, teleport, position, features, reason);
        if (features)
            features->push_back(id(8));
        reason = agent == id(9) ? "controlled rejection" : "";
        return agent != id(9);
    }
    std::shared_ptr<services::SimulationAgentArrival> entity_transfer_module() override {
        return shared_from_this();
    }
    void agent_arrived_at_destination(const framework::Uuid&) override {
        std::lock_guard lock(mutex);
        ++arrived;
    }
    bool close_agent(const framework::Uuid& agent, bool, const std::optional<std::string>& auth) override {
        std::lock_guard lock(mutex);
        ++closed;
        last_auth = auth;
        if (admission_backend && admission_backend->capabilities) {
            if (auto old = circuits.get_agent_circuit_data(agent))
                admission_backend->capabilities->remove_caps(agent, old->circuit_code);
        }
        circuits.remove_circuit(agent);
        return false;
    }
    bool allow_script_crossings() const override {
        return number == 1;
    }
    bool incoming_create_object(const framework::Vector3& position,
                                const std::shared_ptr<services::SimulationObject>& object) override {
        std::lock_guard lock(mutex);
        ++objects;
        last_object = std::dynamic_pointer_cast<Object>(object);
        object_position = position;
        return number == 1;
    }
    std::shared_ptr<framework::AgentTransferObject> deserialize_object(const std::string& xml) override {
        std::lock_guard lock(mutex);
        ++deserialized;
        if (xml == "throw")
            throw std::runtime_error("controlled deserializer");
        return std::make_shared<Object>(xml);
    }
    network::Value stats() {
        std::lock_guard lock(mutex);
        auto value = network::Value::object({});
        value.set("registered", network::Value(registered));
        if (admission_backend) {
            std::string trace;
            for (const auto& event : admission_backend->events) {
                if (!trace.empty())
                    trace += ",";
                trace += event;
            }
            value.set("admission_trace", trace);
            value.set("activated", network::Value(admission_backend->activated));
            if (auto module = admission_backend->capabilities) {
                value.set("caps_objects", network::Value(static_cast<std::int32_t>(module->caps_count())));
                value.set("caps_created", network::Value(admission_backend->caps_created));
                value.set("caps_registered", network::Value(admission_backend->caps_registered));
                value.set("caps_deregistered", network::Value(admission_backend->caps_deregistered));
                value.set("caps_disposed", network::Value(admission_backend->caps_disposed));
                value.set("handlers_deregistered", network::Value(admission_backend->handlers_deregistered));
                if (auto incoming = admission_backend->incoming) {
                    auto seeds = module->get_children_seeds(incoming->agent_id);
                    value.set("caps_children",
                              network::Value(seeds ? static_cast<std::int32_t>(seeds->size()) : -1));
                    value.set("stored_caps_path", module->get_caps_path(incoming->agent_id).value_or(""));
                    auto object = std::dynamic_pointer_cast<HttpCapabilityObject>(
                        module->get_caps_for_user(incoming->circuit_code));
                    value.set("caps_active", network::Value(object && object->active));
                }
            }
            if (auto actor = admission_backend->actor) {
                value.set("presence_state", network::Value(static_cast<std::int32_t>(actor->state())));
                value.set("presence_flags", network::Value(static_cast<std::int32_t>(actor->flags)));
                value.set("presence_not_close", network::Value(actor->not_close));
                value.set("presence_adjustments", network::Value(actor->adjustments));
            }
        }
        if (access_backend) {
            value.set("access_existing", network::Value(access_backend->last_existing));
            value.set("access_first", access_backend->first);
            value.set("access_last", access_backend->last);
        }
        for (auto [name, count] : {std::pair{"queries", queries},
                                   {"created", created},
                                   {"data", data_updates},
                                   {"positions", positions},
                                   {"closed", closed},
                                   {"arrived", arrived},
                                   {"deserialized", deserialized},
                                   {"objects", objects}})
            value.set(name, network::Value(count));
        if (last_object) {
            value.set("object_xml", last_object->xml);
            value.set("object_extra", last_object->extra.size() > 4096 ? std::string{} : last_object->extra);
            value.set("object_extra_size",
                      network::Value(static_cast<std::int32_t>(last_object->extra.size())));
            value.set("object_extra_md5", framework::md5(last_object->extra));
            value.set("object_state", last_object->state);
            value.set("object_modified", network::Value(last_object->changed));
            value.set("object_position", framework::vector3_string(object_position));
        }
        value.set("position", framework::vector3_string(last_position));
        value.set("auth", last_auth.value_or(""));
        auto values = circuits.get_agent_circuits();
        value.set("circuits", network::Value(static_cast<std::int32_t>(values.size())));
        if (!values.empty()) {
            auto circuit = values.begin()->second;
            value.set("authenticated",
                      network::Value(circuits
                                         .authenticate_session(circuit->session_id, circuit->agent_id,
                                                               circuit->circuit_code)
                                         .authorised));
            value.set("circuit_position", framework::vector3_string(circuit->start_position));
            value.set("circuit_flags", network::Value(static_cast<std::int32_t>(circuit->teleport_flags)));
            value.set("caps_path", circuit->caps_path.value_or(""));
        }
        return value;
    }
};
int main(int argc, char** argv) {
    try {
        if (argc < 2)
            throw std::invalid_argument("Module config required");
        auto service = std::make_shared<services::LocalSimulationConnector>();
        auto config = server::IniConfig::load(argv[1]);
        service->initialise(config);
        auto a = std::make_shared<Scene>(1), b = std::make_shared<Scene>(2);
        a->configure_access(config);
        b->configure_access(config);
        service->add_region(a);
        service->add_region(b);
        std::mutex workers_mutex;
        std::vector<std::future<void>> workers;
        server::AgentDeleteHandler lifecycle(service, [&](std::function<void()> task, const std::string&) {
            std::lock_guard lock(workers_mutex);
            workers.push_back(std::async(std::launch::async, [task = std::move(task)] {
                try {
                    task();
                } catch (const std::exception&) {
                }
            }));
        });
        server::ObjectHandler objects(service.get());
        server::AgentUpdateHandler updates(service.get());
        server::AgentAccessHandler access(service.get(), &lifecycle, &updates);
        network::HttpServerOptions options;
        options.port = 0;
        options.custom_methods = {"QUERYACCESS"};
        options.decompress_request_body = false;
        network::HttpServer server(options, [&](const network::HttpRequest& request) {
            network::HttpResponse response;
            response.content_type = "application/json";
            if (request.path == "/ready") {
                response.status = 503;
                response.body = "{\"simulation_ready\":false}";
                return response;
            }
            if (request.path == "/stats") {
                auto value = network::Value::object({});
                value.set("enabled", network::Value(service->enabled()));
                value.set("scenes", network::Value(static_cast<std::int32_t>(service->scene_count())));
                value.set("a", a->stats());
                value.set("b", b->stats());
                response.body = network::osd_json(value, true);
                return response;
            }
            if (request.path.starts_with("/object"))
                objects.process_request(request, response);
            else
                access.process_admission(request, response);
            return response;
        });
        server.start([](int port) { std::cout << "LISTENING http://127.0.0.1:" << port << std::endl; });
        std::string command;
        std::getline(std::cin, command);
        server.stop();
        {
            std::lock_guard lock(workers_mutex);
            for (auto& worker : workers)
                worker.get();
        }
        service->remove_region(a);
        service->remove_region(b);
        service->close();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what();
        return 1;
    }
}
