// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/task_inventory_move.hpp"
namespace opensim::scene {
struct AgentInventoryClient {
 framework::Uuid agent;
 std::function<void(const std::vector<framework::Uuid>&)> remove_items;
 std::function<void(std::shared_ptr<TaskInventoryAgentItem>)> bulk_item;
 std::function<void(const std::string&,bool)> alert;
 std::function<void(std::shared_ptr<TaskInventoryAgentItem>,std::uint32_t)> create_update;
 bool active=true;
};
struct AgentInventoryAddBackend {
 std::function<bool(std::shared_ptr<TaskInventoryAgentItem>)> add_item;
 std::function<std::optional<services::InventoryFolder>(framework::Uuid,std::int8_t)> system_folder;
 std::function<std::optional<services::InventoryFolder>(framework::Uuid)> root_folder;
 std::function<std::optional<services::InventoryFolder>(framework::Uuid,framework::Uuid)> folder;
 std::function<bool(framework::Uuid)> is_god;
 std::function<void(std::shared_ptr<TaskInventoryAgentItem>,int)> upload_complete;
 std::function<std::optional<AgentInventoryClient>(framework::Uuid)> client;
 std::function<void(std::string)> debug,warn;
};
// XInventoryService.ConvertFromInventoryItem's CreatorIdentification storage
// representation. The DTO retains canonical CreatorId for viewer projection.
services::InventoryItem inventory_storage_projection(const TaskInventoryAgentItem&);
class AgentInventoryAddController {
public:
 explicit AgentInventoryAddController(AgentInventoryAddBackend);
 bool add(std::shared_ptr<TaskInventoryAgentItem>,bool trigger=true)const;
 void add(const AgentInventoryClient&,std::shared_ptr<TaskInventoryAgentItem>)const;
 void change_placement(std::shared_ptr<TaskInventoryAgentItem>,const services::InventoryFolder&)const;
private:AgentInventoryAddBackend backend_;
};
}
