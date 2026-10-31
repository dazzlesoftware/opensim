// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/simulation_agent_updates.hpp"
#include "opensim/network/http_server.hpp"
namespace opensim::server {
class AgentUpdateHandler {
public:
    explicit AgentUpdateHandler(services::ISimulationAgentUpdates* service):service_(service){}
    virtual ~AgentUpdateHandler()=default;
    void put_agent(network::Value& args,network::HttpResponse& response);
protected:
    virtual bool update_agent(services::GridRegion& destination,const std::shared_ptr<framework::AgentData>& data);
private:
    services::ISimulationAgentUpdates* service_;
};
}
