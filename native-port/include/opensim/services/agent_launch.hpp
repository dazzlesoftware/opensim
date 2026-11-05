// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/agent_circuit.hpp"
#include "opensim/framework/entity_transfer_context.hpp"
#include "opensim/services/grid_service.hpp"
#include "opensim/network/rpc.hpp"
#include <functional>
#include <stdexcept>
namespace opensim::services {
using LoginTransferContext = framework::EntityTransferContext;
// Login admission operations from ISimulationService. Scene, update, crossing
// and object operations are not implied by this interface.
class ISimulationLogin {
  public:
    virtual ~ISimulationLogin() = default;
    virtual bool query_access(GridRegion& destination, const framework::Uuid& agent,
                              const std::optional<std::string>& home, bool via_teleport,
                              const framework::Vector3& position, std::vector<framework::Uuid>& features,
                              LoginTransferContext& context, std::string& reason) = 0;
    virtual bool create_agent(const GridRegion* source, GridRegion& destination,
                              const std::shared_ptr<framework::AgentCircuitData>& circuit,
                              std::uint32_t flags, LoginTransferContext& context, std::string& reason) = 0;
};
class IUserAgentLogin {
  public:
    virtual ~IUserAgentLogin() = default;
    virtual bool login_agent_to_grid(const GridRegion* source,
                                     const std::shared_ptr<framework::AgentCircuitData>& circuit,
                                     GridRegion& gatekeeper, GridRegion& destination, bool via_login,
                                     std::string& reason) = 0;
    virtual bool login_agent_to_grid_owned(const GridRegion* source,
                                           const std::shared_ptr<framework::AgentCircuitData>& circuit,
                                           const std::shared_ptr<GridRegion>& gatekeeper,
                                           const std::shared_ptr<GridRegion>& destination, bool via_login,
                                           std::string& reason) {
        if (!gatekeeper || !destination)
            throw std::invalid_argument("Owned user-agent login requires regions");
        return login_agent_to_grid(source, circuit, *gatekeeper, *destination, via_login, reason);
    }
};
struct AgentLaunchRequest {
    std::shared_ptr<GridRegion> gatekeeper, destination;
    framework::Uuid account_scope;
    std::string where;
    std::uint32_t flags = 0;
    // The caller supplies the full MakeAgent operation; it runs once per
    // launch, after connector selection. It must not fabricate appearance.
    std::function<std::shared_ptr<framework::AgentCircuitData>(const GridRegion&, std::uint32_t)> make_agent;
};
struct AgentLaunchResult {
    std::shared_ptr<framework::AgentCircuitData> circuit;
    std::shared_ptr<GridRegion> destination;
    std::string where, reason;
};
class AgentLauncher {
  public:
    using CircuitCode = std::function<std::uint32_t()>;
    AgentLauncher(GridService* grid, ISimulationLogin* local, ISimulationLogin* remote,
                  IUserAgentLogin* user_agent, std::string gatekeeper_uri = {},
                  CircuitCode circuit_code = {});
    AgentLaunchResult launch(const AgentLaunchRequest& request);

  private:
    static bool directly(ISimulationLogin& connector, GridRegion& region,
                         const std::shared_ptr<framework::AgentCircuitData>& circuit, std::uint32_t flags,
                         std::string& reason);
    void resolve_gatekeeper();
    GridService* grid_;
    ISimulationLogin *local_, *remote_;
    IUserAgentLogin* user_agent_;
    std::string gatekeeper_uri_, gatekeeper_host_;
    std::mutex gatekeeper_mutex_;
    int gatekeeper_port_ = 0;
    CircuitCode circuit_code_;
};
}
