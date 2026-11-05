// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/task_inventory_item.hpp"
#include "opensim/services/inventory_store.hpp"
namespace opensim::scene {
struct TaskInventoryAgentItem {
    services::InventoryItem item;
    std::optional<std::string> creator_data;
};
struct TaskInventoryMovePart {
    framework::Uuid id, owner;
    std::uint32_t local_id = 0;
};
struct TaskInventoryMoveClient {
    framework::Uuid agent;
    std::function<void(const std::optional<std::string>&, bool)> alert;
};
struct TaskInventoryMoveOutcome {
    std::shared_ptr<TaskInventoryAgentItem> item;
    std::optional<std::string> message;
};
struct TaskInventoryMoveBackend {
    std::function<std::optional<TaskInventoryMovePart>(std::uint32_t)> part;
    std::function<std::shared_ptr<framework::TaskInventoryItem>(const TaskInventoryMovePart&,
                                                                framework::Uuid)>
        task_item;
    std::function<bool(framework::Uuid, framework::Uuid, framework::Uuid)> can_copy;
    std::function<bool()> propagate_permissions, bypass_permissions;
    std::function<framework::Uuid()> random_uuid = [] { return framework::Uuid::random(); };
    std::function<std::int32_t()> unix_time;
    // Actual Scene.AddInventoryItem(client,item): includes real persistence,
    // fallback folder selection, notifications. Original void API ignores store
    // false and then still removes non-copy task items; never fabricate a store.
    std::function<void(const TaskInventoryMoveClient&, std::shared_ptr<TaskInventoryAgentItem>)> add_client;
    std::function<void(std::shared_ptr<TaskInventoryAgentItem>)> add_offline;
    std::function<std::optional<TaskInventoryMoveClient>(framework::Uuid)> client;
    std::function<void(const TaskInventoryMovePart&, framework::Uuid)> remove_script_events, remove_item;
    std::function<void(const TaskInventoryMovePart&, int)> active_script_count;
    std::function<void(std::string)> debug, warn, error;
};
class TaskInventoryMoveController {
public:
    explicit TaskInventoryMoveController(TaskInventoryMoveBackend);
    TaskInventoryMoveOutcome create_agent_item(framework::Uuid destination, const TaskInventoryMovePart&,
                                               framework::Uuid item) const;
    void remove_noncopy(const TaskInventoryMovePart&, framework::Uuid item) const;
    TaskInventoryMoveOutcome move(const TaskInventoryMoveClient&, framework::Uuid folder,
                                  const TaskInventoryMovePart&, framework::Uuid item) const;
    TaskInventoryMoveOutcome move(framework::Uuid avatar, framework::Uuid folder,
                                  const TaskInventoryMovePart&, framework::Uuid item) const;
    void client_move(const TaskInventoryMoveClient&, framework::Uuid folder, std::uint32_t local,
                     framework::Uuid item) const;

private:
    TaskInventoryMoveBackend backend_;
};
}
