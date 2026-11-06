// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <functional>
#include <optional>
namespace opensim::framework {
// Complete OpenSim.Framework.TaskInventoryItem public data model. Optional
// strings retain CLR null vs empty. CreatorIdentification is the DB property;
// it intentionally does not clear old creator_data when assigning plain UUID.
struct TaskInventoryItem {
    explicit TaskInventoryItem(std::function<std::int32_t()> unix_time = {});
    Uuid asset_id;
    std::uint32_t creation_date = 0;
    Uuid creator_id;
    std::optional<std::string> creator_data = std::string{};
    std::uint32_t flags = 0;
    std::int32_t type = 0, inventory_type = 0;
    Uuid item_id, old_item_id, loaded_item_id, rezzer_id;
    std::optional<std::string> name, description;
    Uuid owner_id, last_owner_id, group_id, parent_id, parent_part_id;
    std::uint32_t base_permissions = 0x7fffffffu, current_permissions = 0x7fffffffu,
                  everyone_permissions = 0x7fffffffu, group_permissions = 0x7fffffffu,
                  next_permissions = 0x7fffffffu;
    Uuid permissions_granter;
    std::int32_t permissions_mask = 0;
    bool owner_changed = false, script_running = true;
    std::string creator_identification() const;
    void creator_identification(const std::optional<std::string> &);
    // C# strings are immutable. Value copying native optional strings preserves
    // MemberwiseClone semantics: subsequent property assignments are independent.
    TaskInventoryItem clone() const;
    void reset_ids(Uuid part, std::function<Uuid()> random_uuid = Uuid::random);
};
}
