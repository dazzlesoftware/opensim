// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_item_create.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
namespace opensim::scene {
namespace {
std::uint32_t crc(framework::Uuid id){const auto&b=id.bytes();auto le=[&](int i){return std::uint32_t(b[i])|(std::uint32_t(b[i+1])<<8)|(std::uint32_t(b[i+2])<<16)|(std::uint32_t(b[i+3])<<24);};return (std::uint32_t(b[0])<<24)+(std::uint32_t(b[1])<<16)+(std::uint32_t(b[2])<<8)+b[3]+(std::uint32_t(b[4])<<8)+b[5]+(std::uint32_t(b[6])<<24)+(std::uint32_t(b[7])<<16)+le(8)+le(12);}
std::vector<std::uint8_t> text(std::string_view s){framework::utf16(s);auto zero=s.find('\0');if(zero!=s.npos)s=s.substr(0,zero);std::size_t count=std::min<std::size_t>(254,s.size());if(count<s.size())while(count&&(static_cast<unsigned char>(s[count])&0xc0)==0x80)--count;if(!count)return {};std::vector<std::uint8_t> out(s.begin(),s.begin()+count);out.push_back(0);return out;}
}
network::lludp::Packet inventory_item_create(framework::Uuid agent,const services::InventoryItem& item,framework::Uuid transaction,std::uint32_t callback){
 using namespace network;auto packet=lludp::make_packet("UpdateCreateInventoryItem");packet.zerocoded=true;auto& a=packet.block("AgentData");a["AgentID"]=agent;a["SimApproved"]=true;a["TransactionID"]=transaction;auto b=lludp::make_block(packet.spec->blocks.at(1));framework::Uuid creator;framework::try_parse_sdk_uuid(item.creator_id,creator);
 b["ItemID"]=item.id;b["AssetID"]=item.asset_id;b["CreatorID"]=creator;b["BaseMask"]=std::uint64_t(static_cast<std::uint32_t>(item.base_permissions));b["Description"]=text(framework::trim_utf8(item.description).empty()?std::string{}:item.description);b["EveryoneMask"]=std::uint64_t(static_cast<std::uint32_t>(item.everyone_permissions));b["FolderID"]=item.parent;b["InvType"]=std::int64_t(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(item.inventory_type)));b["Name"]=text(item.name);b["NextOwnerMask"]=std::uint64_t(static_cast<std::uint32_t>(item.next_permissions));b["OwnerID"]=item.owner;b["OwnerMask"]=std::uint64_t(static_cast<std::uint32_t>(item.current_permissions));b["Type"]=std::int64_t(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(item.asset_type)));b["CallbackID"]=std::uint64_t(callback);b["GroupID"]=item.group_id;b["GroupOwned"]=item.group_owned!=0;b["GroupMask"]=std::uint64_t(static_cast<std::uint32_t>(item.group_permissions));b["Flags"]=std::uint64_t(static_cast<std::uint32_t>(item.flags)&0x2000ffu);b["SalePrice"]=std::int64_t(item.sale_price);b["SaleType"]=std::uint64_t(static_cast<std::uint8_t>(item.sale_type));b["CreationDate"]=std::int64_t(item.creation_date);
 auto checksum=crc(item.asset_id)+crc(item.group_id)+crc(item.owner)+crc(creator)+crc(item.id)+crc(item.parent)+static_cast<std::uint32_t>(std::get<std::int64_t>(b["Type"]))+static_cast<std::uint32_t>(std::get<std::int64_t>(b["InvType"]))+1000u+100u+1u+4u*0x7fffffffu;b["CRC"]=std::uint64_t(checksum);packet.blocks["InventoryData"].push_back(std::move(b));return packet;
}
}
