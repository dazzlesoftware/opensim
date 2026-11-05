// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/wearable_cache_item.hpp"
#include "opensim/server/config.hpp"
#include "opensim/framework/console_command_resolver.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/asset_cache.hpp"
#include "opensim/services/scene_capability_events.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <vector>
namespace opensim::network { class WebHttpClients; }
namespace opensim::services {
std::string serialize_baked_appearance(const framework::WearableCacheItems& cache);
framework::WearableCacheItems deserialize_baked_appearance(std::string_view xml);
std::shared_ptr<framework::AssetBase> deserialize_remote_asset(std::string_view xml);
std::shared_ptr<framework::AssetBase> request_remote_asset(
 const network::WebHttpClients&, const std::string& server, const framework::Uuid&);
// Construct before creating agent caps; stop after HTTP/poll requests and caps
// registration have been quiesced. Existing agent caps belong to SceneCapabilities
// and must be removed by that owner. Relative database/loader paths use process
// cwd, as in the original services, NOT IniConfig::directory.
class RegionAssetRuntime {
public:
 using AssetServerLookup = std::function<std::optional<std::string>(framework::Uuid)>;
 using StructuredLog=std::function<void(const std::string& level,const std::string& message)>;
 RegionAssetRuntime(const server::IniConfig&,std::shared_ptr<SceneCapabilityEvents>,
                    std::optional<std::string> region_name={},std::function<void(std::string)> log={},framework::Uuid region_id={},StructuredLog structured_log={},
                    std::shared_ptr<network::WebHttpClients> http={},AssetServerLookup foreign_server={});
 ~RegionAssetRuntime();
 RegionAssetRuntime(const RegionAssetRuntime&)=delete;
 RegionAssetRuntime& operator=(const RegionAssetRuntime&)=delete;
 std::shared_ptr<framework::AssetBase> get(const framework::Uuid&);
 std::shared_ptr<framework::AssetBase> get_foreign(const framework::Uuid&,
                                                  const std::optional<std::string>& server, bool store_local);
 std::optional<std::string> store(std::shared_ptr<framework::AssetBase>);
 std::shared_ptr<framework::IAssetCache> asset_cache()const;
 // Returns false when the Flotsam module is not selected.
 bool default_assets_command(bool remove);
 std::vector<std::string> cache_command_names()const;
 std::vector<framework::ConsoleCommandDescription> cache_command_descriptions()const;
 bool cache_console(const std::vector<std::string>&,std::function<void(const std::string&)> output);
 void stop();
private:
 struct State;
 std::unique_ptr<State> state_;
};
}
