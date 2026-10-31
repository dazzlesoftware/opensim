// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/agent_inventory_add.hpp"
#include "opensim/framework/asset_base.hpp"
namespace opensim::scene {
struct InventoryNewRequest {
 framework::Uuid agent,transaction,folder;
 std::uint32_t callback=0,next_permissions=0;
 std::optional<std::string> name,description;
 std::int8_t inventory_type=0,asset_type=0;
 std::uint8_t subtype=0;
 std::int32_t creation_date=0;
};
struct InventoryNewPresence {bool found=false,nonnull=false;};
struct InventoryNewLandmark {std::optional<std::string> text,prefix,suffix;};
struct InventoryNewBackend {
 std::function<bool(std::int8_t,framework::Uuid)> can_create;
 std::function<std::optional<services::InventoryFolder>(framework::Uuid,framework::Uuid)> folder;
 std::function<std::optional<services::InventoryFolder>(framework::Uuid,std::int8_t)> folder_type;
 std::function<std::optional<std::function<bool(const InventoryNewRequest&)>>()> transactions;
 std::function<InventoryNewPresence(framework::Uuid)> presence;
 std::function<InventoryNewLandmark(framework::Uuid)> landmark;
 std::function<std::shared_ptr<framework::AssetBase>(const InventoryNewRequest&,std::vector<std::uint8_t>)> create_asset;
 std::function<void(std::shared_ptr<framework::AssetBase>)> store_asset;
 std::function<std::optional<std::function<framework::Uuid(std::uint8_t)>>()> environment;
 // Full source Scene.CreateNewInventoryItem arguments, prior to normalization.
 std::function<void(const InventoryNewRequest&,framework::Uuid,std::uint32_t,std::uint32_t,std::uint32_t)> create;
 std::function<void(std::string)> debug,error;
};
class InventoryNewController {
public:
 explicit InventoryNewController(InventoryNewBackend);
 void create(InventoryNewRequest)const;
 static bool defined_folder(std::int8_t);
private:InventoryNewBackend backend_;
};
InventoryNewLandmark generate_inventory_landmark(framework::Uuid region,framework::Vector3 position,std::uint64_t handle);
}
