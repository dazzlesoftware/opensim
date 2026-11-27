// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim::script;

int main() {
    try {
        const auto program = compile_lsl(R"(default{state_entry(){
            llSay(1,osFormatString("{{{0,4:X}}}/{1,-5:F2}/{2}/{3}/{4}",
                [12,"é😀",1.25,<1,2,3>,<0,0,0,1>]));
        }})");
        if (!program)
            throw std::runtime_error("Format function did not compile");
        LslHost host;
        std::string output;
        int permissions = 0;
        host.ossl_permission_error = [&](auto, auto, auto function) {
            if (function != "osFormatString")
                throw std::runtime_error("Wrong permission function");
            ++permissions;
            return std::string{};
        };
        host.chat = [&](const auto &chat) { output = chat.text; };
        LslInstance instance(program.program, {}, host);
        if (instance.run(1000).status != LslRunStatus::Idle || permissions != 1 ||
            output != "{  12}/é😀  /1.250000/<1.000000, 2.000000, 3.000000>/<0.000000, 0.000000, 0.000000, 1.000000>")
            throw std::runtime_error("Format conversion, UTF16 alignment or permission check differs: " + output);
        host.ossl_permission_error = [](auto, auto, auto) { return std::string("denied"); };
        output.clear();
        LslInstance denied(program.program, {}, host);
        if (denied.run(1000).status != LslRunStatus::Faulted || output.find("{  12}") != std::string::npos)
            throw std::runtime_error("Denied formatter must fault before producing formatted output");
        host.ossl_permission_error = [](auto, auto, auto) { return std::string{}; };
        const auto invalid = compile_lsl("default{state_entry(){llSay(1,osFormatString(\"{1}\",[\"x\"]));}}");
        LslInstance fault(invalid.program, {}, host);
        if (fault.run(1000).status != LslRunStatus::Faulted)
            throw std::runtime_error("Out-of-range argument must fault");
        if (compile_lsl("default{state_entry(){osFormatString(1,[]);}}"))
            throw std::runtime_error("Invalid formatter argument type accepted");
        std::cout << "PASS: OSSL formatting, boxed LSL values, alignment, permissions and faults\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
