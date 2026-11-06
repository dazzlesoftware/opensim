// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/profile_requests.hpp"
#include "opensim/services/user_agent_connector.hpp"
namespace opensim::scene {
// Retains boxed XML-RPC values rather than parsing strings or widening numbers.
// The account helper performs its original strict Int32 unboxing afterwards.
class ConnectorProfileUserAgent final : public ProfileUserAgent {
    std::shared_ptr<services::UserAgentServiceConnector> connector_;

  public:
    explicit ConnectorProfileUserAgent(std::shared_ptr<services::UserAgentServiceConnector>);
    ConnectorProfileUserAgent(ProfileText home, services::UserAgentConnectorServices);
    std::shared_ptr<ProfileUserInfo> get_info(framework::Uuid) override;
};
}
