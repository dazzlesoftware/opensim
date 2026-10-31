// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_copy.hpp"
#include "opensim/scene/user_inventory_operations.hpp"
#include "opensim/framework/animation_set.hpp"
#include <bit>
#include <stdexcept>
namespace opensim::scene {
namespace {std::int32_t s(std::uint32_t n){return std::bit_cast<std::int32_t>(n);}std::uint32_t u(int n){return static_cast<std::uint32_t>(n);}int byte(int n){return std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(n));}}
void animation_set_create_permissions(TaskInventoryAgentItem* value){framework::AnimationSet::set_create_item_permissions(value?&value->item:nullptr);}
void animation_set_enforce_permissions(TaskInventoryAgentItem* value,bool creator){framework::AnimationSet::enforce_item_permissions(value?&value->item:nullptr,creator);}
InventoryCopyController::InventoryCopyController(InventoryCopyBackend b):backend_(std::move(b)){if(!backend_.item||!backend_.bypass_permissions||!backend_.can_copy_user||!backend_.unix_time||!backend_.random_uuid||!backend_.add||!backend_.create_update||!backend_.alert)throw std::invalid_argument("Incomplete inventory copy backend");}
void InventoryCopyController::create(framework::Uuid agent,std::uint32_t callback,const TaskInventoryAgentItem& parameters)const{
 auto item=std::make_shared<TaskInventoryAgentItem>();auto& out=item->item;const auto& input=parameters.item;out.owner=agent;out.creator_id=input.creator_id;item->creator_data=parameters.creator_data;out.id=backend_.random_uuid();out.asset_id=input.asset_id;out.name=user_inventory_text(input.name);out.description=user_inventory_text(input.description,true);out.flags=input.flags;out.asset_type=byte(input.asset_type);out.inventory_type=byte(input.inventory_type);out.parent=input.parent;out.current_permissions=input.current_permissions;out.next_permissions=input.next_permissions;out.everyone_permissions=input.everyone_permissions;out.group_permissions=input.group_permissions;out.base_permissions=input.base_permissions;out.creation_date=input.creation_date;
 if(out.inventory_type==96)animation_set_enforce_permissions(item.get(),true);
 if(backend_.add(item))backend_.create_update(agent,std::move(item),callback);else{backend_.alert(agent,"Failed to create item");if(backend_.warn)backend_.warn("Failed to add new inventory item");}
}
void InventoryCopyController::copy(framework::Uuid agent,std::uint32_t callback,framework::Uuid old_agent,framework::Uuid old_item,framework::Uuid folder,const std::optional<std::string>& requested_name)const{
 if(backend_.debug)backend_.debug("CopyInventoryItem received");InventoryCopyItem source;
 if(backend_.library){auto library=backend_.library();if(library)source=library->find(old_item);}
 if(!source){source=backend_.item(agent,old_item);if(!source){if(backend_.error)backend_.error("Failed to find inventory item");return;}if(!(u(source->item.current_permissions)&0x8000u))return;}
 auto name=requested_name;if(!name||name->empty())name=source->item.name;
 bool same=agent==old_agent;if(!same&&backend_.library){auto library=backend_.library();same=library&&old_agent==library->owner;}
 if(!same){if(!(u(source->item.current_permissions)&0x2000u))return;if(!backend_.bypass_permissions()&&!backend_.can_copy_user(agent,old_item))return;}
 auto parameters=*source;parameters.item.parent=folder;parameters.item.name=*name;if(!same){parameters.item.base_permissions=parameters.item.current_permissions=source->item.next_permissions;parameters.item.everyone_permissions=s(u(source->item.everyone_permissions)&u(source->item.next_permissions));}
 parameters.item.creation_date=backend_.unix_time();create(agent,callback,parameters);
}
void InventoryCopyController::link(framework::Uuid agent,std::uint32_t callback,framework::Uuid folder,framework::Uuid target,const std::optional<std::string>& name,const std::optional<std::string>& description,std::int8_t inventory_type,std::int8_t asset_type)const{
 if(!backend_.can_create_user||!backend_.has_presence)throw std::invalid_argument("Incomplete inventory link backend");
 if(!backend_.can_create_user(inventory_type,agent))return;
 if(!backend_.has_presence(agent)){if(backend_.error)backend_.error("Inventory link scene presence not found");return;}
 TaskInventoryAgentItem parameters;auto& item=parameters.item;item.creator_id=agent.str();parameters.creator_data=std::string{};item.parent=folder;item.name=name.value_or("");item.description=description.value_or("");item.asset_id=target;item.asset_type=asset_type;item.inventory_type=inventory_type;item.base_permissions=item.current_permissions=item.next_permissions=item.group_permissions=0x9e000;item.everyone_permissions=0x8e000;
 // The original bool overload ignores assetUpload=false and calls the normal
 // creation overload, including its ordinary AddInventoryItem notifications.
 item.creation_date=backend_.unix_time();create(agent,callback,parameters);
}
}
