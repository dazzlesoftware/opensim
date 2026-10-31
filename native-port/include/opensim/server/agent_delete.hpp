// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/simulation_agent_lifecycle.hpp"
#include "opensim/network/http_server.hpp"
#include <functional>
#include <memory>
namespace opensim::server {
// Auth lookup from the original Uri.Query -> ParseQueryString -> dictionary path.
std::optional<std::string> agent_delete_auth(std::string_view target);
class AgentDeleteHandler {
public:
    // Host supplies its FireAndForget policy (including exception handling).
    // Queued callbacks own the service and request values, never this handler.
    using Dispatch=std::function<void(std::function<void()>,const std::string&)>;
    AgentDeleteHandler(std::shared_ptr<services::ISimulationAgentLifecycle> service,Dispatch dispatch):service_(std::move(service)),dispatch_(std::move(dispatch)){}
    void delete_agent(network::HttpResponse& response,const framework::Uuid& agent,const std::optional<std::string>& action,const framework::Uuid& region,const std::optional<std::string>& auth,const std::optional<std::string>& caller_address);
private:
    std::shared_ptr<services::ISimulationAgentLifecycle> service_;
    Dispatch dispatch_;
};
}
