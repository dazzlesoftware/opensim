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
#pragma once
#include "opensim/server/config.hpp"
#include "opensim/framework/http_uri.hpp"
#include <vector>
namespace opensim::network { class WebHttpClients; }
namespace opensim::server {
std::string script_grid_service_value(const IniConfig&, const network::WebHttpClients&, std::string_view key);
// Validate the destination and resolve the claimed home before asking it to verify the session.
bool authenticate_gatekeeper_agent(const class GridIdentity& grid, std::string_view token,
                                   std::string_view client_ip, std::string_view home_uri,
                                   const std::function<bool(const framework::HttpHost&, bool)>& verify,
                                   framework::RegionUri::Resolver resolver = {});
class GridIdentityError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class GridIdentity {
public:
    enum class Role { Grid, Gatekeeper };
    GridIdentity(const IniConfig& config, const std::optional<std::string>& default_uri = std::string{},
                 framework::RegionUri::Resolver resolver = {},
                 std::function<void(std::u16string_view)> error_log = {}, Role role = Role::Grid);
    bool has_hypergrid = false;
    std::optional<std::string> gate_uri() const;
    std::string gate_uri_slash() const;
    std::optional<std::string> home_uri() const;
    std::string home_uri_slash() const;
    std::optional<std::string> hg_gate_uri() const {
        return has_hypergrid ? gate_uri() : std::optional<std::string>{""};
    }
    std::string hg_gate_uri_slash() const {
        return has_hypergrid ? gate_uri_slash() : "";
    }
    std::optional<std::string> hg_home_uri() const {
        return has_hypergrid ? home_uri() : std::optional<std::string>{""};
    }
    std::string hg_home_uri_slash() const {
        return has_hypergrid ? home_uri_slash() : "";
    }
    int is_local_grid(const std::optional<std::string>& value, bool resolve_check = false) const;
    int is_local_grid(const framework::HttpHost& value) const;
    bool accepts_gatekeeper_token(std::string_view token) const;
    int is_local_home(const std::optional<std::string>& value, bool resolve_check = false) const;
    void add_grid_aliases(const std::optional<std::vector<std::string>>& aliases);
    void set_stuns(const std::optional<std::vector<std::string>>& values);
    void set_grid_url(const std::optional<std::string>& value);
    void set_search_url(const std::optional<std::string>& value);
    void set_destination_url(const std::optional<std::string>& value);
    void set_economy_url(const std::optional<std::string>& value);
    void set_name(const std::optional<std::string>& value) {
        if (value && !value->empty())
            name = *value;
    }
    void set_nick(const std::optional<std::string>& value) {
        if (value && !value->empty())
            nick = *value;
    }
    std::string name, nick, search_url, destination_url;
    std::optional<std::string> economy_url = std::string{}, grid_url = std::string{};
    std::optional<std::vector<std::string>> grid_aliases, stun_servers;

private:
    std::optional<framework::HttpHost> gate_, home_;
    std::vector<framework::HttpHost> gate_aliases_, home_aliases_;
    framework::RegionUri::Resolver resolver_;
};
}
