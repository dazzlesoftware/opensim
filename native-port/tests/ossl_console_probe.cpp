#include "opensim/script/lsl_runtime.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim;

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto frame = network::parse_osd_json(line);
        const auto program = script::compile_lsl("default{listen(integer channel,string name,key id,string message){"
                                                 "llSay(1,(string)osConsoleCommand(message));}}");
        if (!program)
            return 2;
        script::LslHost host;
        std::vector<network::Value> commands;
        int permission_checks = 0, owner_checks = 0;
        std::string output;
        host.ossl_permission_error = [&](auto, auto, auto) {
            ++permission_checks;
            return frame.get("ossl").boolean ? std::string{} : std::string("denied");
        };
        host.console_command = [&](auto, const auto &command) {
            ++owner_checks;
            if (!frame.get("owner").boolean)
                return false;
            commands.emplace_back(command);
            if (frame.get("throws").boolean)
                throw std::runtime_error("Controlled console failure");
            return true;
        };
        host.chat = [&](const auto &chat) {
            if (chat.channel == 1)
                output = chat.text;
        };
        script::LslInstance instance(program.program, {}, host);
        instance.post_event({"listen", {std::int32_t(1), std::string{}, std::string{}, frame.get("command").text}});
        const bool success = instance.run(1000).status == script::LslRunStatus::Idle;
        network::Value command_list;
        command_list.kind = network::Value::Kind::Array;
        command_list.array = std::move(commands);
        auto result = network::Value::object({{"outcome", success ? "return" : "throw"},
                                              {"permissionChecks", network::Value(permission_checks)},
                                              {"ownerChecks", network::Value(owner_checks)},
                                              {"commands", std::move(command_list)}});
        if (success)
            result.map["result"] = network::Value(output == "1");
        std::cout << network::osd_json(result, true) << '\n';
    }
}
