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
#include "opensim/services/grid_service.hpp"
#include "opensim/framework/filesystem.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace opensim;using namespace services;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
GridRegion region(){GridRegion value;value.id=framework::Uuid::random();value.scope=framework::Uuid::random();value.name="A_%' Region";value.x=25600;value.y=25600;value.size_x=256;value.size_y=1024;value.internal_endpoint=RegionEndpoint{"0.0.0.0",9000};value.external_host="127.0.0.1";return value;}
int main(){auto path=std::filesystem::temp_directory_path()/("opensim-regions-"+framework::Uuid::random().str()+".db");const auto encoded=path.u8string();const std::string file(reinterpret_cast<const char*>(encoded.data()),encoded.size());try{
 bool threw=false;try{SqliteRegionStore missing(file);}catch(...){threw=true;}check(threw&&!std::filesystem::exists(path),"normal open does not initialize storage");auto model=region();auto original=std::make_shared<RegionData>(region_data(model));original->scope=model.scope;original->data["flags"]="7";
 {SqliteRegionStore store(file,true);check(store.store(original),"insert canonical region row");check(store.get(model.id,model.scope)&&!store.get(model.id,framework::Uuid::random())&&store.get(model.id,{}),"scope-filtered and unscoped lookup");check(store.at(model.x+1,model.y+700,model.scope)&&!store.at(model.x+256,model.y+700,model.scope),"non-square containment");check(store.range(model.x,model.y+700,model.x+1,model.y+701,model.scope).size()==1,"range uses independent Y size");check(store.find("A\\_\\%' Region",model.scope)->size()==1,"escaped wildcard and quote values bound");check(store.find("missing",{}).has_value()&&store.find("missing",{})->empty(),"SQL empty results are an empty collection");check(store.specific("a_%' region",model.scope)!=nullptr,"ASCII case-insensitive exact names");check(store.set(model.id,"PrincipalID",framework::Uuid::random().str()),"column update");const auto principal=store.get(model.id,{})->data.at("PrincipalID");auto row=store.get(model.id,{});row->name="Renamed";row->x+=256;row->data.erase("PrincipalID");check(store.store(row)&&store.get(model.id,{})->name==model.name&&store.get(model.id,{})->x==model.x+256,"existing row updates preserve provider name behavior");check(store.get(model.id,{})->data.at("PrincipalID")==principal,"unprovided columns survive update");row->scope=framework::Uuid::random();row->x+=4096;check(!store.store(row)&&store.get(model.id,{})->x==model.x+256,"conflicting scoped update leaves row intact");check(store.with_flags(DefaultRegion,model.scope).size()==1&&store.with_flags(Hyperlink,{}).empty(),"flag selection");bool rejected=false;try{store.set(model.id,"unknown;DROP TABLE regions","x");}catch(...){rejected=true;}check(rejected&&store.get(model.id,{}),"unknown identifier rejected");auto bad=std::make_shared<RegionData>();bad->id=framework::Uuid::random();try{store.store(bad);}catch(...){ }check(!store.get(bad->id,{}),"failed insert is atomic");}
 const auto size=std::filesystem::file_size(path);threw=false;try{SqliteRegionStore duplicate(file,true);}catch(...){threw=true;}check(threw&&std::filesystem::file_size(path)==size,"initialization refuses a populated database");
 {SqliteRegionStore reopened(file);auto row=reopened.get(model.id,model.scope);check(row&&row->size_y==1024&&row->x==model.x+256&&row->data.at("flags")=="7","row survives reopen");row->data["flags"]="21";reopened.store(row);server::IniConfig config;GridService grid(reopened,config,[]{return 1700000000;});check(grid.deregister_region(model.id)&&grid.by_id(model.scope,model.id)&&grid.flags(model.scope,model.id)==17,"persistent deregistration is durable offline state");check(reopened.erase(model.id)&&!reopened.erase(model.id),"delete row status");}
 std::filesystem::remove(path);std::cout<<"PASS: SQLite region persistence, queries and reopen\n";return 0;
}catch(const std::exception& error){std::filesystem::remove(path);std::cerr<<error.what()<<'\n';return 1;}}
