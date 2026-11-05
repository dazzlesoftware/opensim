// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/agent_inventory_add.hpp"
#include <bit>
#include <stdexcept>
namespace opensim::scene {
namespace {
bool folder_type(std::int8_t value) {
    switch (value) {
    case -1:
    case 0:
    case 1:
    case 2:
    case 3:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 13:
    case 14:
    case 15:
    case 16:
    case 20:
    case 21:
    case 23:
    case 26:
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:
    case 50:
    case 51:
    case 52:
    case 53:
    case 54:
    case 56:
    case 57:
    case 100:
        return true;
    default:
        return false;
    }
}
}
services::InventoryItem inventory_storage_projection(const TaskInventoryAgentItem &value) {
    auto result = value.item;
    if (value.creator_data && !value.creator_data->empty())
        result.creator_id += ';' + *value.creator_data;
    return result;
}
AgentInventoryAddController::AgentInventoryAddController(AgentInventoryAddBackend b)
    : backend_(std::move(b)) {
    if (!backend_.add_item || !backend_.system_folder || !backend_.root_folder || !backend_.folder ||
        !backend_.is_god || !backend_.upload_complete || !backend_.client)
        throw std::invalid_argument("Incomplete agent inventory add backend");
}
bool AgentInventoryAddController::add(std::shared_ptr<TaskInventoryAgentItem> value, bool trigger) const {
    if (!value)
        throw std::invalid_argument("Null inventory item");
    auto &item = value->item;
    if (!item.parent.is_zero() && backend_.add_item(value)) {
        int level = backend_.is_god(item.owner) ? 1 : 0;
        if (trigger)
            backend_.upload_complete(value, level);
        return true;
    }
    auto original = item.parent;
    std::optional<services::InventoryFolder> folder;
    auto type = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(item.asset_type));
    if (folder_type(type))
        folder = backend_.system_folder(item.owner, type);
    if (folder) {
        if (backend_.debug)
            backend_.debug("Found system folder for inventory item");
        item.parent = folder->id;
    } else {
        folder = backend_.root_folder(item.owner);
        if (folder)
            item.parent = folder->id;
        else {
            if (backend_.warn)
                backend_.warn("Could not find root inventory folder");
            return false;
        }
    }
    if (backend_.add_item(value)) {
        int level = backend_.is_god(item.owner) ? 1 : 0;
        if (trigger)
            backend_.upload_complete(value, level);
        if (!original.is_zero())
            change_placement(value, *folder);
        return true;
    }
    if (backend_.warn)
        backend_.warn("Could not add inventory item");
    return false;
}
void AgentInventoryAddController::add(const AgentInventoryClient &client,
                                      std::shared_ptr<TaskInventoryAgentItem> value) const {
    add(value);
    client.create_update(std::move(value), 0);
}
void AgentInventoryAddController::change_placement(std::shared_ptr<TaskInventoryAgentItem> value,
                                                   const services::InventoryFolder &folder) const {
    auto client = backend_.client(value->item.owner);
    if (!client || !client->active)
        return;
    auto parent = backend_.folder(folder.owner, folder.parent);
    client->remove_items({value->item.id});
    client->bulk_item(value);
    std::string message = "The item was placed in folder " + folder.name;
    if (parent)
        message += " under " + parent->name;
    client->alert(message, false);
}
}
