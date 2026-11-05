// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/agent_inventory_add.hpp"
namespace opensim::scene {
using InventoryCopyItem = std::shared_ptr<TaskInventoryAgentItem>;
struct InventoryCopyLibrary {
    framework::Uuid owner;
    std::function<InventoryCopyItem(framework::Uuid)> find;
};
struct InventoryCopyBackend {
    std::function<std::optional<InventoryCopyLibrary>()> library;
    std::function<InventoryCopyItem(framework::Uuid, framework::Uuid)> item;
    std::function<bool()> bypass_permissions;
    // Preserves source call order (agent,item), including the original facade's
    // parameter-name mismatch. No inferred source-owner lookup is substituted.
    std::function<bool(framework::Uuid, framework::Uuid)> can_copy_user;
    std::function<bool(std::int8_t, framework::Uuid)> can_create_user;
    std::function<bool(framework::Uuid)> has_presence;
    std::function<std::int32_t()> unix_time;
    std::function<framework::Uuid()> random_uuid = [] {
        return framework::Uuid::random();
    };
    std::function<bool(InventoryCopyItem)> add;
    std::function<void(framework::Uuid, InventoryCopyItem, std::uint32_t)> create_update;
    std::function<void(framework::Uuid, const std::string&)> alert;
    std::function<void(std::string)> debug, error, warn;
};
class InventoryCopyController {
  public:
    explicit InventoryCopyController(InventoryCopyBackend);
    void copy(framework::Uuid agent, std::uint32_t callback, framework::Uuid old_agent,
              framework::Uuid old_item, framework::Uuid folder, const std::optional<std::string>& name) const;
    // Complete non-bool Scene.CreateNewInventoryItem overload. Parameters carry
    // source fields, masks and time; source owner, identity, group and sale fields
    // are deliberately not copied into the freshly constructed item.
    void create(framework::Uuid agent, std::uint32_t callback,
                const TaskInventoryAgentItem& parameters) const;
    void link(framework::Uuid agent, std::uint32_t callback, framework::Uuid folder, framework::Uuid target,
              const std::optional<std::string>& name, const std::optional<std::string>& description,
              std::int8_t inventory_type, std::int8_t asset_type) const;

  private:
    InventoryCopyBackend backend_;
};
void animation_set_create_permissions(TaskInventoryAgentItem*);
void animation_set_enforce_permissions(TaskInventoryAgentItem*, bool creator);
}
