// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/scene_client_verification.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include <iostream>
using namespace opensim;
using network::Value;
using framework::Uuid;
std::string hex(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string unhex(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return out;
}
Uuid id(int n) {
    return Uuid("00000000-0000-0000-0000-00000000000" + std::to_string(n));
}
struct Backend;
struct Context {
    Value command;
    Backend* owner = nullptr;
    std::string trace;
    std::optional<std::string> region = "Region";
    int gets = 0, sleeps = 0, logouts = 0, closes = 0;
    bool mutated = false, null_handler = false;
    bool flag(const char* key, bool fallback = false) const {
        return command.contains(key) ? network::osd_boolean(command.get(key)) : fallback;
    }
    int number(const char* key, int fallback = 0) const {
        return command.contains(key) ? network::osd_integer(command.get(key)) : fallback;
    }
    std::string text(const char* key, const std::string& fallback = "") const {
        return command.contains(key) ? network::osd_string(command.get(key)) : fallback;
    }
    void log(const std::string& op, const std::string& detail = "");
};
struct Client : services::VerifiedSceneClient {
    Context& ctx;
    explicit Client(Context& c) : ctx(c) {
    }
    Uuid session_id() const override {
        ctx.log("session");
        return id(3);
    }
};
struct Presence : services::VerifiedScenePresence {
    Context& ctx;
    Uuid agent = id(1);
    std::shared_ptr<Client> client;
    explicit Presence(Context& c) : ctx(c), client(std::make_shared<Client>(c)) {
    }
    std::shared_ptr<services::VerifiedSceneClient> controlling_client() const override {
        ctx.log("client");
        return ctx.flag("client", true) ? client : nullptr;
    }
    Uuid agent_id() const override {
        ctx.log("agent-id");
        return agent;
    }
};
struct Verifier : services::UserAgentVerificationModule {
    Context& ctx;
    explicit Verifier(Context& c) : ctx(c) {
    }
    bool verify_client(const std::shared_ptr<framework::AgentCircuitData>& circuit,
                       const std::string& address) override;
};
struct Service : services::IPresenceService {
    Context& ctx;
    explicit Service(Context& c) : ctx(c) {
    }
    bool login(const std::string&, const Uuid&, const Uuid&) override {
        return false;
    }
    bool logout(const Uuid& session) override {
        ctx.log("logout", session.str());
        ++ctx.logouts;
        return ctx.flag("logout_result", true);
    }
    bool logout_region(const Uuid&) override {
        return false;
    }
    bool report(const Uuid&, const Uuid&) override {
        return false;
    }
    std::optional<services::PresenceInfo> agent(const Uuid&) override {
        return {};
    }
    std::vector<services::PresenceInfo> agents(const std::vector<std::string>&) override {
        return {};
    }
};
struct Backend : services::SceneClientVerificationBackend {
    Context& ctx;
    mutable framework::AgentCircuitManager manager;
    std::shared_ptr<framework::AgentCircuitData> input;
    std::shared_ptr<Presence> actor;
    std::shared_ptr<Verifier> verifier;
    Service service;
    explicit Backend(Context& c)
        : ctx(c), actor(std::make_shared<Presence>(c)), verifier(std::make_shared<Verifier>(c)), service(c) {
        ctx.owner = this;
        input = std::make_shared<framework::AgentCircuitData>();
        input->agent_id = id(1);
        input->session_id = id(2);
        input->circuit_code = 123;
        input->first_name = ctx.command.contains("first")
                                ? (ctx.command.get("first").kind == Value::Kind::Undefined
                                       ? std::nullopt
                                       : std::optional<std::string>(ctx.text("first")))
                                : std::optional<std::string>("First");
        input->last_name = ctx.command.contains("last")
                               ? (ctx.command.get("last").kind == Value::Kind::Undefined
                                      ? std::nullopt
                                      : std::optional<std::string>(ctx.text("last")))
                               : std::optional<std::string>("Last");
        input->teleport_flags = network::osd_unsigned(ctx.command.get("flags"));
        if (!ctx.flag("missing"))
            manager.add_new_circuit(input);
        ctx.null_handler = ctx.flag("null_handler");
        if (ctx.command.contains("region"))
            ctx.region = ctx.command.get("region").kind == Value::Kind::Undefined
                             ? std::nullopt
                             : std::optional<std::string>(ctx.text("region"));
    }
    framework::AgentCircuitManager* authenticate_handler() const override {
        return ctx.null_handler ? nullptr : &manager;
    }
    std::shared_ptr<framework::AgentCircuitData> agent_circuit(const Uuid& agent) override {
        auto handler = authenticate_handler();
        if (!handler)
            throw std::runtime_error("null handler");
        ctx.log("get", agent.str());
        return handler->get_agent_circuit_data(agent);
    }
    void remove_circuit(const Uuid& agent) override {
        auto handler = authenticate_handler();
        if (!handler)
            throw std::runtime_error("null handler");
        ctx.log("remove", agent.str());
        handler->remove_circuit(agent);
    }
    std::optional<std::string> region_name() const override {
        if (ctx.flag("null_region"))
            throw std::runtime_error("null region");
        ctx.log("region");
        return ctx.region;
    }
    std::shared_ptr<services::UserAgentVerificationModule> user_verification_module() override {
        ctx.log("module");
        return ctx.flag("module", true) ? verifier : nullptr;
    }
    std::shared_ptr<services::VerifiedScenePresence> scene_presence(const Uuid& agent) override {
        ctx.log("presence", agent.str());
        ++ctx.gets;
        return ctx.flag("presence", true) && ctx.gets >= ctx.number("after", 1) ? actor : nullptr;
    }
    services::IPresenceService* presence_service() override {
        ctx.log("presence-service");
        return ctx.flag("service", true) ? &service : nullptr;
    }
    void close_agent(const Uuid& agent, bool force) override {
        ctx.log("close", agent.str() + ":" + (force ? "1" : "0"));
        ++ctx.closes;
    }
    void sleep(int milliseconds) override {
        ctx.log("sleep", std::to_string(milliseconds));
        ++ctx.sleeps;
    }
    void debug(const std::string& message) override {
        ctx.log("debug", message);
    }
    void warn(const std::string& message) override {
        ctx.log("warn", message);
    }
    void cleanup_exception(const std::exception&) override {
        ctx.log("cleanup-error");
    }
};
bool Verifier::verify_client(const std::shared_ptr<framework::AgentCircuitData>& circuit,
                             const std::string& address) {
    ctx.log("verify", (circuit == ctx.owner->input ? "1:" : "0:") + address);
    return ctx.flag("verified", true);
}
void Context::log(const std::string& op, const std::string& detail) {
    trace += op + ":" + hex(detail) + ";";
    if (text("failure") == op)
        throw std::runtime_error(op);
    if (!mutated && text("mutation_op") == op) {
        mutated = true;
        auto mode = text("mutation");
        if (mode == "drop")
            owner->manager.remove_circuit(id(1));
        else if (mode == "replace") {
            auto replacement = std::make_shared<framework::AgentCircuitData>();
            replacement->agent_id = id(1);
            replacement->circuit_code = 999;
            owner->manager.add_new_circuit(replacement);
        } else if (mode == "replace_input" || mode == "detach") {
            if (mode == "detach")
                owner->manager.remove_circuit(id(1));
            auto replacement = std::make_shared<framework::AgentCircuitData>();
            replacement->agent_id = id(9);
            replacement->circuit_code = 999;
            replacement->first_name = "Replacement";
            owner->input = replacement;
        } else if (mode == "rename") {
            owner->input->first_name = "Changed";
            region = "Changed Region";
        } else if (mode == "actor_id" && owner->actor)
            owner->actor->agent = id(9);
        else if (mode == "drop_actor")
            owner->actor = nullptr;
        else if (mode == "null_handler")
            null_handler = true;
    }
}
struct Harness : services::SceneClientVerification {
    Context& ctx;
    Backend& backend;
    Harness(Context& c, Backend& b) : SceneClientVerification(b), ctx(c), backend(b) {
    }
    std::shared_ptr<services::VerifiedScenePresence> wait_get_scene_presence(Uuid agent) override {
        if (!ctx.flag("override_wait"))
            return SceneClientVerification::wait_get_scene_presence(agent);
        ctx.log("wait-override", agent.str());
        return ctx.flag("override_presence", true) ? backend.actor : nullptr;
    }
};
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        Context ctx{network::parse_osd_json(unhex(line))};
        Backend backend(ctx);
        Harness handler(ctx, backend);
        std::optional<framework::IpEndpoint> endpoint;
        if (!ctx.flag("null_endpoint")) {
            auto address = framework::IpAddress::parse(ctx.text("ip", "127.0.0.1"));
            if (!address)
                throw std::invalid_argument("endpoint");
            endpoint = framework::IpEndpoint{std::make_shared<framework::IpAddress>(*address), 1234};
        }
        std::string result;
        bool via = true;
        try {
            if (ctx.text("mode") == "verify")
                result = handler.verify_client(ctx.flag("null_circuit") ? nullptr : backend.input,
                                               endpoint ? &*endpoint : nullptr, via)
                             ? "1"
                             : "0";
            else if (ctx.text("mode") == "wait")
                result = handler.wait_get_scene_presence(id(1)) ? "1" : "0";
            else
                result = handler.check_client(id(1), endpoint ? &*endpoint : nullptr) ? "1" : "0";
        } catch (const std::exception&) {
            result = "EX";
        }
        auto circuit = backend.manager.get_agent_circuit_data(id(1));
        std::cout << result << "|" << via << "|" << hex(ctx.trace) << "|" << ctx.gets << ":" << ctx.sleeps
                  << ":" << ctx.logouts << ":" << ctx.closes << "|"
                  << (circuit ? std::to_string(circuit->circuit_code) : "-") << "|"
                  << hex(backend.input->first_name.value_or("")) << "\n";
    }
}
