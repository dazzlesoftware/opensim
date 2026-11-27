// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "../src/circuit_trace.hpp"
#include <thread>

int main() {
    const opensim::framework::Uuid agent("44444444-4444-4444-8444-444444444444");
    for (int sequence = 0; sequence < 4100; ++sequence) {
        if (sequence && sequence % 1024 == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        opensim::network::detail::circuit_trace("probe", agent, " sequence=", sequence);
    }
    std::cout << "READY" << std::endl;
    std::string command;
    std::getline(std::cin, command);
}
