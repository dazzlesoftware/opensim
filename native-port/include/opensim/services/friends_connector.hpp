// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/services/friends_service.hpp"
#include "opensim/network/service_protocol.hpp"

namespace opensim::network {
class WebHttpClients;
}

namespace opensim::services {

class FriendsConnector : public IFriendsService {
public:
    using Request = std::function<std::string(const std::string&, const std::string&)>;

    explicit FriendsConnector(std::optional<std::string> uri = std::string{}, Request request = {});
    explicit FriendsConnector(const server::IniConfig& config,
                              Request request = {},
                              std::shared_ptr<network::WebHttpClients> clients = {});

    std::vector<FriendInfo> get(const framework::Uuid& principal) override;
    std::vector<FriendInfo> get(const std::optional<std::string>& principal) override;
    bool store(const std::optional<std::string>& principal,
               const std::optional<std::string>& friend_id,
               std::int32_t flags) override;
    bool erase(const framework::Uuid& principal, const std::optional<std::string>& friend_id) override;
    bool erase(const std::optional<std::string>& principal, const std::optional<std::string>& friend_id) override;

private:
    using Fields = std::vector<std::pair<std::string, std::optional<std::string>>>;

    std::vector<FriendInfo> get(const Fields& fields);
    bool mutation(const Fields& fields);

    std::string uri_;
    Request request_;
};

} // namespace opensim::services
