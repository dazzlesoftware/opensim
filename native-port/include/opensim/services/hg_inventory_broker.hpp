// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_store.hpp"
#include <functional>

namespace opensim::services {
// The URL lookup implements the scene/UserManagement policy; the broker keeps
// successful foreign URLs until the user's last active root leaves the host.
struct HGInventoryDependencies {
    std::function<std::optional<std::string>(framework::Uuid)> inventory_url;
    std::function<std::shared_ptr<InventoryService>(const std::string &)> connector;
};

class HGInventoryBroker final : public InventoryService {
  public:
    HGInventoryBroker(InventoryFolderStore &, std::unique_ptr<InventoryService>, HGInventoryDependencies);
    ~HGInventoryBroker();
    void client_closed(framework::Uuid, bool active_root_remaining);
    std::optional<std::string> inventory_url(framework::Uuid);
    bool create_user_inventory(const framework::Uuid &) override;
    std::optional<std::vector<InventoryFolder>> skeleton(const framework::Uuid &) override;
    std::optional<InventoryFolder> root(const framework::Uuid &) override;
    std::optional<InventoryFolder> for_type(const framework::Uuid &, int) override;
    std::optional<InventoryFolder> folder(const framework::Uuid &, const framework::Uuid &) override;
    std::optional<InventoryItem> item(const framework::Uuid &, const framework::Uuid &) override;
    std::optional<InventoryCollection> content(const framework::Uuid &, const framework::Uuid &) override;
    InventoryCollections multiple_content(const framework::Uuid &, const std::vector<framework::Uuid> &) override;
    InventoryItemResults multiple_items(const framework::Uuid &, const std::vector<framework::Uuid> &) override;
    std::optional<std::vector<InventoryItem>> folder_items(const framework::Uuid &, const framework::Uuid &) override;
    bool add_folder(InventoryFolder) override;
    bool update_folder(InventoryFolder) override;
    bool move_folder(const InventoryFolder &) override;
    bool add_item(const InventoryItem &) override;
    bool update_item(InventoryItem &) override;
    bool move_items(const framework::Uuid &, const std::vector<InventoryItem> &) override;
    bool delete_items(const framework::Uuid &, const std::vector<framework::Uuid> &) override;
    bool delete_folders(const framework::Uuid &, const std::vector<framework::Uuid> &, bool = true) override;
    bool purge_folder(const framework::Uuid &, bool = true) override;
    bool purge_folder(const InventoryFolder &, bool = true) override;
    std::optional<std::vector<InventoryItem>> active_gestures(const framework::Uuid &) override;
    int asset_permissions(const framework::Uuid &, const framework::Uuid &) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace opensim::services
