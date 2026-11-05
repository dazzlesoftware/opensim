/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/services/login_service.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
using namespace opensim;
using network::Value;
class Echo final : public services::ILoginService {
public:
    Value login(const services::LoginRequest& r) override {
        return Value::object({{"first", Value(r.first)},
                              {"last", Value(r.last)},
                              {"passwd", Value(r.password)},
                              {"start", Value(r.start)},
                              {"scope_id", Value(r.scope)},
                              {"version", Value(r.version)},
                              {"channel", Value(r.channel)},
                              {"mac", Value(r.mac)},
                              {"id0", Value(r.id0)},
                              {"ip", Value(r.remote_ip)}});
    }
    Value set_level(std::string_view first, std::string_view last, std::string_view password,
                    int level) override {
        return Value::object({{"first", Value(std::string(first))},
                              {"last", Value(std::string(last))},
                              {"passwd", Value(std::string(password))},
                              {"level", Value(level)},
                              {"success", Value(true)}});
    }
};
int main() {
    Echo echo;
    services::LoginHandlers handlers(echo);
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            auto separator = line.find('\t');
            auto mode = line.substr(0, separator), body = line.substr(separator + 1);
            Value reply;
            if (mode == "failure")
                reply = services::login_failure(body);
            else if (mode == "levelraw") {
                std::u16string text;
                for (std::size_t i = 0; i < body.size(); i += 4)
                    text.push_back(static_cast<char16_t>(std::stoul(body.substr(i, 4), nullptr, 16)));
                reply = handlers.set_level(Value::object({{"first", Value("Test")},
                                                          {"last", Value("User")},
                                                          {"passwd", Value("secret")},
                                                          {"level", Value(framework::utf8(text))}}));
            } else if (mode == "llsd")
                reply = handlers.llsd_login(network::parse_llsd_xml(body), "127.0.0.1");
            else {
                auto call = network::parse_xml_rpc(body);
                const Value empty;
                auto& input = call.parameters.empty() ? empty : call.parameters.front();
                reply =
                    mode == "level" ? handlers.set_level(input) : handlers.xml_rpc_login(input, "127.0.0.1");
            }
            std::cout << network::xml_rpc_response(reply) << '\n';
        } catch (const std::exception&) {
            std::cout << "EXCEPTION\n";
        }
    }
}
