// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/task_inventory_remove.hpp"
#include <stdexcept>
namespace opensim::scene {
TaskInventoryRemoveController::TaskInventoryRemoveController(TaskInventoryRemoveBackend backend)
    : backend_(std::move(backend)) {
    if (!backend_.part || !backend_.can_edit || !backend_.group_item || !backend_.trash_folder ||
        !backend_.move_to_user || !backend_.remove_script_events || !backend_.active_script_count ||
        !backend_.group_remove || !backend_.invalidate_effective || !backend_.properties)
        throw std::invalid_argument("Incomplete task inventory removal backend");
}
void TaskInventoryRemoveController::remove(const TaskInventoryRemoveClient& client, framework::Uuid item_id,
                                           std::uint32_t local) const {
    auto part = backend_.part(local);
    if (!part || !part->has_group)
        return;
    if (!backend_.can_edit(part->id, client.agent))
        return;
    auto item = backend_.group_item(*part, local, item_id);
    if (!item)
        return;
    auto trash = backend_.trash_folder(client.agent);
    if (!trash)
        throw std::invalid_argument("Null trash inventory folder");
    auto moved = backend_.move_to_user(client, *trash, *part, item_id);
    if (!moved.success) {
        if (backend_.warn)
            backend_.warn(item_id, moved.message);
        client.alert(moved.message, false);
        return;
    }
    if (backend_.group_item(*part, local, item_id)) {
        if (item->type == 10) {
            backend_.remove_script_events(*part, item_id);
            backend_.active_script_count(*part, -1);
        }
        backend_.group_remove(*part, local, item_id);
        backend_.invalidate_effective(*part);
    }
    backend_.properties(*part, client);
}
}
