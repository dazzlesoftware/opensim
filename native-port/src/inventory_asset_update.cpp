// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_asset_update.hpp"
#include "opensim/framework/animation_set.hpp"
#include <bit>
#include <stdexcept>
namespace opensim::scene {
namespace {
bool stored(const std::optional<std::string>& value){return value&&!value->empty()&&*value!="00000000-0000-0000-0000-000000000000";}
std::int8_t asset_type(int value){return std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value));}
}
std::shared_ptr<framework::AssetBase> create_inventory_asset(const services::InventoryItem& item,framework::AssetBytes data,framework::Uuid creator){auto asset=std::make_shared<framework::AssetBase>(framework::Uuid::random(),item.name,asset_type(item.asset_type),creator.str());asset->description(item.description);asset->data=data?std::move(data):std::make_shared<std::vector<std::uint8_t>>(1);return asset;}
InventoryAssetUpdateController::InventoryAssetUpdateController(InventoryAssetUpdateBackend b):backend_(std::move(b)){if(!backend_.item||!backend_.edit_notecard||!backend_.edit_script||!backend_.asset_permissions||!backend_.create_asset||!backend_.store||!backend_.update||!backend_.agent_alert||!backend_.alert)throw std::invalid_argument("Incomplete inventory asset update backend");}
framework::Uuid InventoryAssetUpdateController::caps_update(framework::Uuid agent,framework::Uuid key,framework::AssetBytes data)const{
 auto item=backend_.item(agent,key);if(!item){if(backend_.error)backend_.error("[INVENTORY ACCESS MODULE]: Could not find item "+key.str()+" for caps inventory update");return {};}
 if(item->owner!=agent)return {};
 std::string report;
 switch(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(item->inventory_type))){
 case 7:if(!backend_.edit_notecard(key,{},agent)){backend_.agent_alert(agent,"Insufficient permissions to edit notecard",false);return {};}report="Notecard updated";break;
 case 10:if(!backend_.edit_script(key,{},agent)){backend_.agent_alert(agent,"Insufficient permissions to edit script",false);return {};}report="Script updated";break;
 case 96:{framework::AnimationSet animations(data,backend_.console);auto permissions=animations.validate([&](auto asset){auto result=backend_.asset_permissions(agent,asset);return (result&0xa000)==0xa000?static_cast<std::uint32_t>(result):0u;});if(permissions==0){backend_.agent_alert(agent,"Not enought permissions on asset(s) referenced by animation set '{0}', update failed",false);return {};}break;}
 case 20:if(!(static_cast<std::uint32_t>(item->current_permissions)&0x4000)){backend_.agent_alert(agent,"Insufficient permissions to edit gesture",false);return {};}report="gesture updated";break;
 case 25:if(!(static_cast<std::uint32_t>(item->current_permissions)&0x4000)){backend_.agent_alert(agent,"Insufficient permissions to edit setting",false);return {};}report="Setting updated";break;
 case 26:if(!(static_cast<std::uint32_t>(item->current_permissions)&0x4000)){backend_.agent_alert(agent,"Insufficient permissions to edit setting",false);return {};}break;
 default:break;
 }
 auto parameters=*item;parameters.asset_type=asset_type(parameters.asset_type);auto asset=backend_.create_asset(parameters,data,agent);if(!asset)throw std::invalid_argument("Null inventory asset");item->asset_id=asset->full_id();
 auto result=backend_.store(asset);
 if(!stored(result)){if(backend_.warn)backend_.warn("[INVENTORY ACCESS MODULE]: Failed to store asset "+asset->full_id().str()+", retrying...");result=backend_.store(asset);}
 if(stored(result)){backend_.update(item);if(!report.empty())backend_.alert(agent,report);return asset->full_id();}
 if(backend_.error)backend_.error("[INVENTORY ACCESS MODULE]: Failed to store asset "+asset->full_id().str()+", inventory item update ignored");backend_.alert(agent,"Update failed");return {};
}
bool InventoryAssetUpdateController::update(framework::Uuid owner,InventoryAssetItem item,std::shared_ptr<framework::AssetBase> asset)const{
 if(!item||!asset||item->owner!=owner){if(backend_.error)backend_.error("[INVENTORY ACCESS MODULE]: Given invalid item for inventory update: "+std::string(!item||!asset?"null item or asset":"wrong owner"));return false;}
 auto result=backend_.store(asset);
 if(!stored(result)){if(backend_.warn)backend_.warn("[INVENTORY ACCESS MODULE]: Failed to store asset "+asset->full_id().str()+", retrying...");result=backend_.store(asset);}
 if(stored(result)){backend_.update(item);return true;}
 if(backend_.error)backend_.error("[INVENTORY ACCESS MODULE]: Failed to store asset "+asset->full_id().str()+", inventory item not updated");return false;
}
}
