#include "opensim/scene/entity_transfer_state.hpp"
#include <iostream>
#include <sstream>
#include <bit>

using namespace opensim;

std::string hex(const std::string &value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char byte : value) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--outside-view") {
        std::uint32_t bits, old_x, new_x, old_y, new_y;
        int old_sx, old_sy, new_sx, new_sy;
        while (std::cin >> bits >> old_x >> new_x >> old_y >> new_y >> old_sx >> old_sy >> new_sx >> new_sy)
            std::cout << scene::transfer_outside_view(std::bit_cast<float>(bits), old_x, new_x, old_y, new_y, old_sx,
                                                      old_sy, new_sx, new_sy)
                      << '\n';
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        const framework::Uuid id("11111111-1111-4111-8111-111111111111");
        bool wait = true, fail_region = false, fail_log = false, reset_log = false;
        int sleeps = 0, at = 0;
        char action = 'h';
        std::string logs;
        scene::EntityTransferStateMachine *current = nullptr;
        scene::EntityTransferStateMachine machine(
            [&] {
                if (fail_region)
                    throw std::runtime_error("region failed");
                return std::string("Test Region");
            },
            [&] { return wait; },
            [&](int milliseconds) {
                if (milliseconds != 100)
                    throw std::runtime_error("Wrong sleep duration");
                if (++sleeps != at)
                    return;
                if (action == 'h')
                    current->set_agent_arrived_at_destination(id);
                else if (action == 'x')
                    current->reset_from_transit(id);
                else
                    throw std::runtime_error("sleep failed");
            },
            [&](auto level, const auto &message) {
                if (fail_log)
                    throw std::runtime_error("log failed");
                if (reset_log)
                    current->reset_from_transit(id);
                logs += std::string(level) + ":" + message + "\n";
            });
        current = &machine;
        std::istringstream commands(line);
        std::string command;
        bool first = true;
        while (commands >> command) {
            std::string result = "-";
            try {
                const char c = command[0];
                if (c == 's')
                    result = machine.set_in_transit(id) ? "T" : "F";
                else if (c == 'u')
                    result = machine.update_in_transit(
                                 id, static_cast<scene::AgentTransferState>(std::stoi(command.substr(1))))
                                 ? "T"
                                 : "F";
                else if (c == 'r')
                    result = machine.reset_from_transit(id) ? "T" : "F";
                else if (c == 'a')
                    machine.set_agent_arrived_at_destination(id);
                else if (c == 'w')
                    result = machine.wait_for_agent_arrived_at_destination(id) ? "T" : "F";
                else if (c == 'm')
                    wait = command[1] == '1';
                else if (c == 'p')
                    machine.enable_wait_for_agent_arrived_at_destination = command[1] == '1';
                else if (c == 'b')
                    fail_region = command[1] == '1';
                else if (c == 'l')
                    fail_log = command[1] == '1';
                else if (c == 'z')
                    reset_log = command[1] == '1';
                else if (c == 'h' || c == 'x' || c == 'f') {
                    action = c;
                    at = std::stoi(command.substr(1));
                    sleeps = 0;
                } else if (c != 'g')
                    throw std::runtime_error("Unknown command");
            } catch (const std::exception &error) {
                result = "E:" + hex(error.what());
            }
            const auto state = machine.agent_transfer_state(id);
            if (!first)
                std::cout << ';';
            first = false;
            std::cout << result << '|' << (state ? std::to_string(static_cast<int>(*state)) : "N") << '|' << sleeps
                      << '|' << hex(logs);
        }
        std::cout << '\n';
    }
}
