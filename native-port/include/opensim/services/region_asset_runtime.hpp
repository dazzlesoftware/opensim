// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/config.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/asset_cache.hpp"
#include "opensim/services/scene_capability_events.hpp"
#include <functional>
#include <memory>
#include <optional>
namespace opensim::services {
// Construct before creating agent caps; stop after HTTP/poll requests and caps
// registration have been quiesced. Existing agent caps belong to SceneCapabilities
// and must be removed by that owner. Relative database/loader paths use process
// cwd, as in the original services, NOT IniConfig::directory.
class RegionAssetRuntime {
public:
 RegionAssetRuntime(const server::IniConfig&,std::shared_ptr<SceneCapabilityEvents>,
                    std::optional<std::string> region_name={},std::function<void(std::string)> log={},framework::Uuid region_id={});
 ~RegionAssetRuntime();
 RegionAssetRuntime(const RegionAssetRuntime&)=delete;
 RegionAssetRuntime& operator=(const RegionAssetRuntime&)=delete;
 std::shared_ptr<framework::AssetBase> get(const framework::Uuid&);
 std::optional<std::string> store(std::shared_ptr<framework::AssetBase>);
 std::shared_ptr<framework::IAssetCache> asset_cache()const;
 void stop();
private:
 struct State;
 std::unique_ptr<State> state_;
};
}
