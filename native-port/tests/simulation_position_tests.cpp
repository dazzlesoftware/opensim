// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/simulation_connector.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
using namespace opensim;
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("position cache check failed");
}
int main() {
    try {
        services::GridRegion destination;
        destination.set_server_uri("http://sim:9000/");
        auto position = std::make_shared<framework::AgentPosition>();
        position->agent_id = framework::Uuid("00000000-0000-0000-0000-000000000007");
        double now = 0;
        int calls = 0;
        services::SimulationConnector manual(
            {}, {},
            [&](const std::string&, const network::Value&, int timeout, bool compressed) {
                check(timeout == 10000 && compressed);
                ++calls;
                return network::Value::object({{"Success", network::Value(calls > 1)}});
            },
            {[&] { return now; }, false});
        check(!manual.update_agent(destination, position) && calls == 1);
        now = 121;
        check(!manual.update_agent(destination, nullptr) && calls == 1);
        now = 120;
        manual.purge_failed_simulators();
        check(manual.failed_simulator_count() == 1);
        now = 120.001;
        manual.purge_failed_simulators();
        check(manual.failed_simulator_count() == 0 && manual.update_agent(destination, position) &&
              calls == 2);
        std::atomic<double> automatic_now = 0;
        std::atomic<int> automatic_calls = 0;
        services::SimulationConnector automatic({}, {},
                                                [&](const std::string&, const network::Value&, int, bool) {
                                                    return network::Value::object(
                                                        {{"Success", network::Value(++automatic_calls > 1)}});
                                                },
                                                {[&] { return automatic_now.load(); }, true});
        check(!automatic.update_agent(destination, position));
        automatic_now = 121;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (automatic.failed_simulator_count() != 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(automatic.failed_simulator_count() == 0 && automatic.update_agent(destination, position) &&
              automatic_calls == 2);
        std::cout << "PASS: native position cache expiry boundaries, stale lookup and automatic purge\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what();
        return 1;
    }
}
