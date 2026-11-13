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
#include "opensim/services/grid_service.hpp"
#include "opensim/network/async_http_server.hpp"
#include "opensim/network/web_http_clients.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace opensim;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("opensim-identity-" + framework::Uuid::random().str() + ".ini");
    try {
        framework::RegionUri::Resolver resolver = [](const std::string& host) -> std::optional<std::string> {
            return host == "bad.invalid" ? std::nullopt : std::optional<std::string>{"203.0.113.7"};
        };
        server::IniConfig empty;
        server::GridIdentity missing(empty, std::nullopt, resolver);
        check(!missing.has_hypergrid && !missing.gate_uri() && missing.gate_uri_slash().empty() &&
                  !missing.economy_url,
              "ignore-missing default struct/null behavior");
        bool threw = false;
        try {
            server::GridIdentity required(empty, "", resolver);
        } catch (const std::runtime_error& error) {
            threw = std::string(error.what()) == "GatekeeperURI configuration error";
        }
        check(threw, "empty default requires gatekeeper");
        std::atomic<int> grid_info_requests = 0;
        std::atomic<bool> grid_info_available = true;
        network::HttpServerOptions options;
        options.port = 0;
        network::AsyncHttpServer grid_info_server(options, [&](const auto& request, auto channel) {
            network::HttpResponse response;
            if (request.path == "/json_grid_info" && request.method == "GET") {
                ++grid_info_requests;
                response.status = grid_info_available ? 200 : 503;
                response.body = R"json({"login":"https://remote.example/login","custom":"remote value","numeric":7})json";
            } else {
                response.status = 404;
            }
            channel->complete(std::move(response));
        });
        int grid_info_port = 0;
        grid_info_server.start([&](int port) { grid_info_port = port; });
        {
            std::ofstream file(path);
            file
                << "[Const]\nGatekeeperURI=http://gate.example\nHomeURI=https://home.example:9000\n"
                   "GatekeeperURIAlias=alias.example,https://alias.example:80, bad/path\n"
                   "HomeURIAlias=homealias.example\nStunServers=stun.example:3478,stun.example:3478,bad/path\n"
                   "[GridInfo]\nGridName=Identity Grid\nGridInfoURI=http://127.0.0.1:"
                << grid_info_port << "\n[GridInfoService]\nlogin=https://configured.example/login\n";
        }
        auto config = server::IniConfig::load(path);
        network::WebHttpClients clients(network::native_web_http_handler_factory());
        clients.setup(false, false, nullptr, 4);
        check(server::script_grid_service_value(empty, clients, "login") == "Configuration Error!" &&
                  server::script_grid_service_value(config, clients, "login") == "https://configured.example/login" &&
                  grid_info_requests == 0,
              "OSSL grid queries prefer configured values and report a missing endpoint");
        check(server::script_grid_service_value(config, clients, "custom") == "remote value" &&
                  server::script_grid_service_value(config, clients, "numeric") == "7" &&
                  server::script_grid_service_value(config, clients, "missing").empty() &&
                  grid_info_requests == 3,
              "OSSL grid queries read JSON values from the grid info endpoint");
        grid_info_available = false;
        check(server::script_grid_service_value(config, clients, "custom") == "Get GridInfo Failed!" &&
                  grid_info_requests == 4,
              "OSSL grid queries handle a failed HTTP request");
        grid_info_server.stop();
        server::GridIdentity info(config, std::nullopt, resolver);
        check(info.has_hypergrid && info.gate_uri() == "http://gate.example:80" &&
                  info.home_uri() == "https://home.example:9000",
              "host configuration");
        check(info.grid_aliases->size() == 1 && info.is_local_grid("https://alias.example:80") == 1,
              "alias identity ignores scheme and suppresses duplicate host/port");
        check(info.is_local_home("homealias.example") == 1 && info.is_local_grid("bad/path") == -1 &&
                  info.is_local_grid(std::nullopt) == 1,
              "local home/invalid/empty rules");
        check(info.is_local_grid("http://bad.invalid", true) == -2 &&
                  info.is_local_grid("http://elsewhere.example", true) == 0,
              "DNS result classification");
        check(info.stun_servers->size() == 2, "STUN duplicates retained");
        info.add_grid_aliases(std::vector<std::string>{});
        check(info.grid_aliases->size() == 1, "empty alias setter preserves state");
        info.set_stuns(std::vector<std::string>{});
        check(!info.stun_servers, "empty STUN setter clears state");
        info.set_grid_url("http://new.example");
        check(info.grid_url == "http://new.example:80" && info.is_local_grid("gate.example") == 1 &&
                  info.is_local_grid("new.example") == 0,
              "grid URL display setter does not change identity");
        check(info.accepts_gatekeeper_token("http://gate.example/;token"),
              "Gatekeeper token names the configured grid");
        check(info.accepts_gatekeeper_token("https://alias.example:80;token"),
              "Gatekeeper token names a grid alias");
        check(!info.accepts_gatekeeper_token("http://gate.example:9000;token") &&
                  !info.accepts_gatekeeper_token("http://gate.example") &&
                  !info.accepts_gatekeeper_token(";token") &&
                  !info.accepts_gatekeeper_token("bad/path;token"),
              "Gatekeeper rejects invalid or foreign token destinations");
        int verification_calls = 0;
        const auto verify = [&](const framework::HttpHost&, bool local) {
            ++verification_calls;
            return local;
        };
        check(!server::authenticate_gatekeeper_agent(info, "bad/path;token", "192.0.2.10",
                                                     "http://gate.example", verify, resolver) &&
                  !server::authenticate_gatekeeper_agent(info, "http://gate.example;token", "",
                                                         "http://gate.example", verify, resolver) &&
                  !server::authenticate_gatekeeper_agent(info, "http://gate.example;token", "192.0.2.10",
                                                         "http://bad.invalid", verify, resolver) &&
                  verification_calls == 0,
              "Gatekeeper rejects invalid requests before session verification");
        check(server::authenticate_gatekeeper_agent(info, "http://gate.example;token", "192.0.2.10",
                                                    "http://gate.example", verify, resolver) &&
                  !server::authenticate_gatekeeper_agent(info, "http://gate.example;token", "192.0.2.10",
                                                         "https://home.example:9000", verify, resolver) &&
                  verification_calls == 2,
              "Gatekeeper distinguishes local and remote session verification");
        services::NullRegionStore store("!static");
        services::GridService service(store, config, [] { return 1700000000; }, {}, {}, resolver);
        services::GridRegion region;
        region.id = framework::Uuid::random();
        region.name = "Region";
        region.x = 25600;
        region.y = 25600;
        region.internal_endpoint = services::RegionEndpoint{"0.0.0.0", 9000};
        region.external_host = "127.0.0.1";
        check(service.register_region({}, region).empty(), "register identity test region");
        check(service.by_name({}, "http://gate.example/Region")->id == region.id &&
                  service.local_by_name({}, "http://alias.example/Region")->id == region.id,
              "service uses configured native grid identity");
        std::filesystem::remove(path);
        std::cout << "PASS: grid identity configuration and registry integration\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
