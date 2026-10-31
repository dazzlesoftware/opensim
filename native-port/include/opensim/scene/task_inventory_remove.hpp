// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/task_inventory_item.hpp"
#include <memory>
namespace opensim::scene {
struct TaskInventoryRemoveClient {framework::Uuid agent;std::function<void(const std::optional<std::string>&,bool)> alert;};
struct TaskInventoryRemovePart {framework::Uuid id;bool has_group=true;};
struct TaskInventoryMoveResult {bool success=false;std::optional<std::string> message;};
struct TaskInventoryRemoveBackend {
 std::function<std::optional<TaskInventoryRemovePart>(std::uint32_t)> part;
 std::function<bool(framework::Uuid,framework::Uuid)> can_edit;
 std::function<std::shared_ptr<framework::TaskInventoryItem>(const TaskInventoryRemovePart&,std::uint32_t,framework::Uuid)> group_item;
 std::function<std::optional<framework::Uuid>(framework::Uuid)> trash_folder;
 // Required real MoveTaskInventoryItem transaction: creates/persists/notifies
 // user inventory entry and removes non-copy task items according to source.
 std::function<TaskInventoryMoveResult(const TaskInventoryRemoveClient&,framework::Uuid,const TaskInventoryRemovePart&,framework::Uuid)> move_to_user;
 std::function<void(const TaskInventoryRemovePart&,framework::Uuid)> remove_script_events;
 std::function<void(const TaskInventoryRemovePart&,int)> active_script_count;
 // Routes through group.GetPart(local)->part.Inventory.RemoveInventoryItem.
 std::function<void(const TaskInventoryRemovePart&,std::uint32_t,framework::Uuid)> group_remove;
 std::function<void(const TaskInventoryRemovePart&)> invalidate_effective;
 std::function<void(const TaskInventoryRemovePart&,const TaskInventoryRemoveClient&)> properties;
 std::function<void(framework::Uuid,const std::optional<std::string>&)> warn;
};
class TaskInventoryRemoveController {
public:
 explicit TaskInventoryRemoveController(TaskInventoryRemoveBackend);
 // Original Scene.RemoveTaskInventory, not a raw registry deletion. Exceptions
 // propagate, including a missing Trash folder. No invented fallback folder.
 void remove(const TaskInventoryRemoveClient&,framework::Uuid item,std::uint32_t local)const;
private:TaskInventoryRemoveBackend backend_;
};
}
