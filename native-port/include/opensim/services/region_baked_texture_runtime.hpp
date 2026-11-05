// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/baked_texture_caps.hpp"
#include "opensim/services/scene_capability_events.hpp"
namespace opensim::services {
// Single-region executable binding. Source multi-region shared module lifecycle
// remains separate. Named Caps own first-stage routes; children expire globally.
class RegionBakedTextureRuntime {
  public:
    RegionBakedTextureRuntime(std::shared_ptr<SceneCapabilityEvents>, std::shared_ptr<framework::IAssetCache>,
                              std::string url, std::function<void(const std::string&)> log = {});
    ~RegionBakedTextureRuntime();
    RegionBakedTextureRuntime(const RegionBakedTextureRuntime&) = delete;
    RegionBakedTextureRuntime& operator=(const RegionBakedTextureRuntime&) = delete;
    void stop();
    std::size_t pending_uploads() const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
}
