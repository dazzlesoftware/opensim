// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/user_agent_connector.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using network::Value;
std::string Hex(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
struct Client;
struct Host {
    int scenario;
    Value calls, logs;
    std::shared_ptr<services::GridRegion> gate;
    std::shared_ptr<framework::AgentCircuitData> circuit;
    Client* client = nullptr;
    framework::EntityTransferContext* previous = nullptr;
    void log(const std::u16string& t) {
        logs.array.emplace_back(framework::utf8(t));
        if (scenario == 6)
            throw network::NwcXmlRpcError("System.InvalidOperationException", u"log");
    }
};
struct Client : services::UserAgentServiceConnector {
    using Value = network::Value;
    Host& h;
    Client(std::optional<std::u16string> url, services::UserAgentConnectorServices services, Host& host)
        : UserAgentServiceConnector(std::move(url), std::move(services)), h(host) {
    }
    std::string path() {
        return agent_path();
    }
    void pack(Value& out, const services::GridRegion* s,
              const std::shared_ptr<framework::AgentCircuitData>& c, const services::GridRegion& d,
              std::uint32_t f) {
        framework::AgentCircuitData unused;
        pack_data(out, s, c ? *c : unused, d, f);
    }
    bool create_agent(const services::GridRegion* source, services::GridRegion& dest,
                      const std::shared_ptr<framework::AgentCircuitData>& circuit, std::uint32_t flags,
                      services::LoginTransferContext& context, std::string& reason) override {
        reason = "callee";
        auto args = std::make_shared<Value>(Value::object({}));
        args->set("seed", Value(std::int32_t{7}));
        auto record = Value::object({});
        record.set("same_circuit", Value(circuit == h.circuit));
        record.set("null_circuit", Value(!circuit));
        record.set("context", context.pack());
        record.set("fresh_context", Value(h.previous != &context));
        record.set("flags", std::to_string(flags));
        record.set("path", path());
        record.set("home_id", dest.id.str());
        record.set("home_x", Value(dest.x));
        record.set("home_y", Value(dest.y));
        record.set("home_name", dest.name.value_or(""));
        record.set("home_size_x", Value(dest.size_x));
        record.set("home_size_y", Value(dest.size_y));
        record.set("home_uri", dest.server_uri());
        record.set("args", Value::alias(args));
        h.calls.array.push_back(record);
        h.previous = &context;
        if (h.scenario == 2)
            throw network::NwcXmlRpcError("System.InvalidOperationException", u"create");
        if (h.scenario == 3 && h.gate) {
            h.gate->external_host = "Mutated";
            h.gate->http_port = 0xffffffffu;
            h.gate->set_server_uri("https://MUTATED/Path");
        }
        if (h.scenario == 7 && h.calls.array.size() == 1) {
            auto next = std::make_shared<services::GridRegion>();
            next->set_server_uri("https://SECOND/Case");
            next->external_host = "SecondGate";
            next->http_port = 54321;
            auto borrowed = std::shared_ptr<services::GridRegion>(&dest, [](auto*) {});
            std::string ignored;
            login_agent_to_grid_owned(source, circuit, next, borrowed, false, ignored);
        }
        pack(*args, source, circuit, dest, flags);
        reason = h.scenario == 1 ? "denied" : "created";
        return h.scenario != 1;
    }
};
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int profile;
        Host h;
        in >> profile >> h.scenario;
        h.calls.kind = h.logs.kind = Value::Kind::Array;
        std::string reason = "seed", status;
        Value after;
        bool alive = true;
        auto circuit = (profile & 8) ? nullptr : std::make_shared<framework::AgentCircuitData>();
        if (circuit)
            circuit->agent_id = framework::Uuid("00000000-0000-0000-0000-000000000007");
        h.circuit = circuit;
        auto source = std::make_shared<services::GridRegion>();
        source->id = framework::Uuid("00000000-0000-0000-0000-000000000002");
        source->x = -256;
        source->y = 1024;
        source->name = (profile & 256) ? std::optional<std::string>{} : std::optional<std::string>{"Source"};
        source->raw_server_uri =
            (profile & 256) ? std::optional<std::string>{} : std::optional<std::string>{"https://SOURCE/raw"};
        auto gate = std::make_shared<services::GridRegion>();
        gate->id = framework::Uuid("00000000-0000-0000-0000-000000000003");
        gate->x = 300;
        gate->y = 400;
        gate->http_port = 9001;
        gate->external_host =
            (profile & 64) ? std::optional<std::string>{} : std::optional<std::string>{"GATE.host"};
        gate->raw_server_uri =
            (profile & 32) ? std::optional<std::string>{} : std::optional<std::string>{"https://GATE/Case"};
        auto dest = std::make_shared<services::GridRegion>();
        dest->id = framework::Uuid("00000000-0000-0000-0000-000000000001");
        dest->x = std::numeric_limits<std::int32_t>::min();
        dest->y = std::numeric_limits<std::int32_t>::max();
        dest->size_x = 512;
        dest->size_y = 1024;
        dest->name = "Destination";
        dest->set_server_uri("https://Destination/Case");
        h.gate = (profile & 4) ? nullptr : gate;
        std::weak_ptr<services::GridRegion> weak = h.gate;
        services::UserAgentConnectorServices s;
        s.simulation_options.automatic_purge = false;
        s.client = [](int) -> std::shared_ptr<network::XmlRpcRequesterClient> {
            throw std::logic_error("XML-RPC outside grid fixture");
        };
        s.debug = s.error = s.warn = [&](auto& t) { h.log(t); };
        std::unique_ptr<Client> client;
        try {
            client = std::make_unique<Client>(
                (profile & 128) ? u"relative" : u"HTTPS://HOME/Case?TOKEN=Secret", s, h);
            h.client = client.get();
            auto destination = (profile & 2) ? nullptr : dest;
            bool value = client->login_agent_to_grid_owned(
                (profile & 1) ? nullptr : source.get(), circuit, h.gate, destination,
                h.scenario == 4 ? false : (profile & 16) == 0, reason);
            status = value ? "1" : "0";
            if (h.scenario == 5 && !(profile & 2)) {
                if (h.gate)
                    h.gate->http_port = 12345;
                gate.reset();
                h.gate.reset();
                alive = (profile & 4) || !weak.expired();
                after = Value::object({});
                client->pack(after, (profile & 1) ? nullptr : source.get(), circuit, *dest, 0xffffffffu);
            }
        } catch (const network::NwcXmlRpcError& e) {
            status = "E:" + e.source_type + ":" + framework::utf8(e.message);
        } catch (const std::exception& e) {
            status = "UNEXPECTED:" + std::string(e.what());
        }
        for (auto& call : h.calls.array) {
            Value keys;
            keys.kind = Value::Kind::Array;
            for (auto& key : call.get("args").ordered_keys())
                keys.array.emplace_back(key);
            call.set("keys", std::move(keys));
        }
        auto out = Value::object({});
        out.set("status", status);
        out.set("reason", reason);
        out.set("calls", h.calls);
        out.set("logs", h.logs);
        out.set("alive", Value(alive));
        out.set("after", after);
        std::cout << Hex(network::osd_json(out, true)) << '\n';
    }
}
