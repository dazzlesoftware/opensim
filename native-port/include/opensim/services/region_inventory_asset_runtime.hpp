// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_asset_caps.hpp"
#include "opensim/services/scene_capability_events.hpp"
namespace opensim::services {
struct RegionInventoryAssetBackend {
    std::function<bool(framework::Uuid)> object_exists;
    std::function<bool(framework::Uuid, framework::Uuid)> can_edit_inventory;
    std::function<framework::Uuid(framework::Uuid, framework::Uuid, framework::Uuid,
                                  std::span<const std::uint8_t>)>
        update;
    std::function<void(std::string)> log;
    std::string region_name;
};
// Registers all generic inventory asset source aliases on each Caps, plus real one-shot binary
// upload routes and 30-second expiration. Quiesce HTTP before stop; Caps owns
// initial routes. Backend scene/asset dependencies must outlive stop.
class RegionInventoryAssetRuntime {
  public:
    RegionInventoryAssetRuntime(std::shared_ptr<SceneCapabilityEvents>, RegionInventoryAssetBackend);
    ~RegionInventoryAssetRuntime();
    RegionInventoryAssetRuntime(const RegionInventoryAssetRuntime&) = delete;
    RegionInventoryAssetRuntime& operator=(const RegionInventoryAssetRuntime&) = delete;
    void stop();
    std::size_t pending_uploads() const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
}
