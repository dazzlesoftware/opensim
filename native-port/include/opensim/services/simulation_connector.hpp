// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/agent_launch.hpp"
#include "opensim/services/simulation_agent_lifecycle.hpp"
#include "opensim/framework/agent_position.hpp"
#include "opensim/services/simulation_agent_updates.hpp"
#include "opensim/services/simulation_objects.hpp"
namespace opensim::network {
class OsdServiceRequester;
}
namespace opensim::services {
class SimulationConnectorError : public std::runtime_error {
  public:
    SimulationConnectorError(std::string type, std::u16string text);
    std::string source_type;
    std::u16string message;
};
struct SimulationPositionOptions {
    std::function<double()> clock;
    bool automatic_purge = true;
};
class SimulationConnector : public ISimulationLogin,
                            public ISimulationAgentLifecycle,
                            public ISimulationAgentUpdates,
                            public ISimulationObjects {
  public:
    using Request = std::function<network::Value(const std::string&, const network::Value&,
                                                 const std::string&, bool, bool)>;
    using DeleteRequest = std::function<network::Value(const std::optional<std::string>&, int)>;
    using PositionRequest =
        std::function<network::Value(const std::string&, const network::Value&, int, bool)>;
    explicit SimulationConnector(Request request = {}, DeleteRequest delete_request = {},
                                 PositionRequest position_request = {},
                                 SimulationPositionOptions options = {}, PositionRequest object_request = {});
    explicit SimulationConnector(std::shared_ptr<network::OsdServiceRequester> requester,
                                 SimulationPositionOptions options = {});
    ~SimulationConnector();
    bool create_object(GridRegion* destination, const framework::Vector3& position,
                       std::shared_ptr<SimulationObject> object, bool local_call) override;
    bool create_object(GridRegion& destination, const framework::Vector3& position,
                       std::shared_ptr<SimulationObject> object, bool local_call) {
        return create_object(&destination, position, std::move(object), local_call);
    }
    bool create_object(GridRegion*, const framework::Uuid&, const framework::Uuid&) {
        return false;
    }
    std::shared_ptr<framework::AgentTransferScene> get_scene(const framework::Uuid&) override {
        return {};
    }
    bool update_agent(GridRegion& destination, const std::shared_ptr<framework::AgentData>& data,
                      LoginTransferContext& context) override;
    bool update_agent(GridRegion* destination, const std::shared_ptr<framework::AgentData>& data,
                      LoginTransferContext* context);
    bool update_agent(GridRegion& destination,
                      const std::shared_ptr<framework::AgentPosition>& position) override;
    bool update_agent(GridRegion* destination, const std::shared_ptr<framework::AgentPosition>& position);
    void purge_failed_simulators();
    std::size_t failed_simulator_count() const;
    bool release_agent(const framework::Uuid& region, const framework::Uuid& agent,
                       const std::string& uri) override;
    bool release_agent(const framework::Uuid& region, const framework::Uuid& agent,
                       const std::optional<std::string>& uri);
    bool close_agent(GridRegion& destination, const framework::Uuid& agent,
                     const std::optional<std::string>& auth) override;
    bool close_agent(GridRegion* destination, const framework::Uuid& agent,
                     const std::optional<std::string>& auth);
    bool query_access(GridRegion& destination, const framework::Uuid& agent,
                      const std::optional<std::string>& home, bool via_teleport,
                      const framework::Vector3& position, std::vector<framework::Uuid>& features,
                      LoginTransferContext& context, std::string& reason) override;
    bool create_agent(const GridRegion* source, GridRegion& destination,
                      const std::shared_ptr<framework::AgentCircuitData>& circuit, std::uint32_t flags,
                      LoginTransferContext& context, std::string& reason) override;
    bool query_access(GridRegion* destination, const framework::Uuid& agent,
                      const std::optional<std::string>& home, bool via_teleport,
                      const framework::Vector3& position, std::vector<framework::Uuid>* features,
                      LoginTransferContext* context, std::string& reason);
    bool create_agent(const GridRegion* source, GridRegion* destination,
                      const std::shared_ptr<framework::AgentCircuitData>& circuit, std::uint32_t flags,
                      LoginTransferContext* context, std::string& reason);

  protected:
    bool send_agent_data(GridRegion* destination, const std::shared_ptr<framework::IAgentData>& data,
                         LoginTransferContext* context, int timeout);
    bool send_agent_position(GridRegion* destination,
                             const std::shared_ptr<framework::AgentPosition>& position,
                             LoginTransferContext* context, int timeout);
    virtual std::string agent_path() const {
        return "agent/";
    }
    virtual std::string object_path() const {
        return "object/";
    }
    virtual void pack_data(network::Value& args, const GridRegion* source,
                           framework::AgentCircuitData& circuit, const GridRegion& destination,
                           std::uint32_t flags);

  private:
    Request request_;
    DeleteRequest delete_request_;
    PositionRequest position_request_;
    PositionRequest object_request_;
    struct PositionState;
    std::unique_ptr<PositionState> position_state_;
};

class GatekeeperSimulationConnector final : public SimulationConnector {
  public:
    using SimulationConnector::SimulationConnector;

  protected:
    std::string agent_path() const override {
        return "foreignagent/";
    }
    std::string object_path() const override {
        return "foreignobject/";
    }
};
}
