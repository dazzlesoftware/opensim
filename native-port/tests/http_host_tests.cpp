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
#include "opensim/framework/http_host.hpp"
#include <iostream>
#include <stdexcept>
using opensim::framework::HttpHost;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        HttpHost empty(std::nullopt);
        check(empty.flags == HttpHost::Empty && empty.uri_slash() == "/" && empty.host_port() == ":80",
              "empty source host state");
        HttpHost secure("HTTPS://GRID.EXAMPLE/");
        check(secure.valid() && secure.port == 443 && secure.uri == "https://grid.example:443",
              "secure normalization");
        HttpHost plain("grid.example:443");
        check(secure.equals(plain) && secure.compare(plain) == 0, "host identity ignores scheme");
        check(!HttpHost("grid.example:65536").valid() && !HttpHost("grid.example/path").valid(),
              "port and internal path rejection");
        check(HttpHost("httpXxxgrid.example").uri == "http://grid.example:80", "source prefix compatibility");
        HttpHost parsed("localhost:0");
        check(parsed.valid() && parsed.port == 0, "zero port accepted");
        std::string error;
        check(!parsed.valid_and_resolved(error) && error == "failed DNS resolve of uri host",
              "unresolved host error");
        check(parsed.resolve_dns([](const auto&) -> std::optional<std::string> { return "203.0.113.7"; }) &&
                  parsed.resolved() && !parsed.ip,
              "source ResolveDNS sets flag without assigning IP");
        check(parsed.valid_and_resolved(error) && error.empty(), "valid resolved state");
        check(!parsed.resolve_dns([](const auto&) -> std::optional<std::string> { return std::nullopt; }) &&
                  !parsed.resolved(),
              "failed resolution clears resolved flag");
        HttpHost resolved("grid.example", true,
                          [](const auto&) -> std::optional<std::string> { return "203.0.113.7"; });
        check(resolved.resolved() && resolved.ip == "203.0.113.7", "constructor resolution assigns IP");
        std::cout << "PASS: HTTP host normalization and state\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
