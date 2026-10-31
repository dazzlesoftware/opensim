// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_properties.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
namespace opensim::scene {
namespace {
std::vector<std::uint8_t> limited(std::string_view text,std::size_t max){
 framework::utf16(text); // Reject invalid UTF-8 rather than invent C# strings.
 std::size_t length=0;while(length<text.size()&&text[length]){auto c=static_cast<unsigned char>(text[length]);std::size_t n=c<128?1:c<224?2:c<240?3:4;if(length+n>max)break;length+=n;}
 if(!length)return {};std::vector<std::uint8_t> bytes(text.begin(),text.begin()+length);bytes.push_back(0);return bytes;
}
}
network::lludp::Packet object_properties(const ObjectPropertiesData& o){
 using namespace network;auto packet=lludp::make_packet("ObjectProperties");auto b=lludp::make_block(packet.spec->blocks.at(0));
 b["ObjectID"]=o.id;b["CreatorID"]=o.creator;b["OwnerID"]=o.owner==o.group?framework::Uuid{}:o.owner;b["GroupID"]=o.group;
 b["CreationDate"]=static_cast<std::uint64_t>(static_cast<std::int64_t>(o.creation_date))*1000000ull;
 b["BaseMask"]=std::uint64_t(o.base_mask);b["OwnerMask"]=std::uint64_t(o.owner_mask);b["GroupMask"]=std::uint64_t(o.group_mask);b["EveryoneMask"]=std::uint64_t(o.everyone_mask);b["NextOwnerMask"]=std::uint64_t(o.next_owner_mask);
 b["SaleType"]=std::uint64_t(o.sale_type);b["SalePrice"]=std::int64_t(o.sale_price);b["Category"]=std::uint64_t(o.category);b["InventorySerial"]=std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(o.inventory_serial)));
 b["ItemID"]=o.from_inventory_item;b["LastOwnerID"]=o.last_owner;
 b["Name"]=limited(o.name,63);b["Description"]=limited(o.description,127);b["TouchName"]=limited(o.touch_name,36);b["SitName"]=limited(o.sit_name,36);
 // Source hardcodes ownership/aggregate permissions/folder/from-task/texture
 // fields to zero/empty. Generic block defaults encode those exact values.
 packet.blocks["ObjectData"].push_back(std::move(b));return packet;
}
}
