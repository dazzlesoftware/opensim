// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/agent_data.hpp"
#include "opensim/services/grid_service.hpp"
namespace opensim::services {
class ISimulationAgentUpdates {
public:
    virtual ~ISimulationAgentUpdates()=default;
    virtual std::shared_ptr<framework::AgentTransferScene> get_scene(const framework::Uuid& region)=0;
    virtual bool update_agent(GridRegion& destination, const std::shared_ptr<framework::AgentData>& data,
                              framework::EntityTransferContext& context) = 0;
    virtual bool update_agent(GridRegion& destination,const std::shared_ptr<framework::AgentPosition>& data)=0;
};
}
