// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/user_inventory_operations.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/field_string.hpp"
#include <bit>
#include <chrono>
#include <limits>
#include <stdexcept>
namespace opensim::scene {
namespace {
std::uint32_t u(int n){return static_cast<std::uint32_t>(n);}std::int32_t s(std::uint32_t n){return std::bit_cast<std::int32_t>(n);}
std::optional<std::size_t> last_index(std::string_view text,std::string_view pattern){if(pattern.empty())return text.size();for(auto n=text.size();n>0;){--n;if((static_cast<unsigned char>(text[n])&0xc0)!=0x80&&framework::invariant_starts_with(text.substr(n),pattern))return n;}return {};}
}
std::string user_inventory_text(std::string_view text,bool description){framework::utf16(text);if(text.empty()||(description&&framework::trim_utf8(text).empty()))return {};std::string result(text);if(result.back()==0)result.pop_back();auto zero=result.find('\0');if(zero!=result.npos)for(auto n=zero;n<result.size();++n)result[n]=0;return result;}
std::int32_t user_inventory_unix_time(){double seconds=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();return seconds>=-2147483648.0&&seconds<2147483648.0?static_cast<std::int32_t>(seconds):std::numeric_limits<std::int32_t>::min();}
UserInventoryOperations::UserInventoryOperations(UserInventoryBackend b):backend_(std::move(b)){if(!backend_.item||!backend_.update||!backend_.move||!backend_.delete_items||!backend_.delete_folders||!backend_.bulk||!backend_.unix_time)throw std::invalid_argument("Incomplete user inventory backend");if(!backend_.starts_with)backend_.starts_with=framework::invariant_starts_with;if(!backend_.last_index)backend_.last_index=last_index;if(!backend_.creator)backend_.creator=[](const auto& item){framework::Uuid creator;framework::try_parse_sdk_uuid(item.creator_id,creator);return creator;};}
void UserInventoryOperations::update(framework::Uuid agent,framework::Uuid transaction,framework::Uuid key,UserInventoryItem input)const{
 auto item=backend_.item(agent,key);if(!item){if(backend_.error)backend_.error("Inventory item not found for update");return;}if(item->owner!=agent)return;if(!input)throw std::invalid_argument("Null incoming inventory item");bool send=false;
 item->flags=s((u(item->flags)&~255u)|(u(input->flags)&255u));
 if(item->asset_type==3){if(backend_.starts_with(item->name,"HG ")&&!backend_.starts_with(input->name,"HG ")){input->name=user_inventory_text("HG "+input->name);send=true;}auto at=backend_.last_index(item->description,"@ htt");if(at&&input->description.rfind('@')==std::string::npos){input->description=user_inventory_text(input->description+" "+item->description.substr(*at),true);send=true;}}
 item->name=user_inventory_text(input->name);item->description=user_inventory_text(input->description,true);
 if(u(input->next_permissions)!=0){
  bool deny=(u(item->base_permissions)&0x9e000u)!=0x9e000u||(u(item->current_permissions)&0x10000u)==0||backend_.creator(*item)!=item->owner;
  if(deny){if(u(item->everyone_permissions)&0x10000u){input->next_permissions=0x8e000;input->everyone_permissions=s(u(input->everyone_permissions)|0x10000u);}else input->everyone_permissions=s(u(input->everyone_permissions)&~0x10000u);}
  else if(u(input->everyone_permissions)&0x10000u)input->next_permissions=0x8e000;
  auto base=u(item->base_permissions);auto next=u(input->next_permissions)&base,everyone=u(input->everyone_permissions)&base,group=u(input->group_permissions)&base;
  if(u(item->next_permissions)!=next)item->flags=s(u(item->flags)|0x100000u);item->next_permissions=s(next);
  if(u(item->everyone_permissions)!=everyone)item->flags=s(u(item->flags)|0x80000u);item->everyone_permissions=s(everyone);
  if(u(item->group_permissions)!=group)item->flags=s(u(item->flags)|0x40000u);item->group_permissions=s(group);
  item->group_id=input->group_id;item->group_owned=input->group_owned;item->creation_date=input->creation_date;if(input->creation_date==0)item->creation_date=backend_.unix_time();else item->creation_date=input->creation_date;
  item->inventory_type=input->inventory_type;if(item->sale_price!=input->sale_price||item->sale_type!=input->sale_type)item->flags=s(u(item->flags)|0x1000u);item->sale_price=input->sale_price;item->sale_type=input->sale_type;
  if(item->inventory_type==18&&(u(item->flags)&15u)==0&&(u(input->flags)&15u)!=0){item->flags=s((u(item->flags)&0xfffffff0u)|(u(input->flags)&15u));send=true;}
  backend_.update(item);
 }
 if(!transaction.is_zero()){if(backend_.transaction)backend_.transaction(agent,transaction,item);}else if(send)backend_.bulk(agent,item);
}
void UserInventoryOperations::move(framework::Uuid agent,UserInventoryMoveList* list)const{
 if(!list)throw std::invalid_argument("Null inventory move list");if(backend_.debug)backend_.debug("Moving "+std::to_string(list->size())+" inventory items");if(!backend_.move(agent,*list)&&backend_.warn)backend_.warn("Failed to move inventory items");
 auto version=list->version();std::size_t n=0;for(;;){if(version!=list->version())throw std::runtime_error("Collection was modified; enumeration operation may not execute.");if(n>=list->size())break;auto input=list->at(n++);if(!input)throw std::invalid_argument("Null inventory move item");auto item=backend_.item(agent,input->id);if(item)backend_.bulk(agent,std::move(item));}
}
void UserInventoryOperations::remove_items(framework::Uuid agent,const std::vector<framework::Uuid>& keys)const{backend_.delete_items(agent,keys);}
void UserInventoryOperations::remove_folders(framework::Uuid agent,const std::vector<framework::Uuid>& keys)const{if(backend_.debug)backend_.debug("Removing "+std::to_string(keys.size())+" inventory folders");backend_.delete_folders(agent,keys);}
std::vector<UserInventoryUpdateRequest> user_inventory_update_requests(const network::lludp::Packet& packet){
 std::vector<UserInventoryUpdateRequest> requests;for(const auto& fields:packet.blocks.at("InventoryData")){
  auto item=std::make_shared<services::InventoryItem>();auto uuid=[&](const char* key){return std::get<framework::Uuid>(fields.at(key));};auto mask=[&](const char* key){return s(static_cast<std::uint32_t>(std::get<std::uint64_t>(fields.at(key))));};auto text=[&](const char* key,bool description){return user_inventory_text(framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at(key))),description);};
  item->id=uuid("ItemID");item->name=text("Name",false);item->description=text("Description",true);item->group_id=uuid("GroupID");item->group_owned=std::get<bool>(fields.at("GroupOwned"))?1:0;item->group_permissions=mask("GroupMask");item->next_permissions=mask("NextOwnerMask");item->everyone_permissions=mask("EveryoneMask");item->creation_date=static_cast<std::int32_t>(std::get<std::int64_t>(fields.at("CreationDate")));item->parent=uuid("FolderID");item->inventory_type=static_cast<int>(std::get<std::int64_t>(fields.at("InvType")));item->sale_price=static_cast<std::int32_t>(std::get<std::int64_t>(fields.at("SalePrice")));item->sale_type=static_cast<int>(std::get<std::uint64_t>(fields.at("SaleType")));item->flags=mask("Flags");requests.push_back({uuid("TransactionID"),item->id,std::move(item)});
 }return requests;
}
UserInventoryMoveList user_inventory_move_request(framework::Uuid agent,const network::lludp::Packet& packet){UserInventoryMoveList list;for(const auto& fields:packet.blocks.at("InventoryData")){auto item=std::make_shared<services::InventoryItem>();item->id=std::get<framework::Uuid>(fields.at("ItemID"));item->owner=agent;item->parent=std::get<framework::Uuid>(fields.at("FolderID"));item->name=user_inventory_text(framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("NewName"))));item->creation_date=user_inventory_unix_time();list.add(std::move(item));}return list;}
}
