// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_store.hpp"
#include <functional>
namespace opensim::scene {
struct InventoryDropPresence {
    framework::Uuid id;
    bool deleted = false, god = false;
};
struct InventoryDropPart {
    framework::Uuid id, owner;
    bool has_group = true, group_deleted = false;
    std::uint32_t effective_owner_permissions = 0;
};
struct InventoryDropPermissionBackend {
    std::function<void()> debug;
    std::function<bool()> bypass;
    std::function<std::uint32_t(const InventoryDropPart&)> root_flags;
    std::function<std::uint32_t(framework::Uuid, const InventoryDropPart&, bool)> object_permissions;
};
// Complete PermissionsModule.CanDropInObjectInv(item,ScenePresence,part).
bool can_drop_object_inventory(const services::InventoryItem*, const std::optional<InventoryDropPresence>&,
                               const std::optional<InventoryDropPart>&,
                               const InventoryDropPermissionBackend&);
}
