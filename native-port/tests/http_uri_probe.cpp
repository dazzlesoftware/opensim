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
#include "opensim/framework/http_uri.hpp"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace opensim::framework;
std::optional<std::string> text(std::string_view input) {
    if (input == "N")
        return std::nullopt;
    std::string result;
    for (std::size_t i = 0; i < input.size(); i += 2)
        result += static_cast<char>(std::stoi(std::string(input.substr(i, 2)), nullptr, 16));
    return result;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::string result;
        try {
            const auto a = line.find('|'), b = line.find('|', a + 1);
            const int mode = std::stoi(line.substr(0, a));
            std::string dns;
            RegionUri::Resolver resolver = [&](const std::string& host) -> std::optional<std::string> {
                if (!dns.empty())
                    dns += ",";
                dns += host;
                if (mode == 4 || mode == 5)
                    throw std::runtime_error("dns");
                return host == "bad.invalid" ? std::nullopt : std::optional<std::string>{"203.0.113.7"};
            };
            HttpUri uri(text(std::string_view(line).substr(a + 1, b - a - 1)), mode == 1 || mode == 4,
                        resolver);
            if (mode == 2 || mode == 5)
                uri.resolve_dns(resolver);
            std::string error;
            uri.valid_and_resolved(error);
            result = std::to_string(uri.flags) + "|" + std::to_string(uri.port) + "|" + uri.host + "|" +
                     uri.url + "|" + uri.path + "|" + uri.uri + "|" + uri.ip.value_or("NULL") + "|" +
                     uri.uri_slash() + "|" + uri.host_port() + "|" + (uri.valid() ? "1" : "0") + "|" +
                     (uri.resolved() ? "1" : "0") + "|" + error + "|" + dns;
            if (mode == 3) {
                HttpUri other(text(std::string_view(line).substr(b + 1)));
                const int compared = uri.compare(other);
                result += "|" + std::string(uri.equals(other) ? "1" : "0") + "|" + std::to_string(compared);
            }
        } catch (...) {
            result = "EX";
        }
        for (unsigned char c : result)
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        std::cout << '\n';
    }
    return 0;
}
