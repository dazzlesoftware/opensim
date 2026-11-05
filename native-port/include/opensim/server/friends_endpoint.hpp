// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/service_protocol.hpp"
#include "opensim/services/friends_service.hpp"
namespace opensim::server {
struct FriendsOperations {
    using Friends = std::optional<std::vector<std::shared_ptr<services::FriendInfo>>>;
    std::function<Friends(framework::Uuid)> get;
    std::function<Friends(const std::string&)> get_string;
    std::function<bool(const std::string&, const std::string&, std::int32_t)> store;
    std::function<bool(framework::Uuid, const std::string&)> erase;
    std::function<bool(const std::string&, const std::string&)> erase_string;
};
class FriendsEndpoint {
  public:
    FriendsEndpoint(FriendsOperations operations, const IniConfig& config)
        : operations_(std::move(operations)), auth_(config, "FriendsService") {}
    network::HttpResponse handle(const network::HttpRequest& request);
    void set_status_handler(std::function<bool(framework::Uuid, framework::Uuid, bool)> status) {
        status_ = std::move(status);
    }
    void set_answer_handler(std::function<bool(framework::Uuid, framework::Uuid, std::string, bool)> answer) {
        answer_ = std::move(answer);
    }
    void
    set_offer_handler(std::function<bool(framework::Uuid, framework::Uuid, std::string, std::string)> offer) {
        offer_ = std::move(offer);
    }
    void set_termination_handler(std::function<bool(framework::Uuid, framework::Uuid)> terminate) {
        terminate_ = std::move(terminate);
    }
    void set_rights_handler(
        std::function<bool(framework::Uuid, framework::Uuid, std::int32_t, std::int32_t)> rights) {
        rights_ = std::move(rights);
    }

  private:
    FriendsOperations operations_;
    network::ServiceAuth auth_;
    std::function<bool(framework::Uuid, framework::Uuid, bool)> status_;
    std::function<bool(framework::Uuid, framework::Uuid, std::string, bool)> answer_;
    std::function<bool(framework::Uuid, framework::Uuid, std::string, std::string)> offer_;
    std::function<bool(framework::Uuid, framework::Uuid)> terminate_;
    std::function<bool(framework::Uuid, framework::Uuid, std::int32_t, std::int32_t)> rights_;
};
}
