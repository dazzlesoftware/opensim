// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/profile_user_agent.hpp"
namespace opensim::scene {
ConnectorProfileUserAgent::ConnectorProfileUserAgent(
    std::shared_ptr<services::UserAgentServiceConnector> connector)
    : connector_(std::move(connector)) {
    if (!connector_)
        throw std::invalid_argument("Profile user-agent connector is required");
}
ConnectorProfileUserAgent::ConnectorProfileUserAgent(ProfileText home,
                                                     services::UserAgentConnectorServices services)
    : ConnectorProfileUserAgent(
          std::make_shared<services::UserAgentServiceConnector>(std::move(home), std::move(services))) {}
std::shared_ptr<ProfileUserInfo> ConnectorProfileUserAgent::get_info(framework::Uuid user) {
    auto values = connector_->get_user_info(user);
    auto result = std::make_shared<ProfileUserInfo>();
    for (const auto& key : values.ordered_keys())
        (*result)[key] = values.get(key);
    return result;
}
}
