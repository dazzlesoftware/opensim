// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/task_inventory_update.hpp"
#include "opensim/framework/field_string.hpp"
#include <stdexcept>
namespace opensim::scene {
TaskInventoryUpdateController::TaskInventoryUpdateController(TaskInventoryUpdateBackend b):backend_(std::move(b)){
 if(!backend_.part||!backend_.task_item||!backend_.inventory_item||!backend_.can_drop||!backend_.can_edit_object||!backend_.random_uuid||!backend_.add_group||!backend_.properties||!backend_.bypass_permissions||!backend_.remove_user_items||!backend_.can_edit_inventory||!backend_.is_god||!backend_.update_item)throw std::invalid_argument("Incomplete task inventory update backend");
}
void TaskInventoryUpdateController::update(const TaskInventoryUpdateClient& client,framework::Uuid transaction,std::shared_ptr<framework::TaskInventoryItem> input,std::uint32_t local)const{
 if(!input)throw std::invalid_argument("Null task inventory update");auto key=input->item_id;
 if(key.is_zero()){if(backend_.error)backend_.error("UpdateTaskInventory called with zero item ID");return;}
 auto part=backend_.part(local);if(!part){if(backend_.warn)backend_.warn("UpdateTaskInventory part not found");return;}
 auto current=backend_.task_item(*part,key);
 if(!current){
  auto item=backend_.inventory_item(client.agent,key);if(!item&&backend_.library_item)item=backend_.library_item(key);
  if(!item){if(backend_.error)backend_.error("UpdateTaskInventory user item not found");return;}
  if(!backend_.can_drop(*item,client,*part))return;
  bool modify=backend_.can_edit_object(*part,client);auto copy=backend_.random_uuid();backend_.add_group(client.agent,*part,local,*item,copy,modify);
  if(backend_.info)backend_.info("UpdateTaskInventory add requested");
  if(backend_.fetch_remote_assets)backend_.fetch_remote_assets(client.agent,*item);
  backend_.properties(*part,client);
  if(!backend_.bypass_permissions()&&!(static_cast<std::uint32_t>(item->current_permissions)&0x8000u))backend_.remove_user_items(client,{key});
  return;
 }
 if(!backend_.can_edit_inventory(part->id,client.agent))return;
 if(!transaction.is_zero()&&backend_.transaction_update)backend_.transaction_update(client,*part,transaction,current);
 current->base_permissions|=0x80000u;input->flags=current->flags;
 if(!backend_.is_god(client.agent)){
  bool no_change=false;
  if(client.agent!=part->owner){no_change=true;if(input->owner_id.is_zero()&&!input->group_id.is_zero()){
   if(!client.group_member)throw std::invalid_argument("Missing group membership provider");
   if(client.group_member(input->group_id)){if(!client.group_powers)throw std::invalid_argument("Missing group power provider");if(client.group_powers(input->group_id)&0x4000000000ull)no_change=false;}
  }}
  if(no_change){input->base_permissions=current->base_permissions;input->everyone_permissions=current->everyone_permissions;input->group_permissions=current->group_permissions;input->next_permissions=current->next_permissions;input->current_permissions=current->current_permissions;}
  else{
   input->base_permissions=current->base_permissions;
   if(input->everyone_permissions!=current->everyone_permissions)input->flags|=0x80000u;
   if(input->group_permissions!=current->group_permissions)input->flags|=0x40000u;
   if(input->current_permissions!=current->current_permissions)input->flags|=0x20000u;
   if(input->next_permissions!=current->next_permissions)input->flags|=0x100000u;
   input->everyone_permissions&=current->base_permissions;input->group_permissions&=current->base_permissions;input->current_permissions&=current->base_permissions;input->next_permissions&=current->base_permissions;
  }
 }else{
  if(input->base_permissions!=current->base_permissions)input->flags|=0x10000u;
  if(input->everyone_permissions!=current->everyone_permissions)input->flags|=0x80000u;
  if(input->group_permissions!=current->group_permissions)input->flags|=0x40000u;
  if(input->current_permissions!=current->current_permissions)input->flags|=0x20000u;
  if(input->next_permissions!=current->next_permissions)input->flags|=0x100000u;
 }
 input->next_permissions|=0x80000u;if(backend_.update_item(*part,input))backend_.properties(*part,client);
}
std::optional<TaskInventoryUpdateRequest> task_inventory_update_request(const network::lludp::Packet& packet){
 const auto& update=packet.block("UpdateData");if(std::get<std::uint64_t>(update.at("Key"))!=0)return {};
 const auto& data=packet.block("InventoryData");auto item=std::make_shared<framework::TaskInventoryItem>();
 auto uuid=[&](const char* key){return std::get<framework::Uuid>(data.at(key));};auto mask=[&](const char* key){return static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at(key)));};
 item->item_id=uuid("ItemID");item->parent_id=uuid("FolderID");item->creator_id=uuid("CreatorID");item->owner_id=uuid("OwnerID");item->group_id=uuid("GroupID");item->base_permissions=mask("BaseMask");item->current_permissions=mask("OwnerMask");item->group_permissions=mask("GroupMask");item->everyone_permissions=mask("EveryoneMask");item->next_permissions=mask("NextOwnerMask");item->type=static_cast<int>(std::get<std::int64_t>(data.at("Type")));item->inventory_type=static_cast<int>(std::get<std::int64_t>(data.at("InvType")));item->flags=mask("Flags");item->name=framework::field_to_string(std::get<std::vector<std::uint8_t>>(data.at("Name")));item->description=framework::field_to_string(std::get<std::vector<std::uint8_t>>(data.at("Description")));item->creation_date=static_cast<std::uint32_t>(std::get<std::int64_t>(data.at("CreationDate")));
 return TaskInventoryUpdateRequest{uuid("TransactionID"),static_cast<std::uint32_t>(std::get<std::uint64_t>(update.at("LocalID"))),std::move(item)};
}
}
