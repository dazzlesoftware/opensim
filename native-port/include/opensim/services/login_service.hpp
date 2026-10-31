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
#include "opensim/network/rpc.hpp"
#include "opensim/server/config.hpp"
#include "opensim/services/account_store.hpp"
#include <atomic>
#include <optional>
#include <regex>
namespace opensim::services {
class LoginOrchestrator;
class LoginCompletion;
struct LoginRequest {
    std::string first,last,password,start,scope,version,channel,mac,id0,remote_ip;
    bool llsd=false;
};
network::Value login_failure(std::string_view name);
class ILoginService {
public:
    virtual ~ILoginService()=default;
    virtual network::Value login(const LoginRequest& request)=0;
    virtual network::Value set_level(std::string_view first,std::string_view last,std::string_view password,int level)=0;
};
class LoginService final : public ILoginService {
public:
    LoginService(AccountStore& store,const server::IniConfig& config);
    // Startup-only binding; services must outlive the HTTP listener.
    void set_orchestrator(LoginOrchestrator* value){orchestrator_=value;}
    void set_completion(LoginCompletion* value){completion_=value;}
    using Authenticator=std::function<AuthenticationResult(std::string_view,std::string_view,int,bool)>;
    void set_authenticator(Authenticator value){authenticate_=std::move(value);}
    network::Value login(const LoginRequest& request) override;
    network::Value set_level(std::string_view first,std::string_view last,std::string_view password,int level) override;
private:
    AccountStore& store_;
    Authenticator authenticate_;
    LoginOrchestrator* orchestrator_=nullptr;
    LoginCompletion* completion_=nullptr;
    std::atomic<int> minimum_level_;
    bool allow_remote_set_level_,allow_web_keys_,require_inventory_;
    std::string denied_macs_,denied_id0s_;
    std::optional<std::regex> allowed_clients_,denied_clients_;
};
class LoginHandlers {
public:
    explicit LoginHandlers(ILoginService& service):service_(service){}
    network::Value xml_rpc_login(const network::Value& data,const std::string& remote_ip);
    network::Value llsd_login(const network::Value& data,const std::string& remote_ip);
    network::Value set_level(const network::Value& data);
private:
    ILoginService& service_;
};
}
