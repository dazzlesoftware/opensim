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
#include <stdexcept>
using opensim::framework::HttpUri;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        HttpUri empty(std::nullopt);
        check(empty.flags == HttpUri::Empty && empty.port == -1 && empty.uri_slash() == "/" &&
                  empty.host_port() == ":-1",
              "empty URI state");
        HttpUri path("HTTPS://HOST:443/a/b/../c?q=a b#fragment");
        check(path.valid() && path.port == 443 && path.url == "https://host:443" && path.path == "/a/c" &&
                  path.uri == "https://host/a/c?q=a%20b#fragment",
              "scheme, path, query and fragment canonicalization");
        check(HttpUri("http://host/%41%7e%2f%FF").path == "/A~%2f%FF", "escaped unreserved normalization");
        check(HttpUri("http://host/a\\b").path == "/a/b", "backslash path normalization");
        check(HttpUri("http://127.1/a").host == "127.0.0.1", "numeric IPv4 normalization");
        check(HttpUri("http://[2001:0db8::1]:9000/a").host == "2001:db8::1", "IPv6 literal normalization");
        check(!HttpUri("http://u@v@host/").valid() && !HttpUri("http://host:-0/").valid(),
              "invalid authority and signed port rejected");
        check(HttpUri("http://_host/a").valid() && HttpUri("http://-host/a").valid(),
              "URI host classifier differs from host-only parser");
        HttpUri left("http://host:443/a?x=1"), right("https://host/a?x=2");
        check(left.equals(right) && left.compare(right) == 0, "identity ignores scheme and query");
        check(!HttpUri("http://host/A").equals(HttpUri("http://host/a")),
              "path identity remains case-sensitive");
        HttpUri parsed("http://host/a");
        check(parsed.resolve_dns([](const auto&) -> std::optional<std::string> { return "203.0.113.7"; }) &&
                  parsed.resolved() && !parsed.ip,
              "source ResolveDNS flag/IP asymmetry");
        HttpUri resolved("http://host/a", true,
                         [](const auto&) -> std::optional<std::string> { return "203.0.113.7"; });
        check(resolved.resolved() && resolved.ip == "203.0.113.7", "constructor resolution assigns IP");
        std::cout << "PASS: HTTP URI canonicalization and state\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
