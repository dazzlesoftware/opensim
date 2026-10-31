// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/auth_info.hpp"
#include "opensim/network/rest_forms.hpp"
namespace opensim::services {
class AuthenticationConnector {
public:
 using Request=network::RestFormsRequest;
 explicit AuthenticationConnector(std::optional<std::string> uri=std::string{},Request request={});
 explicit AuthenticationConnector(const server::IniConfig& config,Request request={});
 void initialise(const server::IniConfig& config);
 std::string authenticate(framework::Uuid principal,const std::optional<std::string>& password,std::int32_t lifetime);
 std::string authenticate(framework::Uuid principal,const std::optional<std::string>& password,std::int32_t lifetime,framework::Uuid& real);
 bool verify(framework::Uuid principal,const std::optional<std::string>& token,std::int32_t lifetime);
 bool release(framework::Uuid principal,const std::optional<std::string>& token);
 bool set_password(framework::Uuid,const std::optional<std::string>&){return false;}
 std::shared_ptr<AuthInfo> get_auth_info(framework::Uuid){return {};}
 bool set_auth_info(const std::shared_ptr<AuthInfo>&){return false;}
private:
 using Fields=std::vector<std::pair<std::string,std::optional<std::string>>>;
 network::ServiceXmlValue send(const Fields& fields);
 void initialise_auth(const server::IniConfig& config);
 std::string uri_;Request request_;bool injected_;std::optional<std::pair<std::string,std::string>> auth_;
};
}
