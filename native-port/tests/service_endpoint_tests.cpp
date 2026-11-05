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
#include "opensim/server/presence_endpoint.hpp"
#include "opensim/server/grid_user_endpoint.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace opensim;
namespace {
int failures = 0;
void check(bool value, const char* name) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
void tests(const std::filesystem::path& path) {
    services::SqliteGridUserStore grid_store(":memory:", true);
    auto grid_cache = std::make_shared<services::GridUserCache>();
    services::GridUserService grid(grid_store, [] { return 1700000000; }, grid_cache);
    server::IniConfig grid_config;
    server::GridUserEndpoint grid_endpoint(grid, grid_config);
    const std::string grid_id = "00000000-0000-0000-0000-000000000001",
                      grid_region = "00000000-0000-0000-0000-000000000003";
    auto grid_request = [&](const std::string& body) {
        return grid_endpoint.handle({"POST", "/griduser", body, "127.0.0.1", "", {}});
    };
    check(grid_request("METHOD=getgriduserinfo&UserID=" + grid_id).body.find("Failure") != std::string::npos,
          "grid missing singleton fails");
    check(grid_request("METHOD=getgriduserinfos&AgentIDs[]=missing").body ==
              "<?xml version=\"1.0\"?><ServerResponse />",
          "grid missing batch preserves empty XML root");
    auto grid_response = grid_request("METHOD=loggedin&UserID=" + grid_id);
    check(grid_response.body.find("11/14/2023 22:13:20") != std::string::npos && grid.info(grid_id)->online,
          "grid loggedin persists and formats UTC invariant date");
    check(
        grid_request("METHOD=sethome&UserID=" + grid_id + "&RegionID=" + grid_region).body.find("Success") !=
                std::string::npos &&
            grid.info(grid_id)->home_position == framework::Vector3{128, 128, 70},
        "grid home omitted position defaults");
    check(grid_request("METHOD=setposition&UserID=" + grid_id + "&RegionID=" + grid_region +
                       "&Position=bad&LookAt=<1,2,3>")
                      .body.find("Success") != std::string::npos &&
              grid.info(grid_id)->last_position == framework::Vector3{} &&
              grid.info(grid_id)->last_look_at == framework::Vector3{1, 2, 3},
          "grid invalid vector resets and valid look persists");
    check(grid_request("METHOD=loggedout&UserID=" + grid_id + "&RegionID=bad").body.find("Failure") !=
                  std::string::npos &&
              grid.info(grid_id)->online,
          "invalid grid region leaves online state intact");
    check(grid_request("METHOD=loggedout&UserID=" + grid_id + "&RegionID=" + grid_region)
                      .body.find("Success") != std::string::npos &&
              !grid.info(grid_id)->online && grid.info(grid_id)->logout == 1700000000,
          "grid logout persists session state");
    check(grid_request("METHOD=getgriduserinfos&AgentIDs[]=" + grid_id +
                       "&AgentIDs[]=missing&AgentIDs[]=" + grid_id)
                  .body.find("griduser1") != std::string::npos,
          "grid batch skips null and preserves duplicates");
    auto form = network::parse_service_form(
        "METHOD=one=ignored&METHOD=two&uuids[]=a&uuids=b&uuids%5b%5d=c&x=%uD83D%uDE00&bad=%FF&space=%20");
    check(std::get<std::string>(form.at("METHOD")) == "one" &&
              std::get<std::vector<std::string>>(form.at("uuids")) == std::vector<std::string>({"a", "c"}),
          "form first-value, list typing and equals-split rules");
    check(std::get<std::string>(form.at("x")) == "\xf0\x9f\x98\x80" &&
              std::get<std::string>(form.at("bad")) == "\xef\xbf\xbd" &&
              std::get<std::string>(form.at("space")) == " ",
          "form Unicode and replacement decoding");
    services::NullPresenceStore store;
    double now = 0;
    auto cache = std::make_shared<services::PresenceCache>([&] { return now; });
    services::PresenceService service(store, false, cache);
    auto session = framework::Uuid::random(), region = framework::Uuid::random();
    service.login("user <&>", session, framework::Uuid::random());
    server::IniConfig empty;
    server::PresenceEndpoint endpoint(service, empty);
    auto request = [&](const std::string& body, std::map<std::string, std::string> headers = {}) {
        return endpoint.handle({"POST", "/presence", body, "127.0.0.1", "application/x-www-form-urlencoded",
                                std::move(headers)});
    };
    check(request("METHOD=login&UserID=remote&SessionID=" + framework::Uuid::random().str())
                  .body.find("Failure") != std::string::npos,
          "presence endpoint refuses remote login");
    auto response = request("METHOD=getagent&SessionID=" + session.str());
    check(response.status == 200 && response.body.find("user &lt;&amp;&gt;") != std::string::npos &&
              response.body.find("type=\"List\"") != std::string::npos,
          "presence getagent XML map and escaping");
    check(request("METHOD=getagents&uuids=user").body.find("Failure") != std::string::npos,
          "getagents requires list argument");
    check(request("METHOD=getagents&uuids[]=missing").body.find("<result>null</result>") != std::string::npos,
          "missing agents null result");
    check(request("METHOD=report&SessionID=" + session.str() + "&RegionID=" + region.str())
                      .body.find("Success") != std::string::npos &&
              service.agent(session)->region_id == region,
          "report updates native service");
    check(request("METHOD=getagent&SessionID=" + session.str(), {{"X-SecondLife-Shard", ""}}).status == 403,
          "default service auth rejects script requests even empty header");
    {
        std::ofstream file(path);
        file
            << "[Network]\nAuthType=BasicHttpAuthentication\nHttpAuthUsername=network\nHttpAuthPassword=wrong\n[PresenceService]\nHttpAuthUsername=u\nHttpAuthPassword=p\n";
    }
    auto config = server::IniConfig::load(path);
    server::PresenceEndpoint secured(service, config);
    network::HttpRequest secured_request{
        "POST", "/presence", "METHOD=getagent&SessionID=" + session.str(), "127.0.0.1", "", {}};
    auto denied = secured.handle(secured_request);
    check(denied.status == 401 && denied.headers.at("WWW-Authenticate") == "Basic realm = \"Asset Server\"",
          "basic authentication challenge");
    secured_request.headers["Authorization"] = " Basic dTpwOnRhaWw= ";
    check(secured.handle(secured_request).status == 200,
          "service overrides and source ignored extra credential colon fields");
    secured_request.headers["Authorization"] = "basic dTpw";
    check(secured.handle(secured_request).status == 401, "Basic scheme matching remains case sensitive");
    secured_request.headers["Authorization"] = "Basic invalid!";
    bool threw = false;
    try {
        secured.handle(secured_request);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "invalid Base64 error propagates outside handler processing like source");
    check(request("METHOD=logout&SessionID=" + session.str()).body.find("Success") != std::string::npos &&
              !service.agent(session),
          "logout endpoint clears session");
}
}
int main() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("opensim-service-" + framework::Uuid::random().str() + ".ini");
    try {
        tests(path);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove(path);
        return 1;
    }
    std::filesystem::remove(path);
    return failures ? 1 : 0;
}
