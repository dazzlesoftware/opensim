// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/task_inventory_item.hpp"
#include <functional>
namespace opensim::scene {
struct SceneAssetClient {
 framework::Uuid agent;
 std::function<void(const std::string&,bool)> agent_alert;
 std::function<void(const std::string&)> alert;
};
struct SceneAssetPresence {std::function<std::shared_ptr<SceneAssetClient>()> client;};
struct SceneAssetPart {
 std::function<bool()> group_deleted;
 std::function<std::shared_ptr<framework::TaskInventoryItem>(framework::Uuid)> item;
 std::function<void(std::shared_ptr<framework::TaskInventoryItem>)> update;
};
using SceneInventoryAccessUpdate=std::function<framework::Uuid(std::shared_ptr<SceneAssetClient>,framework::Uuid,framework::AssetBytes)>;
struct SceneInventoryAssetBackend {
 // Empty means TryGetScenePresence=false; a contained null preserves the
 // source's separate found-but-null case and deferred client access.
 std::function<std::optional<std::shared_ptr<SceneAssetPresence>>(framework::Uuid)> presence;
 std::function<std::optional<SceneInventoryAccessUpdate>()> inventory_access;
 std::function<std::shared_ptr<SceneAssetPart>(framework::Uuid)> part;
 std::function<bool(framework::Uuid,framework::Uuid)> edit_inventory;
 std::function<bool(framework::Uuid,framework::Uuid,framework::Uuid)> edit_notecard;
 std::function<std::int32_t(framework::Uuid,framework::Uuid)> asset_permissions;
 std::function<std::shared_ptr<framework::AssetBase>(const std::optional<std::string>&,const std::optional<std::string>&,std::int8_t,framework::AssetBytes,framework::Uuid)> create_asset;
 std::function<void(std::shared_ptr<framework::AssetBase>)> store;
 std::function<void(const std::string&)> error,console;
};
class SceneInventoryAssetController {
public:
 explicit SceneInventoryAssetController(SceneInventoryAssetBackend);
 framework::Uuid update(framework::Uuid avatar,framework::Uuid item,framework::Uuid object,framework::AssetBytes)const;
private:SceneInventoryAssetBackend backend_;
};
std::shared_ptr<framework::AssetBase> create_scene_inventory_asset(const std::optional<std::string>& name,const std::optional<std::string>& description,std::int8_t type,framework::AssetBytes,framework::Uuid creator);
}
