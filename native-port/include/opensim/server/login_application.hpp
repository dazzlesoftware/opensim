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
#include "opensim/network/http_server.hpp"
#include "opensim/server/json_rpc.hpp"
#include "opensim/services/login_service.hpp"
#include "opensim/server/presence_endpoint.hpp"
#include "opensim/server/grid_user_endpoint.hpp"
#include "opensim/server/grid_endpoint.hpp"
#include "opensim/server/account_endpoint.hpp"
#include "opensim/server/authentication_endpoint.hpp"
#include "opensim/server/avatar_endpoint.hpp"
#include "opensim/server/friends_endpoint.hpp"
#include "opensim/server/offline_im_endpoint.hpp"
namespace opensim::server {
class LoginApplication {
  public:
    LoginApplication(services::ILoginService& login, const IniConfig& config);
    network::HttpResponse handle(const network::HttpRequest& request, bool allow_gatekeeper = true);
    network::HttpResponse handle_gatekeeper(const network::HttpRequest& request);
    void set_default_login_uri(const std::string& uri);
    void set_presence_endpoint(PresenceEndpoint* endpoint) {
        presence_ = endpoint;
    }
    void set_grid_user_endpoint(GridUserEndpoint* endpoint) {
        grid_user_ = endpoint;
    }
    void set_grid_endpoint(GridEndpoint* endpoint) {
        grid_ = endpoint;
    }
    void set_friends_endpoint(FriendsEndpoint* endpoint) {
        friends_ = endpoint;
    }
    void set_offline_im_endpoint(OfflineImEndpoint* endpoint) {
        offline_im_ = endpoint;
    }
    void set_avatar_endpoint(AvatarEndpoint* endpoint) {
        avatar_ = endpoint;
    }
    void set_account_endpoint(AccountEndpoint* endpoint) {
        accounts_ = endpoint;
    }
    void set_authentication_endpoint(AuthenticationEndpoint* endpoint) {
        authentication_ = endpoint;
    }
    JsonRpcDispatcher& json_rpc() {
        return json_rpc_;
    }
    void set_grid_message_handler(std::function<bool(const network::Value&)> handler) {
        grid_message_ = std::move(handler);
    }
    void
    set_gatekeeper_handler(std::function<network::Value(std::string_view, const network::Value&)> handler) {
        gatekeeper_ = std::move(handler);
    }
    void set_home_agent_verifier(std::function<bool(std::string_view, const network::Value&)> verifier) {
        home_agent_verifier_ = std::move(verifier);
    }
    void set_home_region_lookup(std::function<network::Value(const network::Value&)> lookup) {
        home_region_lookup_ = std::move(lookup);
    }
    void set_home_user_lookup(
        std::function<network::Value(std::string_view, const network::Value&, std::string_view)> lookup) {
        home_user_lookup_ = std::move(lookup);
    }

  private:
    JsonRpcDispatcher json_rpc_;
    std::function<bool(const network::Value&)> grid_message_;
    std::function<network::Value(std::string_view, const network::Value&)> gatekeeper_;
    std::function<bool(std::string_view, const network::Value&)> home_agent_verifier_;
    std::function<network::Value(const network::Value&)> home_region_lookup_;
    std::function<network::Value(std::string_view, const network::Value&, std::string_view)>
        home_user_lookup_;
    services::LoginHandlers handlers_;
    std::map<std::string, std::string> grid_info_;
    PresenceEndpoint* presence_ = nullptr;
    GridUserEndpoint* grid_user_ = nullptr;
    GridEndpoint* grid_ = nullptr;
    AccountEndpoint* accounts_ = nullptr;
    AuthenticationEndpoint* authentication_ = nullptr;
    AvatarEndpoint* avatar_ = nullptr;
    FriendsEndpoint* friends_ = nullptr;
    OfflineImEndpoint* offline_im_ = nullptr;
};
}
