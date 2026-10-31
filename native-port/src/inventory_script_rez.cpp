// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_script_rez.hpp"
namespace opensim::scene {
InventoryScriptRezController::InventoryScriptRezController(InventoryScriptRezBackend backend):backend_(std::move(backend)){}
void InventoryScriptRezController::rez(framework::Uuid agent,const services::InventoryItem& item,framework::Uuid,std::uint32_t local)const{
 auto part=item.id.is_zero()?backend_.rez_new(agent,item):rez_existing(agent,item.id,local);
 if(part)backend_.properties(*part,agent);
}
std::optional<InventoryScriptPart> InventoryScriptRezController::rez_existing(framework::Uuid agent,framework::Uuid item_id,std::uint32_t local)const{
 auto copy_id=backend_.random_uuid();auto item=backend_.inventory_item(agent,item_id);
 if(!item&&backend_.library_item)item=backend_.library_item(item_id);
 if(!item){if(backend_.log)backend_.log("[PRIM INVENTORY]: Could not find script inventory item to rez");return {};}
 auto part=backend_.scene_part(local);
 if(!part){if(backend_.log)backend_.log("[PRIM INVENTORY]: Could not find prim to rez script");return {};}
 if(!backend_.can_edit_inventory(part->id,agent))return {};
 add_to_group(agent,*part,local,*item,copy_id);
 backend_.compile(*part,copy_id,0,false,backend_.default_engine,0);
 backend_.new_script(agent,*part,copy_id);
 backend_.resume(*part);return part;
}
bool InventoryScriptRezController::add_to_group(framework::Uuid agent,const InventoryScriptPart& context,std::uint32_t local,const services::InventoryItem& source,framework::Uuid copy_id,bool modify)const{
 auto new_id=copy_id.is_zero()?source.id:copy_id;
 auto part=backend_.group_part(context.id,local);
 if(!part){if(backend_.log)backend_.log("[PRIM INVENTORY]: Could not find prim in group to add inventory item");return false;}
 auto task=std::make_shared<InventoryScriptTask>();task->item.creation_date=backend_.unix_time();
 auto& item=task->item;item.item=new_id;item.asset=source.asset_id;item.name=source.name;item.description=source.description;item.owner=part->owner;framework::try_parse_sdk_uuid(source.creator_id,item.creator);item.type=source.asset_type;item.inventory_type=source.inventory_type;item.flags=static_cast<std::uint32_t>(source.flags);
 auto next=static_cast<std::uint32_t>(source.next_permissions);item.base_permissions=static_cast<std::uint32_t>(source.base_permissions);item.current_permissions=static_cast<std::uint32_t>(source.current_permissions);item.everyone_permissions=static_cast<std::uint32_t>(source.everyone_permissions);item.group_permissions=static_cast<std::uint32_t>(source.group_permissions);item.next_permissions=next;
 if(agent!=part->owner&&backend_.propagate_permissions()){item.base_permissions&=next;item.current_permissions&=next;item.everyone_permissions&=next;item.group_permissions&=next;item.flags|=0x100u;item.last_owner=source.owner;}
 bool allowed_drop=modify?false:(backend_.root_flags(*part)&0x10000u)!=0;
 backend_.insert_task(*part,std::move(task),allowed_drop);backend_.invalidate_deep_permissions(*part);return true;
}
}
