// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/agent_launch.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim;
using namespace services;
framework::Uuid id(int n) {
    std::ostringstream out;
    out << "00000000-0000-0000-0000-" << std::hex << std::setw(12) << std::setfill('0') << n;
    return framework::Uuid(out.str());
}
std::string n(const GridRegion& region) {
    return region.id.str();
}
struct Trace {
    int seed, attempt = 0;
    std::string events;
    std::shared_ptr<framework::AgentCircuitData> circuit;
    void add(const std::string& value) {
        events += value + ";";
    }
    bool passed() {
        return seed % 4 == 0 || (seed % 4 == 1 && attempt == 2) || (seed % 4 == 3 && attempt == 3);
    }
};
struct Direct : ISimulationLogin {
    Trace& trace;
    std::string label;
    Direct(Trace& t, std::string value) : trace(t), label(std::move(value)) {
    }
    bool query_access(GridRegion& region, const framework::Uuid& agent,
                      const std::optional<std::string>& home, bool teleport,
                      const framework::Vector3& position, std::vector<framework::Uuid>& features,
                      LoginTransferContext& context, std::string& reason) override {
        const auto packed = context.pack();
        const bool valid_context = packed.map.size() == 3 &&
                                   packed.get("InboundVersion").kind == network::Value::Kind::Real &&
                                   packed.get("InboundVersion").real == static_cast<double>(0.8f) &&
                                   packed.get("OutboundVersion").kind == network::Value::Kind::Real &&
                                   packed.get("OutboundVersion").real == static_cast<double>(0.8f) &&
                                   packed.get("WearablesCount").kind == network::Value::Kind::Integer &&
                                   packed.get("WearablesCount").integer == -1;
        ++trace.attempt;
        trace.add(label + "-query:" + n(region) + ":" + std::to_string(static_cast<int>(position.z)) + ":" +
                  std::to_string(!home && teleport && features.empty() && agent == id(9) &&
                                 context.inbound_version == 0.8f && context.outbound_version == 0.8f &&
                                 context.wearables_count == -1 && valid_context));
        context.outbound_version = 0.7f;
        context.wearables_count = trace.seed;
        reason = "query-" + std::to_string(trace.attempt);
        if (trace.seed % 13 == 0)
            throw std::runtime_error("query");
        return !((trace.seed % 4 == 1 && trace.attempt == 1) || (trace.seed % 4 == 3 && trace.attempt == 2));
    }
    bool create_agent(const GridRegion* source, GridRegion& region,
                      const std::shared_ptr<framework::AgentCircuitData>& circuit, std::uint32_t flags,
                      LoginTransferContext& context, std::string& reason) override {
        trace.add(label + "-create:" + n(region) + ":" + std::to_string(flags) + ":" +
                  std::to_string(circuit->teleport_flags) + ":" +
                  std::to_string(!source && circuit == trace.circuit && context.outbound_version == 0.7f &&
                                 context.wearables_count == trace.seed));
        circuit->start_position.z += 1;
        reason = "create-" + std::to_string(trace.attempt);
        if (trace.seed % 17 == 0)
            throw std::runtime_error("create");
        return trace.passed();
    }
};
struct Indirect : IUserAgentLogin {
    Trace& trace;
    Indirect(Trace& t) : trace(t) {
    }
    bool login_agent_to_grid(const GridRegion* source,
                             const std::shared_ptr<framework::AgentCircuitData>& circuit, GridRegion& gate,
                             GridRegion& destination, bool via, std::string& reason) override {
        ++trace.attempt;
        trace.add("hg:" + n(destination) + ":" + gate.external_host.value_or("-") + ":" +
                  std::to_string(gate.http_port) + ":" + gate.server_uri() + ":" + gate.scope.str() + ":" +
                  gate.token + ":" + std::to_string(circuit->teleport_flags) + ":" +
                  std::to_string(!source && via && circuit == trace.circuit));
        if (trace.seed & 128) {
            ++gate.internal_endpoint->port;
            trace.add("gate-endpoint:" + std::to_string(gate.internal_endpoint->port) + ":" +
                      std::to_string(destination.internal_endpoint->port));
        }
        reason = "hg-" + std::to_string(trace.attempt);
        if (trace.seed % 19 == 0)
            throw std::runtime_error("hg");
        return trace.passed();
    }
};
int main(int argc, char** argv) {
    try {
        const int seed = std::stoi(argv[1]);
        Trace trace{seed};
        server::IniConfig config;
        NullRegionStore store;
        GridService grid(store, config);
        std::shared_ptr<GridRegion> destination;
        for (int i = 1; i <= 3; ++i) {
            auto region = std::make_shared<GridRegion>();
            region->id = id(i);
            region->name = "Region" + std::to_string(i);
            region->x = 25600 + i * 256;
            region->y = 25600;
            region->size_x = region->size_y = 256;
            region->scope = id(8);
            region->token = "source-token";
            region->internal_endpoint = RegionEndpoint{"0.0.0.0", 9000};
            region->external_host = "127.0.0.1";
            region->http_port = 9000;
            auto row = std::make_shared<RegionData>(region_data(*region));
            row->data["flags"] = (i == 1 && !(seed & 16)) ? "0" : "6";
            store.store(row);
            if (i == 1)
                destination = region;
        }
        Direct local(trace, "local"), remote(trace, "remote");
        Indirect indirect(trace);
        const int mode = seed % 5;
        std::string uri = seed % 3 == 0   ? "http://GATE.example:8002/"
                          : seed % 3 == 1 ? "https://[::1]:8443/"
                                          : "bad/path";
        AgentLauncher launcher(seed & 8 ? nullptr : &grid,
                               mode == 1 || mode == 3 || mode == 4 ? &local : nullptr,
                               mode == 2 || mode == 3 || mode == 4 ? &remote : nullptr,
                               mode == 4 ? &indirect : nullptr, uri, [&] {
                                   trace.add("code");
                                   return 1234u;
                               });
        AgentLaunchRequest request;
        request.destination = destination;
        request.account_scope = id(8);
        request.where = "home";
        request.flags = 128 | (seed & 32 ? 512 : 0);
        if (seed & 64) {
            request.gatekeeper = std::make_shared<GridRegion>(destination->legacy_copy());
            request.gatekeeper->external_host = "provided.example";
            request.gatekeeper->http_port = 9001;
            request.gatekeeper->set_server_uri("http://provided.example:9001/");
            request.gatekeeper->scope = id(7);
            request.gatekeeper->token = "provided-token";
        }
        request.make_agent = [&](const GridRegion& region, std::uint32_t code) {
            trace.add("make:" + n(region) + ":" + std::to_string(code));
            if (seed % 11 == 0)
                throw std::runtime_error("make");
            auto circuit = std::make_shared<framework::AgentCircuitData>();
            circuit->agent_id = id(9);
            circuit->circuit_code = code;
            circuit->start_position = {12, 13, 14};
            circuit->teleport_flags = 65536;
            trace.circuit = circuit;
            return circuit;
        };
        std::string result;
        try {
            const auto value = launcher.launch(request);
            result = (value.circuit ? "OK" : "FAIL") + std::string("|") +
                     (value.destination ? n(*value.destination) : "-") + "|" + value.where + "|" +
                     value.reason + "|" + std::to_string(value.circuit && value.circuit == trace.circuit);
        } catch (const std::exception&) {
            result = "EX";
        }
        std::cout << result << '|' << trace.events << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
