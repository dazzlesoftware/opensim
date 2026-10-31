// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_item_create.hpp"
#include <iostream>
#include <bit>
using namespace opensim;using namespace scene;
namespace {
framework::Uuid id(int n){auto s=std::to_string(n);return framework::Uuid("11223344-5566-7788-99aa-"+std::string(12-s.size(),'0')+s);}
std::string hex(std::span<const std::uint8_t> v){std::string s;for(auto b:v){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
std::string text(int m){switch(m&7){case 0:return "";case 1:return "item <name> & description";case 2:return std::string(300,'a');case 3:{std::string s;for(int i=0;i<150;++i)s+="é";return s;}case 4:{std::string s;for(int i=0;i<100;++i)s+="中";return s;}case 5:{std::string s;for(int i=0;i<100;++i)s+="😃";return s;}case 6:return std::string("abc\0suffix",10);default:return " \t\n　";}}
std::string probe(int seed,int m){services::InventoryItem i;i.id=id(2);i.asset_id=id(3);i.parent=id(4);i.owner=id(5);i.group_id=id(6);i.creator_id=(m&32)?"invalid":id(7).str();if(m&64)i.creator_id+=";profile;name";i.name=text(m);i.description=text(m+1);i.asset_type=seed;i.inventory_type=-seed;i.base_permissions=-1;i.current_permissions=std::bit_cast<std::int32_t>(0x80000000u+static_cast<std::uint32_t>(seed));i.next_permissions=0x9e000;i.group_permissions=0x84000;i.everyone_permissions=0x8a000;i.flags=-1;i.group_owned=bool(m&8);i.sale_price=seed;i.sale_type=seed;i.creation_date=seed;auto p=inventory_item_create(id(1),i,(m&128)?id(8):framework::Uuid{},static_cast<std::uint32_t>(seed));return hex(network::lludp::encode_packet(p))+"|"+hex(network::lludp::encode_datagram(p));}
}
int main(int argc,char**argv){try{if(argc>1&&std::string_view(argv[1])=="--probe"){int s,m;while(std::cin>>s>>m)std::cout<<probe(s,m)<<'\n';return 0;}auto p=inventory_item_create(id(1),services::InventoryItem{});if(!p.zerocoded||!p.reliable||!std::get<bool>(p.block("AgentData").at("SimApproved")))throw std::runtime_error("source flags");std::cout<<"PASS: inventory item create projection\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
