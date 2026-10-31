// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/auth_info.hpp"
#include "opensim/network/service_protocol.hpp"
namespace opensim::server {
struct AuthenticationOperations {
 using Id=framework::Uuid;
 std::function<std::optional<std::string>(Id,std::string,int)> authenticate;
 std::function<bool(Id,std::string)> set_password,release;
 std::function<bool(Id,std::string,int)> verify;
 std::function<std::shared_ptr<services::AuthInfo>(Id)> get_info;
 std::function<bool(std::shared_ptr<services::AuthInfo>)> set_info;
};
class AuthenticationEndpoint {
public:
 AuthenticationEndpoint(AuthenticationOperations operations,const IniConfig& config):operations_(std::move(operations)),auth_(config,"AuthenticationService"),allow_get_(config.boolean("AuthenticationService","AllowGetAuthInfo",false)),allow_set_(config.boolean("AuthenticationService","AllowSetAuthInfo",false)),allow_password_(config.boolean("AuthenticationService","AllowSetPassword",false)){}
 network::HttpResponse handle(const network::HttpRequest& request);
private:
 AuthenticationOperations operations_;network::ServiceAuth auth_;bool allow_get_,allow_set_,allow_password_;
};
}
