// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_properties.hpp"
#include <iostream>
using namespace opensim;using namespace scene;
namespace {
framework::Uuid id(int n){auto tail=std::to_string(n);return framework::Uuid("11223344-5566-7788-99aa-"+std::string(12-tail.size(),'0')+tail);}
std::string hex(std::span<const std::uint8_t> bytes){std::string out;for(auto b:bytes){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
std::string text(int mode){switch(mode&7){case 0:return "";case 1:return "Object <name> & desc";case 2:return std::string(255,'a');case 3:{std::string s;for(int i=0;i<70;++i)s+="é";return s;}case 4:{std::string s;for(int i=0;i<70;++i)s+="中";return s;}case 5:{std::string s;for(int i=0;i<70;++i)s+="😃";return s;}case 6:return std::string("abc\0suffix",10);default:return "\r\n\t\"'é中😃";}}
ObjectPropertiesData data(int seed,int mode){ObjectPropertiesData o;o.id=id(1);o.creator=id(2);o.owner=id(3);o.group=(mode&8)?o.owner:id(4);o.last_owner=(mode&16)?framework::Uuid{}:id(5);o.from_inventory_item=(mode&32)?id(6):framework::Uuid{};o.creation_date=seed;
 if(mode&64){o.base_mask=0xffffffffu;o.owner_mask=0;o.group_mask=0x12345678;o.everyone_mask=0x87654321;o.next_owner_mask=0x80000000;}if(mode&128){o.sale_type=255;o.sale_price=seed;o.category=static_cast<std::uint32_t>(seed);o.inventory_serial=static_cast<std::uint32_t>(seed);}
 o.name=text(mode);o.description=text(mode+1);o.touch_name=text(mode+2);o.sit_name=text(mode+3);return o;}
std::string probe(int seed,int mode){return hex(network::lludp::encode_datagram(object_properties(data(seed,mode))));}
}
int main(int argc,char**argv){try{if(argc>1&&std::string_view(argv[1])=="--probe"){int s,m;while(std::cin>>s>>m)std::cout<<probe(s,m)<<'\n';return 0;}int checks=0;auto check=[&](bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);};auto p=object_properties(data(-1,255));const auto& b=p.block("ObjectData");check(p.reliable&&p.zerocoded,"source header");check(std::get<framework::Uuid>(b.at("OwnerID")).is_zero(),"group-owned viewer encoding");check(std::get<std::int64_t>(b.at("InventorySerial"))==-1,"serial wraps signed16");check(std::get<std::uint64_t>(b.at("CreationDate"))==std::uint64_t(-1)*1000000ull,"signed creation date cast");check(std::get<std::vector<std::uint8_t>>(object_properties(data(1,2)).block("ObjectData").at("Name")).size()==64,"namebyte limit+NUL");check(std::get<std::vector<std::uint8_t>>(object_properties(data(1,6)).block("ObjectData").at("Name")).size()==4,"embeddedNUL truncates");check(std::get<std::vector<std::uint8_t>>(object_properties(data(1,0)).block("ObjectData").at("Name")).empty(),"empty means zero bytes");std::cout<<"PASS: "<<checks<<" object properties checks\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
