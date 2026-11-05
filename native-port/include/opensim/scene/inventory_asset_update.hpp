// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include "opensim/services/inventory_store.hpp"
#include <functional>
namespace opensim::scene {
using InventoryAssetItem = std::shared_ptr<services::InventoryItem>;
struct InventoryAssetUpdateBackend {
    std::function<InventoryAssetItem(framework::Uuid, framework::Uuid)> item;
    std::function<bool(framework::Uuid, framework::Uuid, framework::Uuid)> edit_notecard, edit_script;
    std::function<std::int32_t(framework::Uuid, framework::Uuid)> asset_permissions;
    std::function<std::shared_ptr<framework::AssetBase>(const services::InventoryItem&, framework::AssetBytes,
                                                        framework::Uuid)>
        create_asset;
    std::function<std::optional<std::string>(std::shared_ptr<framework::AssetBase>)> store;
    std::function<bool(InventoryAssetItem)> update;
    std::function<void(framework::Uuid, const std::string&, bool)> agent_alert;
    std::function<void(framework::Uuid, const std::string&)> alert;
    std::function<void(const std::string&)> warn, error, console;
};
class InventoryAssetUpdateController {
  public:
    explicit InventoryAssetUpdateController(InventoryAssetUpdateBackend);
    framework::Uuid caps_update(framework::Uuid agent, framework::Uuid item, framework::AssetBytes) const;
    bool update(framework::Uuid owner, InventoryAssetItem, std::shared_ptr<framework::AssetBase>) const;

  private:
    InventoryAssetUpdateBackend backend_;
};
std::shared_ptr<framework::AssetBase> create_inventory_asset(const services::InventoryItem&,
                                                             framework::AssetBytes, framework::Uuid creator);
}
