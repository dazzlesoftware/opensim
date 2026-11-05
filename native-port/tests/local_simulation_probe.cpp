// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/local_simulation.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/network/osd_json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
using namespace opensim;
using network::Value;
std::string hex(std::string_view s) {
    std::string out;
    const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out;
}
std::string decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
    return out;
}
framework::Uuid id(int n) {
    return framework::Uuid("00000000-0000-0000-0000-00000000000" + std::to_string(n));
}
std::string opt(const std::optional<std::string>& s) {
    return s ? hex(*s) : "-";
}
struct Context {
    services::LocalSimulationConnector* owner = nullptr;
    Value command;
    std::string trace;
    bool mutated = false;
    std::map<std::string, std::shared_ptr<services::LocalSimulationScene>> scenes;
    std::shared_ptr<framework::AgentData> data;
    std::shared_ptr<framework::AgentPosition> position;
    std::shared_ptr<framework::AgentCircuitData> circuit;
    void log(std::string op, const std::string& label, const std::string& text = "") {
        trace += op + ":" + label + ":" + text + ";";
        if (network::osd_string(command.get("failure")) == op &&
            (!command.contains("fail_scene") || network::osd_string(command.get("fail_scene")) == label))
            throw std::runtime_error("controlled callback failure");
    }
    void mutate(const std::string& label);
};
struct Object : services::SimulationObject {
    framework::Uuid object_uuid() const override {
        return framework::Uuid("00000000-0000-0000-0000-000000000007");
    }
    Context& ctx;
    std::string label;
    Object(Context& c, std::string s) : ctx(c), label(std::move(s)) {
    }
    std::shared_ptr<services::SimulationObject> clone_for_new_scene() override {
        ctx.log("clone", label);
        if (network::osd_boolean(ctx.command.get("null_clone")))
            return {};
        return std::make_shared<Object>(ctx, "clone");
    }
    std::optional<std::string> state_snapshot() override {
        ctx.log("snapshot", label);
        return network::osd_boolean(ctx.command.get("null_snapshot")) ? std::nullopt
                                                                      : std::optional<std::string>("scripts");
    }
    void set_state(const std::optional<std::string>& state, framework::AgentTransferScene* base) override {
        auto scene = dynamic_cast<services::LocalSimulationScene*>(base);
        ctx.log("set-state", label, opt(state) + ":" + scene->region_name().value_or(""));
    }
    std::string to_xml2() override {
        return "xml";
    }
    std::string extra_to_xml_string() override {
        return "extra";
    }
    void extra_from_xml_string(const std::string&) override {
    }
    bool has_group_changed() const override {
        return false;
    }
    void has_group_changed(bool) override {
    }
};
struct Arrival : services::SimulationAgentArrival {
    Context& ctx;
    std::string label;
    Arrival(Context& c, std::string s) : ctx(c), label(std::move(s)) {
    }
    void agent_arrived_at_destination(const framework::Uuid& a) override {
        ctx.log("arrival", label, a.str());
    }
};
struct Scene : services::LocalSimulationScene {
    Context& ctx;
    std::string label;
    framework::Uuid key;
    std::uint32_t x = 256, y = 256;
    std::uint64_t handle;
    bool registered = false;
    Scene(Context& c, std::string s, int n)
        : ctx(c), label(std::move(s)), key(id(n)), handle(static_cast<std::uint64_t>(n) * 256) {
    }
    framework::Uuid region_id() const override {
        return key;
    }
    std::optional<std::string> region_name() const override {
        return label;
    }
    std::uint32_t region_size_x() const override {
        return x;
    }
    std::uint32_t region_size_y() const override {
        return y;
    }
    std::uint64_t region_handle() const override {
        return handle;
    }
    void register_simulation_service(services::LocalSimulationConnector* s) override {
        ctx.log("register", label, s == ctx.owner ? "1" : "0");
        registered = true;
    }
    void unregister_simulation_service(services::LocalSimulationConnector* s) override {
        ctx.log("unregister", label, s == ctx.owner ? "1" : "0");
        registered = false;
    }
    bool new_user_connection(const std::shared_ptr<framework::AgentCircuitData>& a, std::uint32_t flags,
                             const services::GridRegion* source, std::string& reason) override {
        ctx.log("connect", label,
                (a ? a->agent_id.str() : "-") + ":" + std::to_string(flags) + ":" +
                    (source ? source->id.str() : "-"));
        reason = label + ":reason";
        return !network::osd_boolean(ctx.command.get("deny"));
    }
    bool incoming_update_child_agent(const std::shared_ptr<framework::AgentData>& a) override {
        ctx.log("data", label, a ? a->agent_id.str() : "-");
        return !network::osd_boolean(ctx.command.get("deny"));
    }
    bool incoming_update_child_agent(const std::shared_ptr<framework::AgentPosition>& a) override {
        ctx.log("position", label,
                a ? a->agent_id.str() + ":" + std::to_string(static_cast<int>(a->far)) : "-");
        if (a && network::osd_boolean(ctx.command.get("mutate_data")))
            a->far += 1;
        if (network::osd_boolean(ctx.command.get("reset_data")))
            ctx.position.reset();
        ctx.mutate(label);
        return !network::osd_boolean(ctx.command.get("deny"));
    }
    bool query_access(const framework::Uuid& a, const std::optional<std::string>& home, bool teleport,
                      const framework::Vector3& p, std::vector<framework::Uuid>* features,
                      std::string& reason) override {
        ctx.log("query", label,
                a.str() + ":" + opt(home) + ":" + (teleport ? "1" : "0") + ":" +
                    framework::vector3_string(p) + ":" + (features ? std::to_string(features->size()) : "-"));
        if (features)
            features->push_back(id(8));
        reason = label + ":reason";
        return !network::osd_boolean(ctx.command.get("deny"));
    }
    std::shared_ptr<services::SimulationAgentArrival> entity_transfer_module() override {
        ctx.log("entity", label);
        if (network::osd_boolean(ctx.command.get("null_transfer")))
            return {};
        return std::make_shared<Arrival>(ctx, label);
    }
    bool close_agent(const framework::Uuid& a, bool force, const std::optional<std::string>& auth) override {
        ctx.log("close", label, a.str() + ":" + (force ? "1" : "0") + ":" + opt(auth));
        return false;
    }
    bool incoming_create_object(const framework::Vector3& p,
                                const std::shared_ptr<services::SimulationObject>& o) override {
        auto object = std::dynamic_pointer_cast<Object>(o);
        ctx.log("object", label, framework::vector3_string(p) + ":" + (object ? object->label : "-"));
        return !network::osd_boolean(ctx.command.get("deny"));
    }
    std::shared_ptr<framework::AgentTransferObject> deserialize_object(const std::string&) override {
        return {};
    }
};
void Context::mutate(const std::string& label) {
    if (mutated || !command.contains("mutation"))
        return;
    mutated = true;
    auto mode = network::osd_string(command.get("mutation"));
    if (mode == "add")
        owner->init(scenes.at("C"));
    else if (mode == "remove_self")
        owner->remove_scene(scenes.at(label));
    else if (mode == "remove_other")
        owner->remove_scene(scenes.at("B"));
    else if (mode == "duplicate")
        owner->init(scenes.at("D"));
    else if (mode == "remove_all") {
        for (auto& [key, scene] : scenes)
            owner->remove_scene(scene);
    }
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        Context ctx;
        ctx.command = network::parse_osd_json(decode(line));
        services::LocalSimulationConnector owner;
        ctx.owner = &owner;
        for (auto [name, n] : {std::pair{"A", 1}, {"B", 2}, {"C", 3}, {"D", 1}})
            ctx.scenes[name] = std::make_shared<Scene>(ctx, name, n);
        ctx.data = std::make_shared<framework::AgentData>();
        ctx.data->agent_id = id(7);
        ctx.position = std::make_shared<framework::AgentPosition>();
        ctx.position->agent_id = id(7);
        ctx.position->far = 10;
        ctx.circuit = std::make_shared<framework::AgentCircuitData>();
        ctx.circuit->agent_id = id(7);
        std::vector<framework::Uuid> features{id(4), id(5)};
        std::string results, reason = "before";
        for (const auto& op : ctx.command.get("ops").array) {
            auto method = network::osd_string(op.get("op"));
            auto label = network::osd_string(op.get("scene"));
            auto scene = op.contains("scene") && !label.empty() ? ctx.scenes.at(label) : nullptr;
            auto d = services::GridRegion();
            d.id = id(op.contains("id") ? network::osd_integer(op.get("id")) : 1);
            if (op.contains("name") && op.get("name").kind == Value::Kind::Undefined)
                d.name.reset();
            else
                d.name = op.contains("name") ? network::osd_string(op.get("name")) : "Destination";
            auto dest = network::osd_boolean(op.get("null_destination")) ? nullptr : &d;
            framework::EntityTransferContext context;
            if (op.contains("version"))
                context.outbound_version = static_cast<float>(network::osd_real(op.get("version")));
            auto cp = network::osd_boolean(op.get("null_context")) ? nullptr : &context;
            std::string result = "OK";
            try {
                if (method == "initialise") {
                    if (network::osd_boolean(op.get("null_config")))
                        owner.initialise(static_cast<const server::IniConfig*>(nullptr));
                    else {
                        server::IniConfig config;
                        if (op.contains("name")) {
                            auto path = std::filesystem::temp_directory_path() /
                                        ("opensim-local-probe-" + framework::random_uuid() + ".ini");
                            {
                                std::ofstream f(path);
                                f << "[Modules]\nSimulationServices = " << network::osd_string(op.get("name"))
                                  << "\n";
                            }
                            config = server::IniConfig::load(path);
                            std::filesystem::remove(path);
                        }
                        owner.initialise(config);
                    }
                } else if (method == "init")
                    owner.init(scene);
                else if (method == "remove")
                    owner.remove_scene(scene);
                else if (method == "add")
                    owner.add_region(scene);
                else if (method == "remove_region")
                    owner.remove_region(scene);
                else if (method == "close")
                    owner.close();
                else if (method == "post")
                    owner.post_initialise();
                else if (method == "loaded")
                    owner.region_loaded(scene);
                else if (method == "service_init")
                    owner.initialise_service(nullptr);
                else if (method == "get") {
                    auto found = std::dynamic_pointer_cast<Scene>(owner.get_scene(d.id));
                    result = found ? found->label : "-";
                } else if (method == "inner")
                    result = owner.get_inner_service() == &owner && owner.replaceable_interface() == nullptr
                                 ? "1"
                                 : "0";
                else if (method == "local_id")
                    result = owner.is_local_region(d.id) ? "1" : "0";
                else if (method == "local_handle")
                    result = owner.is_local_region(
                                 static_cast<std::uint64_t>(network::osd_integer(op.get("handle"))))
                                 ? "1"
                                 : "0";
                else if (method == "resize") {
                    auto s = std::dynamic_pointer_cast<Scene>(scene);
                    s->x = static_cast<std::uint32_t>(network::osd_integer(op.get("x")));
                    s->y = static_cast<std::uint32_t>(network::osd_integer(op.get("y")));
                } else if (method == "rekey")
                    std::dynamic_pointer_cast<Scene>(scene)->key = d.id;
                else if (method == "create") {
                    services::GridRegion source;
                    source.id = id(9);
                    result =
                        owner.create_agent(network::osd_boolean(op.get("source")) ? &source : nullptr, dest,
                                           network::osd_boolean(op.get("null_data")) ? nullptr : ctx.circuit,
                                           0xffffffffu, cp, reason)
                            ? "1"
                            : "0";
                } else if (method == "data")
                    result = owner.update_agent(
                                 dest, network::osd_boolean(op.get("null_data")) ? nullptr : ctx.data, cp)
                                 ? "1"
                                 : "0";
                else if (method == "position")
                    result = owner.update_agent(
                                 dest, network::osd_boolean(op.get("null_data")) ? nullptr : ctx.position)
                                 ? "1"
                                 : "0";
                else if (method == "query") {
                    auto home = op.contains("home") && op.get("home").kind != Value::Kind::Undefined
                                    ? std::optional<std::string>(network::osd_string(op.get("home")))
                                    : std::nullopt;
                    result =
                        owner.query_access(
                            dest, id(7), home, !network::osd_boolean(op.get("cross")), {1.25f, 2, 30},
                            network::osd_boolean(op.get("null_features")) ? nullptr : &features, cp, reason)
                            ? "1"
                            : "0";
                } else if (method == "release")
                    result = owner.release_agent(d.id, id(7), "") ? "1" : "0";
                else if (method == "close_agent")
                    result = owner.close_agent(dest, id(7),
                                               op.contains("auth") ? std::optional<std::string>(
                                                                         network::osd_string(op.get("auth")))
                                                                   : std::nullopt)
                                 ? "1"
                                 : "0";
                else if (method == "object")
                    result = owner.create_object(dest, {1.25f, 2, 30},
                                                 network::osd_boolean(op.get("null_object"))
                                                     ? nullptr
                                                     : std::make_shared<Object>(ctx, "original"),
                                                 network::osd_boolean(op.get("local")))
                                 ? "1"
                                 : "0";
            } catch (const std::exception&) {
                result = "EX";
            }
            results += result + ",";
        }
        std::string registered;
        for (auto& [key, scene] : ctx.scenes)
            registered += key + ":" + (std::dynamic_pointer_cast<Scene>(scene)->registered ? "1" : "0") + ";";
        std::string fs;
        for (auto& f : features)
            fs += f.str() + ",";
        std::cout << results << "|" << hex(reason) << "|" << hex(ctx.trace) << "|" << owner.enabled() << "|"
                  << owner.scene_count() << "|" << registered << "|" << fs << "\n";
    }
}
