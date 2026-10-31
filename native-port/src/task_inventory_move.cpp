// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/task_inventory_move.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <stdexcept>
namespace opensim::scene {
namespace {
std::int32_t signed_mask(std::uint32_t v){return std::bit_cast<std::int32_t>(v);}
std::string agent_text(const std::optional<std::string>& text,bool description){
 if(!text||text->empty()||(description&&framework::trim_utf8(*text).empty()))return {};
 framework::utf16(*text);auto value=*text;if(value.back()=='\0')value.pop_back();auto zero=value.find('\0');if(zero!=value.npos)for(std::size_t i=zero;i<value.size();++i)value[i]=0;return value;
}
}
TaskInventoryMoveController::TaskInventoryMoveController(TaskInventoryMoveBackend backend):backend_(std::move(backend)){
 if(!backend_.task_item||!backend_.propagate_permissions||!backend_.bypass_permissions||!backend_.random_uuid||!backend_.unix_time||!backend_.add_client||!backend_.remove_script_events||!backend_.active_script_count||!backend_.remove_item)throw std::invalid_argument("Incomplete task inventory move backend");
}
TaskInventoryMoveOutcome TaskInventoryMoveController::create_agent_item(framework::Uuid destination,const TaskInventoryMovePart& part,framework::Uuid key)const{
 auto task=backend_.task_item(part,key);if(!task){if(backend_.error)backend_.error("Task inventory item not found for avatar inventory copy");return {{},"Item not found: "+key.str()};}
 if(destination!=task->owner_id&&!(task->current_permissions&0x2000u))return {{},"Item doesn't have the Transfer permission."};
 auto agent=std::make_shared<TaskInventoryAgentItem>();agent->item.creation_date=backend_.unix_time();auto& item=agent->item;item.id=backend_.random_uuid();item.creator_id=task->creator_id.str();agent->creator_data=task->creator_data;item.owner=destination;item.asset_id=task->asset_id;item.description=agent_text(task->description,true);item.name=agent_text(task->name,false);item.asset_type=task->type;item.inventory_type=task->inventory_type;auto flags=task->flags;
 if(part.owner!=destination&&backend_.propagate_permissions()){
  auto perms=task->base_permissions&task->next_permissions;
  if(task->inventory_type==6){auto folded=task->current_permissions&0xfu;if(folded&&folded!=0xfu)perms&=(folded<<13)|~0x1e000u;if(!(perms&0xa000u))perms|=0x2000u;perms|=0x80000u;perms=(perms&~0xfu)|((perms>>13)&0xfu);}
  else perms&=task->current_permissions;
  perms|=0x80000u;item.base_permissions=signed_mask(perms);item.current_permissions=signed_mask(perms);item.next_permissions=signed_mask(perms&task->next_permissions);item.everyone_permissions=signed_mask(perms&task->everyone_permissions);item.group_permissions=signed_mask(perms&task->group_permissions);flags=(flags|0x100u)&~0x1f0000u;
 }else{item.base_permissions=signed_mask(task->base_permissions);item.current_permissions=signed_mask(task->current_permissions);item.next_permissions=signed_mask(task->next_permissions);item.everyone_permissions=signed_mask(task->everyone_permissions);item.group_permissions=signed_mask(task->group_permissions);}
 item.flags=signed_mask(flags);return {std::move(agent),{}};
}
void TaskInventoryMoveController::remove_noncopy(const TaskInventoryMovePart& part,framework::Uuid key)const{
 auto task=backend_.task_item(part,key);if(!task)return;if(!backend_.bypass_permissions()&&!(task->current_permissions&0x8000u)){
  if(task->type==10){backend_.remove_script_events(part,key);backend_.active_script_count(part,-1);}backend_.remove_item(part,key);
 }
}
TaskInventoryMoveOutcome TaskInventoryMoveController::move(const TaskInventoryMoveClient& client,framework::Uuid folder,const TaskInventoryMovePart& part,framework::Uuid key)const{
 if(backend_.debug)backend_.debug("Move task inventory item to client");auto outcome=create_agent_item(client.agent,part,key);if(!outcome.item)return outcome;outcome.item->item.parent=folder;backend_.add_client(client,outcome.item);remove_noncopy(part,key);outcome.message={};return outcome;
}
TaskInventoryMoveOutcome TaskInventoryMoveController::move(framework::Uuid avatar,framework::Uuid folder,const TaskInventoryMovePart& part,framework::Uuid key)const{
 if(!backend_.client)throw std::invalid_argument("Missing scene presence provider");auto client=backend_.client(avatar);if(client)return move(*client,folder,part,key);auto outcome=create_agent_item(avatar,part,key);if(!outcome.item)return outcome;outcome.item->item.parent=folder;if(!backend_.add_offline)throw std::invalid_argument("Missing offline inventory persistence provider");backend_.add_offline(outcome.item);remove_noncopy(part,key);return outcome;
}
void TaskInventoryMoveController::client_move(const TaskInventoryMoveClient& client,framework::Uuid folder,std::uint32_t local,framework::Uuid key)const{
 if(key.is_zero())return;if(!backend_.part||!backend_.can_copy)throw std::invalid_argument("Missing viewer inventory move provider");auto part=backend_.part(local);if(!part){if(backend_.warn)backend_.warn("Task inventory move part not found");return;}auto task=backend_.task_item(*part,key);if(!task){if(backend_.warn)backend_.warn("Task inventory move item not found");return;}if(!backend_.can_copy(key,part->id,client.agent))return;auto outcome=move(client,folder,*part,key);if(!outcome.item)client.alert(outcome.message,false);
}
}
