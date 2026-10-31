// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/config.hpp"
#include "opensim/services/inventory_caps.hpp"
#include "opensim/services/library_inventory_caps.hpp"
#include "opensim/services/scene_capability_events.hpp"
namespace opensim::services {
// Construct before agent caps. Host drives poll() and stops registration before
// stop(). Existing routes remain owned by Caps; remove them before destroying
// dependencies captured by the supplied inventory provider.
class RegionInventoryRuntime {
public:
 RegionInventoryRuntime(const server::IniConfig&,std::shared_ptr<SceneCapabilityEvents>,FetchInventory2Core::GetMultipleItems,std::optional<LibraryItemBackend> library={});
 ~RegionInventoryRuntime();
 RegionInventoryRuntime(const RegionInventoryRuntime&)=delete;
 RegionInventoryRuntime& operator=(const RegionInventoryRuntime&)=delete;
 void poll();
 void stop();
private:struct State;std::shared_ptr<State> state_;
};
}
