// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_drop_permission.hpp"
#include <stdexcept>
namespace opensim::scene {
bool can_drop_object_inventory(const services::InventoryItem* item,const std::optional<InventoryDropPresence>& presence,const std::optional<InventoryDropPart>& part,const InventoryDropPermissionBackend& backend){
 if(backend.debug)backend.debug();
 if(!presence||presence->deleted||!part||!item)return false;
 if(!part->has_group||part->group_deleted)return false;
 if(!backend.bypass)throw std::invalid_argument("Missing inventory drop bypass provider");
 if(backend.bypass())return true;if(presence->god)return true;
 if(!(part->effective_owner_permissions&0x80000u))return false;
 bool other=presence->id!=part->owner;if(other&&item->inventory_type==10)return false;
 if((other||item->owner!=part->owner)&&!(static_cast<std::uint32_t>(item->current_permissions)&0x2000u))return false;
 if(!backend.root_flags)throw std::invalid_argument("Missing inventory drop root flags provider");if(backend.root_flags(*part)&0x10000u)return true;
 if(!backend.object_permissions)throw std::invalid_argument("Missing object permissions provider");return (backend.object_permissions(presence->id,*part,true)&0x4000u)!=0;
}
}
