#include "opensim/script/ossl_format.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto frame = opensim::network::parse_osd_json(line);
        opensim::network::Value result;
        try {
            if (frame.get("mixed").kind == opensim::network::Value::Kind::Boolean && frame.get("mixed").boolean) {
                const auto quoted = opensim::network::osd_json(frame.get("format"), true);
                const auto program = opensim::script::compile_lsl("default{state_entry(){llSay(1,osFormatString(" +
                                                                  quoted + ",[12,\"é😀\",1.25,<1,2,3>,<0,0,0,1>]));}}");
                if (!program)
                    throw std::runtime_error("Mixed formatter script did not compile");
                opensim::script::LslHost host;
                std::string output;
                host.ossl_permission_error = [](auto, auto, auto) { return std::string{}; };
                host.chat = [&](const auto &chat) {
                    if (chat.channel == 1)
                        output = chat.text;
                };
                opensim::script::LslInstance instance(program.program, {}, host);
                if (instance.run(1000).status != opensim::script::LslRunStatus::Idle)
                    throw std::runtime_error("Format script faulted");
                result = opensim::network::Value::object({{"outcome", "return"}, {"result", output}});
            } else {
                std::vector<std::string> arguments;
                for (const auto &value : frame.get("arguments").array)
                    arguments.push_back(value.text);
                result = opensim::network::Value::object(
                    {{"outcome", "return"},
                     {"result", opensim::script::ossl_format_string(frame.get("format").text, arguments)}});
            }
        } catch (const std::exception &) {
            result = opensim::network::Value::object({{"outcome", "throw"}});
        }
        std::cout << opensim::network::osd_json(result, true) << '\n';
    }
}
