// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/server/http_route_registry.hpp"
#include "opensim/services/agent_launch.hpp"
#include "opensim/services/simulation_agent_updates.hpp"
#include "opensim/services/simulation_objects.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace opensim::scene {
class RegionRuntime;
struct RegionInstantMessage;
} // namespace opensim::scene

namespace opensim::server {

inline constexpr std::uint8_t grid_moderation_dialog = 250;

class LocalRegionRouter final : public opensim::services::ISimulationLogin,
                                public opensim::services::ISimulationAgentUpdates,
                                public opensim::services::ISimulationObjects {
  public:
    void add(const std::shared_ptr<opensim::scene::RegionRuntime> &region);
    void remove(const std::shared_ptr<opensim::scene::RegionRuntime> &region);
    std::shared_ptr<opensim::scene::RegionRuntime> find(const opensim::framework::Uuid &id) const;
    std::shared_ptr<opensim::scene::RegionRuntime> find_by_handle(std::uint64_t handle) const;
    std::shared_ptr<opensim::scene::RegionRuntime> find_by_name(std::string_view name) const;

    void for_each_region(const std::function<void(opensim::scene::RegionRuntime &)> &action) const;

    bool deliver_to_viewer(const std::function<bool(opensim::scene::RegionRuntime &)> &deliver) const;

    bool deliver_grid_message(const opensim::scene::RegionInstantMessage &message) const;
    bool receive_grid_message(const opensim::scene::RegionInstantMessage &message) const;

    opensim::network::Value land_data(std::uint64_t handle, std::uint32_t x, std::uint32_t y) const;

    std::optional<opensim::network::HttpResponse> map_request(const opensim::network::HttpRequest &request) const;

    bool query_access(opensim::services::GridRegion &destination, const opensim::framework::Uuid &agent,
                      const std::optional<std::string> &home, bool teleport,
                      const opensim::framework::Vector3 &position, std::vector<opensim::framework::Uuid> &features,
                      opensim::services::LoginTransferContext &context, std::string &reason) override;

    bool create_agent(const opensim::services::GridRegion *source, opensim::services::GridRegion &destination,
                      const std::shared_ptr<opensim::framework::AgentCircuitData> &circuit, std::uint32_t flags,
                      opensim::services::LoginTransferContext &context, std::string &reason) override;

    std::shared_ptr<opensim::framework::AgentTransferScene> get_scene(const opensim::framework::Uuid &id) override;

    bool update_agent(opensim::services::GridRegion &destination,
                      const std::shared_ptr<opensim::framework::AgentData> &data,
                      opensim::framework::EntityTransferContext &context) override;

    bool update_agent(opensim::services::GridRegion &destination,
                      const std::shared_ptr<opensim::framework::AgentPosition> &data) override;

    bool create_object(opensim::services::GridRegion *destination, const opensim::framework::Vector3 &position,
                       std::shared_ptr<opensim::services::SimulationObject> object, bool local_call) override;

  private:
    std::vector<std::shared_ptr<opensim::scene::RegionRuntime>> snapshot() const;

    mutable std::mutex mutex_;
    std::map<opensim::framework::Uuid, std::weak_ptr<opensim::scene::RegionRuntime>> regions_;
};

} // namespace opensim::server
