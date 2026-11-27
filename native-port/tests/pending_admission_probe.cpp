// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/circuit_transport.hpp"
#include "pending_cache_clock.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <deque>

using namespace opensim::network;
using namespace opensim::framework;

namespace {
struct Observation {
    int admissions, acknowledgments, delivered;
    bool connected;
    std::uint64_t callback_errors;
    bool timer_active;
};

std::vector<Observation> observe(std::string_view input) {
    std::istringstream commands{std::string(input)};
    int scenario = 0;
    commands >> scenario;
    PendingCacheClock cache_clock;
    AgentCircuitManager circuits;
    auto data = std::make_shared<AgentCircuitData>();
    data->agent_id = Uuid::random();
    data->session_id = Uuid::random();
    data->circuit_code = 42;
    circuits.add_new_circuit(42, data);
    UdpEndpoint original{"127.0.0.1", 12001}, replacement{"127.0.0.1", 12002};
    const auto new_identity = [](UdpEndpoint &value) {
        auto snapshot = value;
        snapshot.identity.reset();
        value.identity = std::make_shared<const UdpEndpoint>(std::move(snapshot));
    };
    new_identity(original);
    new_identity(replacement);
    int failure = 0, admissions = 0, acknowledgments = 0, delivered = 0;
    bool presence = true;
    bool deferred = false;
    std::deque<std::function<void()>> jobs;
    CircuitTransport transport(
        circuits,
        [&](const auto &, auto bytes) {
            if (lludp::decode_datagram(bytes).spec->name == "PacketAck")
                ++acknowledgments;
            return true;
        },
        [&](const auto &, const auto &, const auto &) {
            ++admissions;
            if (failure == 2)
                throw std::runtime_error("Admission failed");
            presence = true;
            return failure != 1;
        },
        [&](const auto &, const auto &) { ++delivered; },
        [&](const auto &) {
            if (failure == 3)
                throw std::runtime_error("Teardown failed");
        },
        {}, [&] { return static_cast<std::int32_t>(cache_clock.now); },
        [&](auto job) {
            if (deferred)
                jobs.push_back(std::move(job));
            else
                job();
        },
        cache_clock.backend());
    transport.set_reuse_callbacks([&](const auto &) { return presence; }, {});
    auto use = lludp::make_packet("UseCircuitCode");
    use.sequence = 1;
    use.block("CircuitCode")["Code"] = std::uint64_t{42};
    use.block("CircuitCode")["ID"] = data->agent_id;
    use.block("CircuitCode")["SessionID"] = data->session_id;
    auto update = lludp::make_packet("AgentUpdate");
    update.reliable = false;
    update.block("AgentData")["AgentID"] = data->agent_id;
    update.block("AgentData")["SessionID"] = data->session_id;
    if (scenario == 3)
        transport.receive(original, lludp::encode_datagram(use));
    failure = scenario;
    std::vector<Observation> observations;
    std::string command;
    while (commands >> command) {
        if (command == "u") {
            ++use.sequence;
            transport.receive(replacement, lludp::encode_datagram(use));
        } else if (command == "p") {
            transport.receive(replacement, lludp::encode_datagram(update));
        } else if (command == "ok") {
            failure = 0;
        } else if (command == "f") {
            cache_clock.fire();
        } else if (command == "d") {
            deferred = true;
        } else if (command == "e") {
            new_identity(replacement);
        } else if (command == "k") {
            data->secure_session_id = Uuid::random();
        } else if (command == "n") {
            presence = false;
        } else if (command == "r") {
            if (!jobs.empty()) {
                auto job = std::move(jobs.front());
                jobs.pop_front();
                job();
            }
        } else if (command.starts_with('t')) {
            cache_clock.now = std::stod(command.substr(1));
        } else {
            throw std::invalid_argument("Unknown pending admission probe command");
        }
        observations.push_back({admissions, acknowledgments, delivered, bool(transport.connection(data->agent_id)),
                                transport.statistics().callback_errors, cache_clock.active()});
    }
    failure = 0;
    return observations;
}

void self_test() {
    for (const int scenario : {2, 3}) {
        const auto states = observe(std::to_string(scenario) + " u p ok u t60000 f u t60001 u f u p");
        if (states.size() != 12 || states[0].callback_errors != 1 || !states[0].timer_active ||
            states[6].admissions != 1 || states[8].admissions != 1 || states[9].timer_active ||
            states[10].admissions != 2 || !states[10].connected || states[11].delivered != 1)
            throw std::runtime_error("Pending admission error did not preserve timer-driven retry and queue expiry");
    }
    const auto rejected = observe("1 u p ok u p");
    if (rejected.size() != 5 || rejected[0].callback_errors != 0 || rejected[0].timer_active ||
        rejected[3].admissions != 2 || !rejected[3].connected || rejected[4].delivered != 1)
        throw std::runtime_error("Ordinary admission rejection must clear pending state for an immediate retry");
    const auto late = observe("0 d u p t60001 f r p");
    if (late.size() != 7 || late[5].admissions != 1 || !late[5].connected || late[5].delivered != 0 ||
        late[6].delivered != 1)
        throw std::runtime_error("Expired queue must not cancel its scheduled admission");
    const auto superseded = observe("0 d u p t60001 f u p r p");
    if (superseded.size() != 9 || superseded[7].admissions != 1 || superseded[7].delivered != 1 ||
        superseded[8].delivered != 2)
        throw std::runtime_error("Scheduled admission must replay the current endpoint queue");
    std::cout << "PASS: admission and teardown errors retain pending queues until a strict cache purge\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
            self_test();
            return 0;
        }
        std::string line;
        while (std::getline(std::cin, line)) {
            bool first = true;
            for (const auto &state : observe(line)) {
                if (!first)
                    std::cout << ';';
                first = false;
                std::cout << state.admissions << ',' << state.acknowledgments << ',' << state.delivered << ','
                          << state.connected << ',' << state.callback_errors << ',' << state.timer_active;
            }
            std::cout << '\n';
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
