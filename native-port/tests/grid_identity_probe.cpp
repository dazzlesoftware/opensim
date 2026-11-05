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
#include "opensim/server/grid_identity.hpp"
#include "opensim/framework/filesystem.hpp"
#include <iostream>
#include <iomanip>
using namespace opensim;
std::optional<std::string> text(std::string_view input) {
    if (input == "N")
        return std::nullopt;
    std::string result;
    for (std::size_t i = 0; i < input.size(); i += 2)
        result += static_cast<char>(std::stoi(std::string(input.substr(i, 2)), nullptr, 16));
    return result;
}
std::string array(const std::optional<std::vector<std::string>>& value) {
    if (!value)
        return "NULL";
    std::string result;
    for (std::size_t i = 0; i < value->size(); ++i) {
        if (i)
            result += ',';
        result += (*value)[i];
    }
    return result;
}
int main(int argc, char** argv) {
    try {
        const auto config = server::IniConfig::load(framework::utf8_path(argv[1]));
        server::GridIdentity info(
            config, text(argv[2]), [](const std::string& host) -> std::optional<std::string> {
                return host == "bad.invalid" ? std::nullopt : std::optional<std::string>{"203.0.113.7"};
            });
        const int action = std::stoi(argv[3]);
        try {
            if (action == 1) {
                info.set_name("");
                info.set_name("New Name");
                info.set_nick("");
                info.set_nick("NN");
                info.set_grid_url("https://new.example:8002");
                info.set_search_url("http://search.example/new?q=2");
                info.set_destination_url("http://bad.invalid");
                info.set_economy_url("https://economy.example/path");
                info.add_grid_aliases(
                    std::vector<std::string>{"added.example", "http://added.example:80", "bad/path"});
                info.set_stuns(
                    std::vector<std::string>{"stun.example:3478", "bad/path", "stun.example:3478"});
            }
            if (action == 2) {
                info.add_grid_aliases(std::vector<std::string>{});
                info.set_stuns(std::vector<std::string>{});
                info.set_grid_url("bad/path");
                info.set_search_url("http://bad.invalid");
                info.set_name(std::nullopt);
                info.set_nick("");
            }
            if (action == 3)
                info.add_grid_aliases(std::nullopt);
        } catch (...) {
            std::cout << "SETTER_ERROR\n";
            return 0;
        }
        std::vector<std::string> rows = {info.has_hypergrid ? "1" : "0",
                                         info.gate_uri().value_or("NULL"),
                                         info.gate_uri_slash(),
                                         info.hg_gate_uri().value_or("NULL"),
                                         info.hg_gate_uri_slash(),
                                         info.home_uri().value_or("NULL"),
                                         info.home_uri_slash(),
                                         info.hg_home_uri().value_or("NULL"),
                                         info.hg_home_uri_slash(),
                                         info.grid_url.value_or("NULL"),
                                         array(info.grid_aliases),
                                         info.name,
                                         info.nick,
                                         info.search_url,
                                         info.destination_url,
                                         info.economy_url.value_or("NULL"),
                                         array(info.stun_servers)};
        for (const auto& value : std::vector<std::optional<std::string>>{
                 std::nullopt, "", "bad/path", "http://gate.example", "https://gate.example:80",
                 "alias.example", "home.example", "http://bad.invalid", "http://unrelated.example",
                 "added.example"}) {
            rows.push_back(std::to_string(info.is_local_grid(value)));
            rows.push_back(std::to_string(info.is_local_grid(value, true)));
            rows.push_back(std::to_string(info.is_local_home(value)));
            rows.push_back(std::to_string(info.is_local_home(value, true)));
        }
        std::string result;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (i)
                result += '|';
            result += rows[i];
        }
        for (unsigned char c : result)
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        std::cout << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cout << "ERROR|" << error.what() << '\n';
        return 0;
    }
}
