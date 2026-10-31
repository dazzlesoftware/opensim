// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/task_inventory_registry.hpp"
#include <iostream>
using namespace opensim;using namespace scene;using Item=TaskInventoryRegistry::Item;
namespace {
framework::Uuid id(int n){auto t=std::to_string(n);return framework::Uuid("11223344-5566-7788-99aa-"+std::string(12-t.size(),'0')+t);}
std::string row(const Item& i){return i->item_id.str()+":"+i->name.value_or("NULL")+":"+std::to_string(i->inventory_type)+":"+i->asset_id.str()+":"+i->owner_id.str()+":"+i->group_id.str()+":"+i->parent_id.str()+":"+std::to_string(i->group_permissions);}
Item make(int n,std::optional<std::string> name,int type){auto i=std::make_shared<framework::TaskInventoryItem>([]{return 111;});i->item_id=id(n);i->name=std::move(name);i->inventory_type=type;i->asset_id=id(n+20);i->owner_id=id(9);return i;}
std::string probe(int mode){std::string trace,out;auto add=[&](std::string s){trace+="/"+s;};TaskInventoryRegistryHooks hooks;hooks.part=[] {return TaskInventoryPart{id(10),id(11),123};};hooks.changed=[&](auto mask,auto item){add("C"+std::to_string(mask)+(item?":"+item->str():""));};hooks.aggregate_permissions=[&]{add("A");};hooks.deep_permissions=[&]{add("D");};hooks.clear_scripted=[&]{add("X");};hooks.schedule_full=[&]{add("F");};hooks.group_changed=[&]{add("H");};hooks.remove_script=[&](auto local,auto key){add("R"+std::to_string(local)+":"+key.str());};hooks.log=[&](auto){add("L");};TaskInventoryRegistry registry(std::move(hooks));registry.serial((mode&128)?0xffffffffu:0);
 auto snapshot=[&]{std::string s=std::to_string(registry.serial())+":"+(registry.inventory_changed()?"1":"0")+":"+(registry.has_script_dictionary()?"1":"0");for(auto&i:registry.items())s+="["+row(i)+"]";s+="S";for(auto&i:registry.scripts())s+="["+row(i)+"]";return s;};
 auto step=[&](auto body){trace="";std::string status;try{status=std::to_string(body());}catch(...){status="E";}out+="|"+status+trace+"#"+snapshot();};
 if(mode&256){for(int n=1;n<=256;++n)registry.add(make(n,"Pool",10));step([&]{return registry.add(make(300,"Pool",10));});step([&]{return registry.add(make(301,"pool",10));});auto live=registry.find(id(1));live->name={};step([&]{return registry.add(make(302,{},10));});live->item_id=id(999);step([&]{return registry.update(live);});step([&]{return registry.remove(id(1));});return out;}
 std::optional<std::string> name=(mode&1)?std::optional<std::string>{}:(mode&2)?std::optional<std::string>(""):std::optional<std::string>("Script");auto a=make(1,name,(mode&4)?7:10),b=make(2,name,10),c=make(3,"Different",10);
 step([&]{return registry.add(a,bool(mode&8));});step([&]{return registry.add(b);});step([&]{return registry.add(c);});
 step([&]{return registry.add((mode&16)?a:make(1,name,7));});
 registry.inventory_changed(false);step([&]{auto i=registry.find(id(2));if(!i)return false;i->asset_id={};i->name="Live";i->group_permissions=0;return registry.update(i,!(mode&32),!(mode&64));});
 step([&]{auto i=make(3,"Replacement",(mode&4)?10:7);i->asset_id={};i->owner_id={};i->group_id=id(12);i->group_permissions=(mode&32)?0:1;return registry.update(i,!(mode&32),!(mode&64));});
 step([&]{return registry.update(make(99,"Missing",10));});step([&]{return registry.remove(id(1));});step([&]{return registry.remove(id(2));});
 step([&]{return registry.add(make(4,"Slot",10));});step([&]{return registry.add(make(5,"Slot",7));});step([&]{return registry.remove(id(3));});step([&]{return registry.remove(id(5));});step([&]{return registry.add(make(6,"Slot",10));});step([&]{return registry.remove(id(99));});return out;
}
}
int main(int argc,char**argv){try{if(argc>1&&std::string_view(argv[1])=="--probe"){int mode;while(std::cin>>mode)std::cout<<probe(mode)<<'\n';return 0;}int checks=0;auto check=[&](bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);};check(probe(0).find("Script 1")!=std::string::npos,"duplicate name suffix");check(probe(1).find("|E#")!=std::string::npos,"null unique name throws");check(probe(2).find("|0#0:0:0S")!=std::string::npos,"empty name noinsert");check(probe(128).find("#0:1:1")!=std::string::npos,"serial wraps");std::cout<<"PASS: "<<checks<<" registry checks\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
