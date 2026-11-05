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
#include <stdexcept>
using opensim::framework::RegionUri;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        RegionUri uri("https://grid.example:8002/Alpha/1/2/-3");
        check(uri.flags == 34 && uri.port == 8002 && uri.x == 1 && uri.y == 2 && uri.z == -3,
              "scheme/port/coordinate parser");
        check(uri.host_port_space_name() == "grid.example:8002/ Alpha", "legacy host/port space separator");
        uri.parse("Beta/7");
        check(uri.flags == 32 && uri.x == 7 && uri.y == 2 && uri.z == -3 && uri.schema == "https://" &&
                  uri.port == 8002,
              "reparse resets flags while retaining unspecified fields");
        check(uri.original == "https://grid.example:8002/Alpha/1/2/-3",
              "reparse retains constructor original");
        RegionUri local("Alpha");
        check(local.local() && !local.has_host() && local.region_name == "Alpha", "local name constructor");
        RegionUri localhost("https://localhost");
        check(localhost.local() && localhost.region_name == "localhost" && localhost.port == 443,
              "dot-free host-like text is a local region in source");
        RegionUri overflow("Alpha/4294967296/4294967295/-2147483648");
        check(overflow.x == 0 && overflow.y == -1 && overflow.z == (-2147483647 - 1),
              "unchecked coordinate arithmetic");
        RegionUri negative("Alpha/1/2/-");
        check(negative.valid() && negative.z == 0, "source accepts minus-only Z as zero");
        RegionUri nul("http://%00.example/Alpha");
        check(!nul.valid() && nul.host == std::string(9, '\0'),
              "bundled UTF8 conversion retains zero-filled allocation");
        check(RegionUri("Alpha%2BBeta").region_name == "Alpha Beta", "escaped plus becomes source space");
        check(!RegionUri("http://_bad.example/Alpha").valid(), "leading underscore hostname rejected");
        int calls = 0;
        RegionUri::Resolver resolver = [&](const auto&) -> std::optional<std::string> {
            ++calls;
            return "203.0.113.7";
        };
        RegionUri recognized("https://local.example/Alpha", [](const auto&) { return 1; }, resolver);
        check(recognized.local() && !recognized.has_host() && calls == 0,
              "local-grid constructor bypasses DNS and clears host");
        RegionUri remote("https://remote.example/Alpha", [](const auto&) { return 0; }, resolver);
        check(remote.has_host() && remote.ip == "203.0.113.7" && calls == 1 &&
                  (remote.flags & RegionUri::HasResolvedHost),
              "remote constructor resolves through host resolver");
        remote.set_local(false);
        check(remote.has_host(), "false local setter is a no-op");
        remote.set_local(true);
        check(!remote.has_host() && remote.host.empty() && remote.ip == "203.0.113.7",
              "true local setter preserves resolved IP");
        RegionUri numeric("http://127.0.0.1/Alpha");
        check(numeric.resolve_dns() && numeric.ip == "127.0.0.1", "native numeric IPv4 resolution");
        RegionUri any("http://0.0.0.0/Alpha");
        check(!any.resolve_dns(), "unspecified numeric address excluded");
        RegionUri ipv6("");
        ipv6.flags = RegionUri::HasHost;
        ipv6.host = "::1";
        check(ipv6.resolve_dns() && ipv6.ip == "::1", "native numeric IPv6 resolution");
        std::cout << "PASS: region URI state and native address resolution\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
