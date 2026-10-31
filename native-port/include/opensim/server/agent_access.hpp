// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/agent_launch.hpp"
#include "opensim/network/http_server.hpp"
#include "opensim/server/agent_delete.hpp"
#include "opensim/server/agent_update.hpp"
namespace opensim::server {
struct AgentPathParameters{framework::Uuid agent,region;std::string action;};
bool agent_path_parameters(std::string_view path,AgentPathParameters& result);
// Parsed-OSD access/create-agent cores. Scene services supply actual admission.
class AgentAccessHandler {
public:
    explicit AgentAccessHandler(services::ISimulationLogin* service,AgentDeleteHandler* lifecycle=nullptr,AgentUpdateHandler* updates=nullptr):service_(service),lifecycle_(lifecycle),updates_(updates){}
    void process_admission(const network::HttpRequest& request,network::HttpResponse& response);
    void query_access(const network::Value& args,network::HttpResponse& response,const framework::Uuid& agent,const framework::Uuid& region);
    void post_agent(const network::Value& args,network::HttpResponse& response,const framework::Uuid& path_agent,const std::optional<std::string>& caller_address);
private:
    services::ISimulationLogin* service_;
    AgentDeleteHandler* lifecycle_;
    AgentUpdateHandler* updates_;
};
}
