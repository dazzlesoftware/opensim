#include "simulation_http_fixture.hpp"
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/simulation_connector.hpp"
#include "opensim/network/osd_json.hpp"
#include <future>
#include <iostream>
#include <sstream>
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
services::GridRegion region(bool second = false) {
    services::GridRegion r;
    r.id = id(second ? 8 : 9);
    r.x = 25600;
    r.y = 51200;
    r.name = "Destination";
    r.set_server_uri(second ? "http://other:9000/base/" : "http://sim:9000/base/");
    return r;
}
std::shared_ptr<framework::AgentPosition> position(float far = 10, int agent = 7) {
    auto p = std::make_shared<framework::AgentPosition>();
    p->agent_id = id(agent);
    p->position = {1.25f, 2, 30};
    p->far = far;
    return p;
}
struct Harness : services::SimulationConnector {
    mutable int paths = 0;
    int throw_path = 0;
    bool custom = false;
    using SimulationConnector::SimulationConnector;
    using SimulationConnector::send_agent_position;
    std::string agent_path() const override {
        if (++paths == throw_path)
            throw std::runtime_error("path");
        return custom ? "custom/" : "agent/";
    }
};
int main(int argc, char** argv) {
    if (argc > 1) {
        services::SimulationConnector connector(simulation_http_requester());
        auto destination = region();
        destination.set_server_uri(argv[1]);
        bool first = connector.update_agent(destination, position());
        bool second = connector.update_agent(destination, position(20));
        auto result = Value::object({});
        result.set("first", Value(first));
        result.set("second", Value(second));
        result.set("failed", Value(static_cast<std::int32_t>(connector.failed_simulator_count())));
        std::cout << network::osd_json(result, true);
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        auto args = network::parse_osd_json(decode(line));
        auto mode = network::osd_string(args.get("mode"));
        double now = 0;
        int calls = 0;
        std::string trace, nested, results;
        auto first = position();
        if (network::osd_boolean(args.get("bad_pack")))
            first->god_data = std::make_shared<Value>();
        auto second = position(20), third = position(30);
        auto a = region(), b = region(true);
        Harness* handler = nullptr;
        std::promise<void> entered, proceed;
        auto gate = proceed.get_future().share();
        auto status = [](const std::function<bool()>& call) {
            try {
                return call() ? std::string("1") : std::string("0");
            } catch (const std::exception&) {
                return std::string("EX");
            }
        };
        services::SimulationConnector::PositionRequest request = [&](const std::string& uri,
                                                                     const Value& data, int timeout,
                                                                     bool compressed) {
            ++calls;
            trace += hex(uri) + ":" + std::to_string(timeout) + ":" + std::to_string(compressed) + ":" +
                     hex(network::osd_json(data, true)) + ";";
            if (calls == 1) {
                if (mode == "parallel") {
                    entered.set_value();
                    gate.wait();
                } else if (mode == "coalesce" || mode == "failure_queued") {
                    nested += status([&] { return handler->update_agent(a, second); }) + ",";
                    nested += status([&] { return handler->update_agent(a, third); }) + ",";
                } else if (mode == "same") {
                    first->far = 99;
                    nested += status([&] { return handler->update_agent(a, first); }) + ",";
                }
            }
            const auto& replies = args.get("replies");
            const auto& reply = replies.array.at(std::min<std::size_t>(calls - 1, replies.array.size() - 1));
            if (reply.kind == Value::Kind::String && reply.text == "throw")
                throw std::runtime_error("request");
            return reply;
        };
        Harness connector({}, {}, request, {[&] { return now; }, false});
        handler = &connector;
        connector.throw_path = network::osd_integer(args.get("throw_path"));
        connector.custom = network::osd_boolean(args.get("custom"));
        auto public_call = [&](services::GridRegion* destination,
                               const std::shared_ptr<framework::AgentPosition>& data) {
            return status([&] { return connector.update_agent(destination, data); });
        };
        auto append = [&](std::string value) { results += value + ","; };
        auto advance = [&](double delta) { now += delta; };
        if (mode == "worker") {
            framework::EntityTransferContext context;
            context.outbound_version = static_cast<float>(network::osd_real(args.get("version")));
            append(status([&] {
                return connector.send_agent_position(
                    network::osd_boolean(args.get("null_destination")) ? nullptr : &a,
                    network::osd_boolean(args.get("null_data")) ? nullptr : first,
                    network::osd_boolean(args.get("null_context")) ? nullptr : &context,
                    network::osd_integer(args.get("timeout")));
            }));
        } else if (mode == "parallel") {
            auto worker = std::async(std::launch::async, [&] { return public_call(&a, first); });
            if (entered.get_future().wait_for(std::chrono::seconds(5)) != std::future_status::ready)
                throw std::runtime_error("request barrier");
            nested += public_call(&a, second) + ",";
            nested += public_call(&a, third) + ",";
            proceed.set_value();
            append(worker.get());
        } else {
            append(public_call(network::osd_boolean(args.get("null_destination")) ? nullptr : &a,
                               network::osd_boolean(args.get("null_data")) ? nullptr : first));
            if (mode == "repeat" || mode == "failure_queued")
                append(public_call(&a, first));
            else if (mode == "other_agent") {
                append(public_call(&a, position(40, 8)));
                append(public_call(&b, first));
            } else if (mode == "cooldown") {
                advance(130);
                append(public_call(&a, first));
                connector.purge_failed_simulators();
                append(public_call(&a, first));
            } else if (mode == "clock_back") {
                advance(-130);
                connector.purge_failed_simulators();
                append(public_call(&a, first));
                advance(260);
                connector.purge_failed_simulators();
                append(public_call(&a, first));
            } else if (mode == "purge_order") {
                advance(-100);
                append(public_call(&b, first));
                advance(130);
                connector.purge_failed_simulators();
                append(public_call(&b, first));
                advance(100);
                connector.purge_failed_simulators();
                append(public_call(&b, first));
            }
        }
        std::cout << results << "|" << nested << "|" << hex(trace) << "|"
                  << connector.failed_simulator_count() << "|" << connector.paths << "\n";
    }
}
