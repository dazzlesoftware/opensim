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
#include "opensim/services/login_service.hpp"
#include "opensim/services/login_orchestration.hpp"
#include "opensim/services/login_completion.hpp"
#include "opensim/framework/identity.hpp"
#include <charconv>
#include <stdexcept>
namespace opensim::services {
using network::Value;
using namespace opensim::framework;
namespace {
Value failure(const std::string& reason,const std::string& message) {
    return Value::object({{"reason",Value(reason)},{"message",Value(message)},{"login",Value("false")}});
}
std::string optional(const Value& data,std::string_view name,const std::string& fallback="") {
    return data.contains(name)&&data.get(name).kind!=Value::Kind::Undefined ? data.get(name).as_string() : fallback;
}
bool nonnull(const Value& data,std::string_view key) { return data.contains(key)&&data.get(key).kind!=Value::Kind::Undefined; }
std::string llsd_string(const Value& value) {
    return value.kind==Value::Kind::Boolean?(value.boolean?"1":"0"):value.as_string();
}
std::optional<std::regex> pattern(const std::string& expression) {
    if(expression.empty())return std::nullopt;
    try {return std::regex(expression,std::regex::ECMAScript|std::regex::icase);}catch(const std::regex_error&){return std::nullopt;}
}
}
Value login_failure(std::string_view name) {
    if(name=="UserProblem")return failure("key","Could not authenticate your avatar. Please check your username and password, and check the grid if problems persist.");
    if(name=="GridProblem")return failure("key","Error connecting to the desired location. Try connecting to another region.");
    if(name=="InventoryProblem")return failure("key","The inventory service is not responding.  Please notify your login region operator.");
    if(name=="DeadRegionProblem")return failure("key","The region you are attempting to log into is not responding. Please select another region and try again.");
    if(name=="LoginBlockedProblem")return failure("presence","Logins are currently restricted. Please try again later.");
    if(name=="UnverifiedAccountProblem")return failure("presence","Your account has not yet been verified. Please check your email and click the provided link.");
    if(name=="AlreadyLoggedInProblem")return failure("presence","You appear to be already logged in. Please wait a a minute or two and retry. If this takes longer than a few minutes please contact the grid owner. ");
    if(name=="InternalError")return failure("Internal Error","Error generating Login Response");
    if(name=="IncompleteXMLRPC")return failure("key","Incomplete login credentials. Check your username and password.");
    if(name=="InvalidLLSD")return failure("key","Invalid login credentials. Check your username and passwd.");
    throw std::invalid_argument("Unknown login failure");
}
LoginService::LoginService(AccountStore& store,const server::IniConfig& config):store_(store),
    minimum_level_(config.integer("LoginService","MinLoginLevel",0)),
    allow_remote_set_level_(config.boolean("LoginService","AllowRemoteSetLoginLevel",false)),
    allow_web_keys_(config.boolean("NativeServer","AllowWebLoginKeys",true)),
    require_inventory_(config.boolean("LoginService","RequireInventory",true)) {
    auto access=[&config](const std::string& key){return config.get("AccessControl",key,config.get("LoginService",key));};
    denied_macs_=access("DeniedMacs"); denied_id0s_=access("DeniedID0s");
    allowed_clients_=pattern(access("AllowedClients")); denied_clients_=pattern(access("DeniedClients"));
}
Value LoginService::login(const LoginRequest& r) {
    const auto session=orchestrator_?Uuid::random():Uuid{};
    try {
        const std::string client=r.version.find(' ')!=std::string::npos?r.version:r.channel+" "+r.version;
        if((allowed_clients_&&!std::regex_search(client,*allowed_clients_))||
           (denied_clients_&&std::regex_search(client,*denied_clients_))||
           (!denied_macs_.empty()&&denied_macs_.find(r.mac)!=std::string::npos)||
           (!denied_id0s_.empty()&&denied_id0s_.find(r.id0)!=std::string::npos))return login_failure("LoginBlockedProblem");
        const auto account=store_.find(r.scope,r.first,r.last);
        if(!account)return login_failure("UserProblem");
        if(account->user_level<minimum_level_.load())return login_failure("LoginBlockedProblem");
        if(account->principal_id=="6571e388-6218-4574-87db-f9379718315e")return login_failure("UserProblem");
        if(r.scope!=ZeroUuid&&account->scope_id!=r.scope&&account->scope_id!=ZeroUuid)return login_failure("UserProblem");
        const std::string password=r.password.starts_with("$1$")?r.password.substr(3):md5(r.password);
        const auto authentication=authenticate_?authenticate_(account->principal_id,password,30,allow_web_keys_):store_.authenticate(account->principal_id,password,30,allow_web_keys_);
        if(!authentication)return login_failure("UserProblem");
        Uuid secure_session;
        try{secure_session=Uuid(authentication.token);}catch(const std::invalid_argument&){return login_failure("UserProblem");}
        if(orchestrator_){
            const auto scope=Uuid(r.scope==ZeroUuid?account->scope_id:r.scope);
            auto prepared=orchestrator_->prepare(*account,scope,session,secure_session,r.start);
            if(auto* failure=std::get_if<Value>(&prepared))return *failure;
            if(completion_){
                LoginCompletionRequest request;request.start=r.start;request.viewer=r.version;request.channel=r.channel;request.mac=r.mac;request.id0=r.id0;request.remote_ip=r.remote_ip;request.first=r.first;request.last=r.last;if(!authentication.real_id.empty())request.real_id=Uuid(authentication.real_id);
                auto result=completion_->finish(std::get<PreparedLogin>(prepared),request);
                if(auto* rejected=std::get_if<Value>(&result)){orchestrator_->abort(session);return *rejected;}
                auto response=std::get<std::shared_ptr<LoginResponse>>(result);return r.llsd?response->to_llsd():response->to_xml_rpc();
            }
            // The source LaunchAgentAtGrid returns a null circuit with an empty
            // reason when no simulation connector exists. Admission is still
            // unavailable here; clean up presence and preserve that response.
            orchestrator_->abort(session);
            return failure("key","");
        }
        if(require_inventory_)return login_failure("InventoryProblem");
        // Region/inventory/presence launch is the next migration scope. Never
        // issue a successful viewer response or circuit until those services exist.
        return login_failure("GridProblem");
    }catch(const std::exception&){if(orchestrator_)orchestrator_->cleanup(session);return login_failure("InternalError");}
}
Value LoginService::set_level(std::string_view first,std::string_view last,std::string_view password,int level) {
    auto result=Value::object({{"success",Value("false")}});
    if(!allow_remote_set_level_)return result;
    try {
        const auto account=store_.find(ZeroUuid,first,last);
        if(!account||account->user_level<200)return result;
        if(!(authenticate_?authenticate_(account->principal_id,password,30,allow_web_keys_):store_.authenticate(account->principal_id,password,30,allow_web_keys_)))return result;
        minimum_level_.store(level); return Value::object({{"success",Value(true)}});
    }catch(const std::exception&){return result;}
}
Value LoginHandlers::xml_rpc_login(const Value& data,const std::string& ip) {
    if(data.kind!=Value::Kind::Map||!nonnull(data,"first")||!nonnull(data,"last")||
        !(nonnull(data,"passwd")||(!data.contains("passwd")&&nonnull(data,"web_login_key")&&data.get("web_login_key").as_string()!=ZeroUuid)))
        return login_failure("IncompleteXMLRPC");
    LoginRequest r;
    r.first=data.get("first").as_string();r.last=data.get("last").as_string();
    r.password=data.contains("passwd")?data.get("passwd").as_string():"$1$"+data.get("web_login_key").as_string();
    r.scope=normalize_uuid(optional(data,"scope_id",std::string(ZeroUuid)));
    if(data.contains("start")&&data.get("start").kind==Value::Kind::Undefined)throw std::invalid_argument("Null XML-RPC start location");
    r.start=optional(data,"start");r.version=optional(data,"version","Unknown");
    r.channel=optional(data,"channel","Unknown");r.mac=optional(data,"mac","Unknown");r.id0=optional(data,"id0","Unknown");r.remote_ip=ip;
    return service_.login(r);
}
Value LoginHandlers::llsd_login(const Value& data,const std::string& ip) {
    if(data.kind!=Value::Kind::Map||!data.contains("first")||!data.contains("last")||!data.contains("passwd"))return login_failure("InvalidLLSD");
    LoginRequest r;
    r.llsd=true;
    r.first=llsd_string(data.get("first"));r.last=llsd_string(data.get("last"));r.password=llsd_string(data.get("passwd"));
    r.scope=data.contains("scope_id")?normalize_uuid(llsd_string(data.get("scope_id"))):std::string(ZeroUuid);r.start=llsd_string(data.get("start"));
    r.version=llsd_string(data.get("version"));r.channel=llsd_string(data.get("channel"));r.mac=llsd_string(data.get("mac"));r.id0=llsd_string(data.get("id0"));r.remote_ip=ip;
    return service_.login(r);
}
Value LoginHandlers::set_level(const Value& data) {
    if(data.kind!=Value::Kind::Map||!nonnull(data,"first")||!nonnull(data,"last")||!nonnull(data,"passwd")||!nonnull(data,"level"))
        return Value::object({{"success",Value("false")}});
    auto level_text=data.get("level").as_string();int level;
    const auto start=level_text.find_first_not_of(" \t\r\n");
    if(start==std::string::npos)throw std::invalid_argument("Invalid login level");
    level_text=level_text.substr(start,level_text.find_last_not_of(" \t\r\n")-start+1);
    if(level_text.starts_with('+'))level_text.erase(0,1);
    auto parsed=std::from_chars(level_text.data(),level_text.data()+level_text.size(),level);
    if(parsed.ec!=std::errc{}||parsed.ptr!=level_text.data()+level_text.size())throw std::invalid_argument("Invalid login level");
    return service_.set_level(data.get("first").as_string(),data.get("last").as_string(),data.get("passwd").as_string(),level);
}
}
