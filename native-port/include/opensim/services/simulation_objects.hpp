// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/agent_transfer.hpp"
#include "opensim/services/region_store.hpp"
namespace opensim::services {
class SimulationObject:public framework::AgentTransferObject {
public:
    virtual framework::Uuid object_uuid()const=0;
    virtual std::shared_ptr<SimulationObject> clone_for_new_scene()=0;
    virtual std::optional<std::string> state_snapshot()=0;
    virtual void set_state(const std::optional<std::string>& state,framework::AgentTransferScene* scene)=0;
};
class ISimulationObjects {
public:
    virtual ~ISimulationObjects()=default;
    virtual std::shared_ptr<framework::AgentTransferScene> get_scene(const framework::Uuid& region)=0;
    virtual bool create_object(GridRegion* destination,const framework::Vector3& position,std::shared_ptr<SimulationObject> object,bool local_call)=0;
};
}
