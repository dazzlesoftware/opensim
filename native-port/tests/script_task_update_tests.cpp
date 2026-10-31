// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/script_task_update.hpp"
#include <iostream>
using namespace opensim;using namespace scene;
namespace {
framework::Uuid id(int n){auto tail=std::to_string(n);return framework::Uuid("11223344-5566-7788-99aa-"+std::string(12-tail.size(),'0')+tail);}
std::string hex(std::span<const std::uint8_t> bytes){std::string out;for(auto b:bytes){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
std::string probe(int mode,int failure,int overload){std::string trace;bool stored_running=false;auto current_asset=id(9);auto step=[&](int point,std::string s){trace+="|"+s;if(point==failure)throw std::runtime_error("injected");};
 ScriptTaskUpdateClient client{id((mode&512)?6:1),[&](const auto& message,bool modal){step(2,"AL:"+message+":"+(modal?"1":"0"));}};
 ScriptTaskUpdateItem item;item.id=id((mode&8)?7:2);item.name="Script <name>";item.description="description & text";
 item.set_running=[&](bool value){step(6,std::string("RUN:")+(value?"1":"0"));stored_running=value;};item.set_asset=[&](auto a){step(10,"ASSET:"+a.str());current_asset=a;};item.asset=[&]{step(16,"GETASSET");return current_asset;};
 ScriptTaskUpdatePart part;part.local_id=123;
 part.get_item=[&](auto local,auto key)->std::optional<ScriptTaskUpdateItem>{step(5,"ITEM:"+std::to_string(local)+":"+key.str());if(mode&4)return {};return item;};
 part.remove_instance=[&](auto key,bool delete_state){step(9,"REMOVE:"+key.str()+":"+(delete_state?"1":"0"));};
 part.update_inventory=[&](const auto& v){step(11,"INV:"+v.id.str());};part.invalidate_permissions=[&]{step(12,"PERMS");};part.send_properties=[&](const auto& v){step(13,"PROPS:"+v.agent.str());};
 part.compile=[&](auto key,int start,bool post,const auto& engine,int source)->ScriptTaskErrors{step(14,"COMPILE:"+key.str()+":"+std::to_string(start)+":"+(post?"1":"0")+":"+engine+":"+std::to_string(source));if(mode&128)current_asset=id(5);if(mode&64)return {};if(mode&32)return std::vector<std::string>{"(1, 4): actual compiler error","another error"};return std::vector<std::string>{};};part.resume_scripts=[&]{step(18,"RESUME");};
 ScriptTaskUpdateBackend backend;backend.default_engine="YEngine";backend.can_edit_script=[&](auto i,auto o,auto a){step(1,"EDIT:"+i.str()+":"+o.str()+":"+a.str());return !(mode&1);};backend.find_part=[&](auto key)->std::optional<ScriptTaskUpdatePart>{step(3,"PART:"+key.str());if(mode&2)return {};return part;};
 backend.random_uuid=[&]{step(7,"NEW");return id(3);};backend.store=[&](auto asset){step(8,"STORE:"+asset->full_id().str()+":"+std::to_string(asset->type())+":"+asset->name().value_or("NULL")+":"+asset->description().value_or("NULL")+":"+asset->creator_id().value_or("NULL")+":"+hex(*asset->data));if(mode&16)asset->full_id(id(4));};
 backend.updated=[&](auto a,auto i,auto o,bool run,auto asset){step(17,"EVENT:"+a.str()+":"+i.str()+":"+o.str()+":"+(run?"1":"0")+":"+asset.str());};backend.find_client=[&](auto a)->std::optional<ScriptTaskUpdateClient>{step(19,"CLIENT:"+a.str());if(mode&256)return {};return client;};backend.log=[&](auto){step(4,"LOG");};
 ScriptTaskUpdateController controller(std::move(backend));framework::AssetBytes data;if(!(mode&1024))data=std::make_shared<std::vector<std::uint8_t>>((mode&2048)?std::vector<std::uint8_t>{}:std::vector<std::uint8_t>{0,1,255,42});
 std::string result;try{auto errors=overload?controller.update(id(1),id(2),id(8),bool(mode&4096),data):controller.update(client,id(2),id(8),bool(mode&4096),data);result=errors?"OK:"+std::to_string(errors->size()):"NULL";if(errors)for(const auto&e:*errors)result+=":"+e;}catch(...){result="THROW";}
 return result+":"+(stored_running?"1":"0")+":"+current_asset.str()+trace;
}
}
int main(int argc,char**argv){try{if(argc>1&&std::string_view(argv[1])=="--probe"){int m,f,o;while(std::cin>>m>>f>>o)std::cout<<probe(m,f,o)<<'\n';return 0;}
 int checks=0;auto check=[&](bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);};
 check(probe(0,0,0).find("|RUN:0|NEW|STORE:")!=std::string::npos,"running before store");check(probe(4096,8,0).starts_with("THROW:1:"),"store failure retains running mutation");check(probe(0,9,0).find("|ASSET:")==std::string::npos,"remove failure preserves old asset");check(probe(64,0,0).starts_with("NULL:"),"null errors preserved");check(probe(64,0,0).find("|RESUME")!=std::string::npos,"null errors still resumes");check(probe(1,0,0).find("Insufficient permissions to edit script:0")!=std::string::npos,"deny alert");check(probe(256,0,1).find("|EDIT:")==std::string::npos,"missing avatar skips edit");check(probe(128,0,0).find(":0:"+id(5).str()+"|RESUME")!=std::string::npos,"event reads item after compiler");
 std::cout<<"PASS: "<<checks<<" script task transaction checks\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
