// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/authentication_connector.hpp"
namespace opensim::services {
namespace {
std::string field(const network::ServiceXmlValue& root,const std::string& name){for(const auto& [key,value]:root.fields)if(key==name)return value.list?"System.Collections.Generic.Dictionary`2[System.String,System.Object]":value.text;throw std::out_of_range("Missing authentication reply field: "+name);}
}
AuthenticationConnector::AuthenticationConnector(std::optional<std::string> uri,Request request):request_(std::move(request)),injected_(static_cast<bool>(request_)){
 if(!uri)throw std::invalid_argument("Null authentication URI");uri_=*uri;while(uri_.ends_with('/'))uri_.pop_back();if(!injected_)request_=network::rest_forms_transport();
}
AuthenticationConnector::AuthenticationConnector(const server::IniConfig& config,Request request):request_(std::move(request)),injected_(static_cast<bool>(request_)){initialise_auth(config);initialise(config);}
void AuthenticationConnector::initialise_auth(const server::IniConfig& config){
 if(config.get("AuthenticationService","AuthType",config.get("Network","AuthType","None"))=="BasicHttpAuthentication")auth_={{config.get("AuthenticationService","HttpAuthUsername",config.get("Network","HttpAuthUsername")),config.get("AuthenticationService","HttpAuthPassword",config.get("Network","HttpAuthPassword"))}};
 if(!injected_)request_=network::rest_forms_transport(auth_);
}
void AuthenticationConnector::initialise(const server::IniConfig& config){
 if(!config.has_section("AuthenticationService"))throw std::invalid_argument("AuthenticationService section missing");auto uri=config.get("AuthenticationService","AuthenticationServerURI");if(uri.empty())throw std::invalid_argument("Authentication server URI missing");uri_=std::move(uri);initialise_auth(config);
}
network::ServiceXmlValue AuthenticationConnector::send(const Fields& fields){return network::parse_service_xml(request_(uri_+"/auth/plain",network::build_service_form(fields)));}
std::string AuthenticationConnector::authenticate(framework::Uuid principal,const std::optional<std::string>& password,std::int32_t lifetime){auto reply=send({{"LIFETIME",std::to_string(lifetime)},{"PRINCIPAL",principal.str()},{"PASSWORD",password},{"METHOD","authenticate"}});if(field(reply,"Result")!="Success")return {};return field(reply,"Token");}
std::string AuthenticationConnector::authenticate(framework::Uuid principal,const std::optional<std::string>& password,std::int32_t lifetime,framework::Uuid& real){real={};return authenticate(principal,password,lifetime);}
bool AuthenticationConnector::verify(framework::Uuid principal,const std::optional<std::string>& token,std::int32_t lifetime){return field(send({{"LIFETIME",std::to_string(lifetime)},{"PRINCIPAL",principal.str()},{"TOKEN",token},{"METHOD","verify"}}),"Result")=="Success";}
bool AuthenticationConnector::release(framework::Uuid principal,const std::optional<std::string>& token){return field(send({{"PRINCIPAL",principal.str()},{"TOKEN",token},{"METHOD","release"}}),"Result")=="Success";}
}
