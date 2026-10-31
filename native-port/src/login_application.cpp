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
#include "opensim/server/login_application.hpp"
#include <stdexcept>
namespace opensim::server {
using namespace network;
LoginApplication::LoginApplication(services::ILoginService& login,const IniConfig& config):handlers_(login),grid_info_(config.section("GridInfoService")) {
    if(!grid_info_.contains("platform"))grid_info_["platform"]="OpenSim";
    const auto home=config.get("Startup","HomeURI",config.get("Hypergrid","HomeURI",config.get("LoginService","SRV_HomeURI")));
    if(!home.empty())grid_info_["home"]=home;
    for(const auto& pair:{std::pair{"HomeURIAlias","homealias"},std::pair{"GatekeeperURI","gatekeeper"},std::pair{"GatekeeperURIAlias","gatekeeperalias"}}){
        const auto value=config.get("Startup",pair.first,config.get("Hypergrid",pair.first));
        if(!value.empty())grid_info_[pair.second]=value;
    }
    grid_info_xml(grid_info_); // Reject invalid XML keys at startup, before accepting traffic.
}
void LoginApplication::set_default_login_uri(const std::string& uri){if(!grid_info_.contains("login"))grid_info_["login"]=uri;}
HttpResponse LoginApplication::handle(const HttpRequest& request) {
    if(request.path.starts_with("/auth")&&authentication_)return authentication_->handle(request);
    if(request.path=="/friends"&&friends_)return friends_->handle(request);
    if(request.path=="/avatar"&&avatar_)return avatar_->handle(request);
    if(request.path=="/accounts"&&accounts_)return accounts_->handle(request);
    if(request.path=="/presence"&&presence_)return presence_->handle(request);
    if(request.path=="/grid"&&grid_)return grid_->handle(request);
    if(request.path=="/griduser"&&grid_user_)return grid_user_->handle(request);
    HttpResponse response;
    response.headers["Cache-Control"]="no-store";
    if(request.path=="/health"||request.path=="/ready") {
        if(request.method!="GET"&&request.method!="HEAD"){response.status=405;response.headers["Allow"]="GET, HEAD";return response;}
        response.content_type="application/json";
        response.status=request.path=="/ready"?503:200;
        response.body="{\"service\":\"OpenSimNative\",\"running\":true,\"authentication_ready\":true,\"simulation_ready\":false,\"login_success_available\":false}";
        return response;
    }
    if(request.path=="/get_grid_info"||request.path=="/json_grid_info") {
        if(request.method!="GET"&&request.method!="HEAD"){response.status=405;response.headers["Allow"]="GET, HEAD";return response;}
        if(request.path=="/get_grid_info"){response.content_type="application/xml";response.body=grid_info_xml(grid_info_);}
        else {response.content_type="application/json";response.body=json_strings(grid_info_);}
        return response;
    }
    if(request.path!="/"&&request.path!="/login"){response.status=404;return response;}
    if(request.method!="POST"){response.status=405;response.headers["Allow"]="POST";return response;}
    auto content=request.content_type.substr(0,request.content_type.find(';'));
    for(auto& c:content)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    if(content=="application/llsd+xml") {
        response.content_type="application/llsd+xml";
        try {
            const auto value=parse_llsd_xml(request.body);
            if(value.kind!=Value::Kind::Map){response.status=400;return response;}
            response.body=llsd_xml(handlers_.llsd_login(value,request.remote_ip));
        }catch(const std::exception&){response.status=400;}
        return response;
    }
    response.content_type="text/xml";
    RpcCall call;
    try{call=parse_xml_rpc(request.body);}catch(const std::exception&){response.status=404;return response;}
    try {
        const Value empty;
        const auto& data=call.parameters.empty()?empty:call.parameters.front();
        if(call.method=="login_to_simulator")response.body=xml_rpc_response(handlers_.xml_rpc_login(data,request.remote_ip));
        else if(call.method=="set_login_level")response.body=xml_rpc_response(handlers_.set_level(data));
        else if(call.method=="get_grid_info") {
            std::map<std::string,Value> info;for(const auto& [key,value]:grid_info_)info[key]=Value(value);
            response.body=xml_rpc_response(Value::object(std::move(info)));
        }else response.body=xml_rpc_fault(-32601,"Requested method ["+call.method+"] not found");
    }catch(const std::exception&){response.body=xml_rpc_fault(-32603,"Error processing XML-RPC request");}
    response.headers["Access-Control-Allow-Origin"]="*";
    return response;
}
}
