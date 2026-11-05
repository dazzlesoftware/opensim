// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/object_handler.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/vector_text.hpp"
#include <iostream>
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
std::string unhex(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return out;
}
struct Context {
    Value command;
    std::string trace;
    void log(const std::string& op, const std::string& value = "") {
        trace += op + ":" + hex(value) + ";";
        if (network::osd_string(command.get("failure")) == op)
            throw std::runtime_error(op);
    }
};
struct Object : services::SimulationObject {
    framework::Uuid object_uuid() const override {
        return framework::Uuid("00000000-0000-0000-0000-000000000007");
    }
    Context& ctx;
    explicit Object(Context& c) : ctx(c) {
    }
    std::string to_xml2() override {
        return "";
    }
    std::string extra_to_xml_string() override {
        return "";
    }
    void extra_from_xml_string(const std::string& s) override {
        ctx.log("extra", s);
    }
    bool has_group_changed() const override {
        return false;
    }
    void has_group_changed(bool b) override {
        ctx.log("modified", b ? "1" : "0");
    }
    std::shared_ptr<services::SimulationObject> clone_for_new_scene() override {
        return {};
    }
    std::optional<std::string> state_snapshot() override {
        return {};
    }
    void set_state(const std::optional<std::string>& s, framework::AgentTransferScene*) override {
        ctx.log("state", s.value_or(""));
    }
};
struct Scene : framework::AgentTransferScene {
    Context& ctx;
    explicit Scene(Context& c) : ctx(c) {
    }
    std::shared_ptr<framework::AgentTransferObject> deserialize_object(const std::string& s) override {
        ctx.log("deserialize", s);
        return network::osd_boolean(ctx.command.get("null_object")) ? nullptr : std::make_shared<Object>(ctx);
    }
    bool allow_script_crossings() const override {
        ctx.log("allow");
        return network::osd_boolean(ctx.command.get("allow"));
    }
};
struct Service : services::ISimulationObjects {
    Context& ctx;
    explicit Service(Context& c) : ctx(c) {
    }
    std::shared_ptr<framework::AgentTransferScene> get_scene(const framework::Uuid& id) override {
        ctx.log("scene", id.str());
        return network::osd_boolean(ctx.command.get("null_scene")) ? nullptr : std::make_shared<Scene>(ctx);
    }
    bool create_object(services::GridRegion* d, const framework::Vector3& p,
                       std::shared_ptr<services::SimulationObject>, bool local) override {
        ctx.log("create", d->id.str() + ":" + std::to_string(d->x) + ":" + std::to_string(d->y) + ":" +
                              d->name.value_or("") + ":" + framework::vector3_string(p) + ":" +
                              (local ? "1" : "0"));
        return !network::osd_boolean(ctx.command.get("deny"));
    }
};
struct Override : server::ObjectHandler {
    Context& ctx;
    Override(services::ISimulationObjects* s, Context& c) : ObjectHandler(s), ctx(c) {
    }
    bool create_object(services::GridRegion& d, const framework::Vector3& p,
                       const std::shared_ptr<services::SimulationObject>&) override {
        ctx.log("override", d.id.str() + ":" + framework::vector3_string(p));
        return !network::osd_boolean(ctx.command.get("deny"));
    }
};
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        Context ctx{network::parse_osd_json(unhex(line))};
        Service service(ctx);
        auto ptr = network::osd_boolean(ctx.command.get("null_service")) ? nullptr : &service;
        std::unique_ptr<server::ObjectHandler> handler;
        if (network::osd_boolean(ctx.command.get("override")))
            handler = std::make_unique<Override>(ptr, ctx);
        else
            handler = std::make_unique<server::ObjectHandler>(ptr);
        network::HttpResponse response;
        response.status = 211;
        response.body = "before";
        response.content_type = "before-type";
        response.keep_alive = true;
        bool failed = false;
        try {
            if (network::osd_boolean(ctx.command.get("core")))
                handler->post_object(ctx.command.get("args"), response);
            else {
                network::HttpRequest request;
                request.method = network::osd_string(ctx.command.get("method"));
                request.path = "/object/ignored";
                request.body = ctx.command.contains("body")
                                   ? network::osd_string(ctx.command.get("body"))
                                   : network::osd_json(ctx.command.get("args"), true);
                if (network::osd_boolean(ctx.command.get("shard")))
                    request.headers["x-secondlife-shard"] = "";
                handler->process_request(request, response);
            }
        } catch (const std::exception&) {
            failed = true;
        }
        std::cout << failed << "|" << response.status << "|" << response.keep_alive << "|"
                  << hex(response.body) << "|" << hex(response.content_type) << "|" << hex(ctx.trace) << "\n";
    }
}
