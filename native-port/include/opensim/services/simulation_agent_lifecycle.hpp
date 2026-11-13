// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/grid_service.hpp"
#include <optional>
#include <string>
namespace opensim::services {
// Release/close portion of ISimulationService. Hosts retain scene ownership.
class ISimulationAgentLifecycle {
public:
    virtual ~ISimulationAgentLifecycle()=default;
    virtual bool release_agent(const framework::Uuid& region, const framework::Uuid& agent,
                               const std::string& uri) = 0;
    virtual bool close_agent(services::GridRegion& destination, const framework::Uuid& agent,
                             const std::optional<std::string>& auth) = 0;
};
}
