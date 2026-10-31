// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/service_protocol.hpp"
#include "opensim/services/friends_service.hpp"
namespace opensim::server {
struct FriendsOperations {
    using Friends=std::optional<std::vector<std::shared_ptr<services::FriendInfo>>>;
    std::function<Friends(framework::Uuid)> get;
    std::function<Friends(const std::string&)> get_string;
    std::function<bool(const std::string&,const std::string&,std::int32_t)> store;
    std::function<bool(framework::Uuid,const std::string&)> erase;
    std::function<bool(const std::string&,const std::string&)> erase_string;
};
class FriendsEndpoint {
public:
    FriendsEndpoint(FriendsOperations operations,const IniConfig& config):operations_(std::move(operations)),auth_(config,"FriendsService"){}
    network::HttpResponse handle(const network::HttpRequest& request);
private:
    FriendsOperations operations_;network::ServiceAuth auth_;
};
}
