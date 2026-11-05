// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/task_inventory_item.hpp"
#include "opensim/services/inventory_store.hpp"
#include "opensim/network/lludp.hpp"
namespace opensim::scene {
struct TaskInventoryUpdatePart {
    framework::Uuid id, owner;
    std::uint32_t local = 0;
};
struct TaskInventoryUpdateClient {
    framework::Uuid agent;
    std::function<bool(framework::Uuid)> group_member;
    std::function<std::uint64_t(framework::Uuid)> group_powers;
};
struct TaskInventoryUpdateBackend {
    using Item = std::shared_ptr<framework::TaskInventoryItem>;
    std::function<std::optional<TaskInventoryUpdatePart>(std::uint32_t)> part;
    std::function<Item(const TaskInventoryUpdatePart&, framework::Uuid)> task_item;
    std::function<std::optional<services::InventoryItem>(framework::Uuid, framework::Uuid)> inventory_item;
    std::function<std::optional<services::InventoryItem>(framework::Uuid)> library_item;
    std::function<bool(const services::InventoryItem&, const TaskInventoryUpdateClient&,
                       const TaskInventoryUpdatePart&)>
        can_drop;
    std::function<bool(const TaskInventoryUpdatePart&, const TaskInventoryUpdateClient&)> can_edit_object;
    std::function<framework::Uuid()> random_uuid = [] { return framework::Uuid::random(); };
    std::function<void(framework::Uuid, const TaskInventoryUpdatePart&, std::uint32_t,
                       const services::InventoryItem&, framework::Uuid, bool)>
        add_group;
    std::function<void(framework::Uuid, const services::InventoryItem&)> fetch_remote_assets;
    std::function<void(const TaskInventoryUpdatePart&, const TaskInventoryUpdateClient&)> properties;
    std::function<bool()> bypass_permissions;
    std::function<void(const TaskInventoryUpdateClient&, const std::vector<framework::Uuid>&)>
        remove_user_items;
    std::function<bool(framework::Uuid, framework::Uuid)> can_edit_inventory;
    // Empty means no AgentTransactionsModule, exactly as in the source scene.
    std::function<void(const TaskInventoryUpdateClient&, const TaskInventoryUpdatePart&, framework::Uuid,
                       Item)>
        transaction_update;
    std::function<bool(framework::Uuid)> is_god;
    std::function<bool(const TaskInventoryUpdatePart&, Item)> update_item;
    std::function<void(std::string)> error, warn, info;
};
class TaskInventoryUpdateController {
public:
    explicit TaskInventoryUpdateController(TaskInventoryUpdateBackend);
    void update(const TaskInventoryUpdateClient&, framework::Uuid transaction,
                std::shared_ptr<framework::TaskInventoryItem>, std::uint32_t local) const;

private:
    TaskInventoryUpdateBackend backend_;
};
struct TaskInventoryUpdateRequest {
    framework::Uuid transaction;
    std::uint32_t local;
    std::shared_ptr<framework::TaskInventoryItem> item;
};
// Exact LLClientView field projection. Key != 0 is an asset update and ignored.
std::optional<TaskInventoryUpdateRequest> task_inventory_update_request(const network::lludp::Packet&);
}
