/*
 * Copyright (c) Contributors, http://opensimulator.org/
 * See CONTRIBUTORS.TXT for a full list of copyright holders.
 *
 * - Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * - Neither the name of the openmetaverse.org nor the names
 *   of its contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "opensim/framework/region_uri.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace opensim::framework;
std::string unhex(std::string_view input) {
    std::string value;
    for (std::size_t i = 0; i < input.size(); i += 2)
        value += static_cast<char>(std::stoi(std::string(input.substr(i, 2)), nullptr, 16));
    return value;
}
std::optional<std::string> text(std::string_view input) {
    return input == "N" ? std::nullopt : std::optional{unhex(input)};
}
std::string info(const RegionUri& uri) {
    return std::to_string(uri.flags) + "|" + uri.schema + "|" + uri.host + "|" + std::to_string(uri.port) +
           "|" + uri.region_name + "|" + uri.username + "|" + uri.user_pass + "|" + std::to_string(uri.x) +
           "|" + std::to_string(uri.y) + "|" + std::to_string(uri.z) + "|" + uri.original.value_or("NULL") +
           "|" + uri.ip.value_or("NULL") + "|" + (uri.valid() ? "1" : "0") + "|" + uri.host_url() + "|" +
           uri.host_url_slash() + "|" + uri.region_url_name() + "|" + uri.host_port_space_name() + "|" +
           (uri.local() ? "1" : "0");
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::string result;
        try {
            auto first = line.find('|'), second = line.find('|', first + 1);
            auto mode = std::stoi(line.substr(0, first));
            auto original = text(std::string_view(line).substr(
                first + 1, second == std::string::npos ? second : second - first - 1));
            RegionUri::Resolver resolver = [](const std::string& host) -> std::optional<std::string> {
                return host == "bad.invalid" ? std::nullopt : std::optional<std::string>{"203.0.113.7"};
            };
            auto uri = mode == 1 ? RegionUri(
                                       original,
                                       [](const std::string& uri) {
                                           return uri.find("local.example") != std::string::npos ? 1 : 0;
                                       },
                                       resolver)
                                 : RegionUri(original);
            if (mode == 2)
                uri.parse(text(std::string_view(line).substr(second + 1)));
            if (mode == 3)
                uri.set_local(false);
            if (mode == 4)
                uri.set_local(true);
            if (mode == 5)
                uri.resolve_dns(resolver);
            result = info(uri);
        } catch (...) {
            result = "EX";
        }
        for (unsigned char c : result)
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        std::cout << '\n';
    }
    return 0;
}
