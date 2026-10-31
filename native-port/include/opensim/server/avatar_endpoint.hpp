// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/service_protocol.hpp"
#include "opensim/services/avatar_service.hpp"
namespace opensim::server {
struct AvatarOperations {
    using Id=framework::Uuid;
    using Strings=services::AvatarService::Strings;
    std::function<std::shared_ptr<services::AvatarData>(Id)> get;
    std::function<bool(Id,services::AvatarData&)> set;
    std::function<bool(Id)> reset;
    std::function<bool(Id,const Strings&,const Strings&)> set_items;
    std::function<bool(Id,const Strings&)> remove_items;
};
class AvatarEndpoint {
public:
    AvatarEndpoint(AvatarOperations operations,const IniConfig& config):operations_(std::move(operations)),auth_(config,"AvatarService"){}
    network::HttpResponse handle(const network::HttpRequest& request);
private:
    AvatarOperations operations_;
    network::ServiceAuth auth_;
};
}
