/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/services/inventory_store.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
using namespace opensim::services;
using opensim::framework::Uuid;
Uuid id(int value){std::ostringstream text;text<<"00000000-0000-0000-0000-"<<std::hex<<std::setfill('0')<<std::setw(12)<<value;return Uuid(text.str());}
int main(int argc,char** argv){if(argc==3&&std::string(argv[1])=="--initialize"){try{InventoryFolderStore store(argv[2],true);return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}int seed;while(std::cin>>seed){try{
    InventoryFolderStore store(":memory:",true);InventoryService service(store,seed%2==0);const auto owner=id(1),other=id(2);std::vector<std::string> observations;
    auto flag=[&](bool value){observations.push_back(value?"1":"0");};
    flag(service.create_user_inventory(owner));for(int i=0;i<seed%3;++i)flag(service.create_user_inventory(owner));const auto root=*service.root(owner);
    InventoryFolder folder{"Ordinary",-1,static_cast<int>(static_cast<std::uint16_t>(seed+1)),id(100),owner,root.id};flag(service.add_folder(folder));flag(service.add_folder(folder));folder.name="Edited";folder.version=0;flag(service.update_folder(folder));
    auto system=*service.for_type(owner,6);system.name="Override";system.version=static_cast<std::uint16_t>(system.version+seed);flag(service.update_folder(system));
    InventoryItem item;item.id=id(200);item.owner=owner;item.parent=folder.id;item.asset_id=id(300);item.asset_type=6;item.inventory_type=6;item.creator_id=owner.str()+";https://grid.invalid";item.name=std::string(70,'x');item.description="Description";item.current_permissions=(-2147483647-1)|0x4000;item.sale_type=255;item.group_owned=1;
    flag(service.add_item(item));auto initial=*service.item(other,item.id);observations.push_back(initial.creator_id);observations.push_back(std::to_string(initial.name.size()));
    item.owner=other;item.parent=root.id;item.asset_type=21;item.inventory_type=21;item.creator_id="different";item.name="Updated";item.asset_id=id(301);flag(service.update_item(item));flag(item.owner==owner&&item.parent==folder.id&&item.asset_type==6&&item.inventory_type==6);observations.push_back(item.creator_id);flag(service.item(owner,item.id)->asset_id==id(301));
    InventoryItem link;link.id=id(201);link.owner=owner;link.parent=folder.id;link.asset_id=id(300);link.asset_type=24;link.inventory_type=6;link.creator_id=owner.str();link.name="Link";flag(service.add_item(link));flag(service.update_item(link));flag(service.delete_items(other,{item.id,link.id}));flag(service.item(owner,item.id).has_value());flag(service.item(owner,link.id).has_value());
    auto contents=service.content(other,folder.id);flag(contents.owner==owner);observations.push_back(std::to_string(contents.items.size()));
    auto skeleton=*service.skeleton(owner);std::map<Uuid,std::string> names;for(const auto& value:skeleton)names[value.id]=value.name;std::vector<std::string> rows;
    for(const auto& value:skeleton)rows.push_back(value.name+":"+std::to_string(value.type)+":"+std::to_string(value.version)+":"+(value.parent.is_zero()?"-":names.contains(value.parent)?names[value.parent]:"?"));std::sort(rows.begin(),rows.end());observations.insert(observations.end(),rows.begin(),rows.end());
    for(std::size_t i=0;i<observations.size();++i){if(i)std::cout<<'|';std::cout<<observations[i];}std::cout<<'\n';
}catch(const std::exception& error){std::cout<<"ERROR:"<<error.what()<<'\n';}}}
