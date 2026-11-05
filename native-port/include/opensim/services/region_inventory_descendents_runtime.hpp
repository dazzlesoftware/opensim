// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/config.hpp"
#include "opensim/services/inventory_descendents.hpp"
#include "opensim/services/scene_capability_events.hpp"
namespace opensim::services {
// Local FetchInventoryDescendents2 poll capability only. Caps owns registered
// routes; quiesce HTTP/poll dispatch before stop and remove Caps before releasing
// provider dependencies. Non-localhost URLs use source external registration.
class RegionInventoryDescendentsRuntime {
  public:
    RegionInventoryDescendentsRuntime(const server::IniConfig&, std::shared_ptr<SceneCapabilityEvents>,
                                      InventoryDescendentsBackend, std::function<bool()> shutting_down = {});
    ~RegionInventoryDescendentsRuntime();
    RegionInventoryDescendentsRuntime(const RegionInventoryDescendentsRuntime&) = delete;
    RegionInventoryDescendentsRuntime& operator=(const RegionInventoryDescendentsRuntime&) = delete;
    void stop();
    std::uint64_t processed_requests() const;
    std::size_t queued_requests() const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
}
