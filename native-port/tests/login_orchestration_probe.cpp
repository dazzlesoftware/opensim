// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_orchestration.hpp"
#include "opensim/services/login_service.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim;using namespace services;
framework::Uuid id(int n){std::ostringstream out;out<<"00000000-0000-0000-0000-"<<std::hex<<std::setw(12)<<std::setfill('0')<<n;return framework::Uuid(out.str());}
struct FixturePresence:IPresenceService{
    int seed;bool active=false;std::string& events;
    FixturePresence(int value,std::string& trace):seed(value),events(trace){}
    bool login(const std::string&,const framework::Uuid&,const framework::Uuid&)override{events+="presence-login;";active=true;if(seed%5==2)throw std::runtime_error("login");return seed%5!=1;}
    bool logout(const framework::Uuid&)override{events+="presence-logout;";if(seed==59||seed==63)throw std::runtime_error("logout");active=false;return true;}
    bool logout_region(const framework::Uuid&)override{return true;}bool report(const framework::Uuid&,const framework::Uuid&)override{return true;}
    std::optional<PresenceInfo> agent(const framework::Uuid&)override{return {};}
    std::vector<PresenceInfo> agents(const std::vector<std::string>&)override{return {};}
};
int main(int argc,char** argv){try{
    const int seed=std::stoi(argv[1]);const auto config=server::IniConfig::load(argv[2]);
    std::string events;FixturePresence presence(seed,events);NullRegionStore store;GridService grid(store,config);
    GridRegion region;region.id=id(1);region.name="Region";region.x=region.y=25600;region.size_x=region.size_y=256;region.internal_endpoint=RegionEndpoint{"0.0.0.0",9000};region.external_host="127.0.0.1";
    auto row=std::make_shared<RegionData>(region_data(region));row->data["flags"]="5";store.store(row);
    std::optional<GridUserInfo> info;if(seed%4){info=GridUserInfo{};info->home_region=id(1);info->last_region=seed%4==3?framework::Uuid{}:id(1);info->online=seed%4>=2;info->home_position={128,128,30};info->last_position={1,2,3};}
    LoginDependencies dependencies;dependencies.grid=seed%7?&grid:nullptr;dependencies.presence=seed%5?&presence:nullptr;
    dependencies.grid_user=[&](const std::string&)->std::optional<GridUserInfo>{events+="grid-user;";return info;};
    dependencies.logged_out=[&](const std::string&,const framework::Uuid& session,const framework::Uuid&,const framework::Vector3&,const framework::Vector3&){events+="logout:"+session.str()+";";if(info)info->online=false;return true;};
    if(seed%6)dependencies.inventory=[&](const framework::Uuid&)->std::optional<std::vector<InventoryFolder>>{events+="inventory;";if(seed%6==4)throw std::runtime_error("inventory");if(seed%6==1)return {};return std::vector<InventoryFolder>(seed%6==2?0:seed%6==3?1:2);};
    if(seed&2)dependencies.suitcase_root=[&](const framework::Uuid&){events+="suitcase;";if(seed==7)throw std::runtime_error("suitcase");};
    dependencies.recover_duplicate=[&](const framework::Uuid&,const framework::Uuid&,const GridUserInfo& value){events+="recover;";if(seed%3==2)throw std::runtime_error("recover");if(seed%3==1)dependencies.logged_out(id(9).str(),{},value.last_region,value.last_position,value.last_look_at);return seed%3==1;};
    if(seed&128)dependencies.appearance=[&](const framework::Uuid& user)->std::shared_ptr<framework::AvatarAppearance>{events+="appearance:"+user.str()+";";if(seed&256)throw std::runtime_error("appearance");return seed&64?nullptr:std::make_shared<framework::AvatarAppearance>();};
    LoginOrchestrator orchestrator(dependencies,config);UserAccount account;account.principal_id=id(9).str();account.user_level=(seed&8)?200:0;
    const std::optional<std::string> start=seed%4==0?"home":seed%4==1?"last":seed%4==2?"uri:Region&1&2&3":"invalid";
    std::string result;
    try{
        auto prepared=orchestrator.prepare(account,id(8),id(10),id(11),start);
        if(auto* ready=std::get_if<PreparedLogin>(&prepared))result="READY|"+ready->destination.region->id.str()+"|"+std::to_string(ready->destination.flags)+"|"+std::to_string(ready->skeleton.size())+"|"+(ready->appearance?"1":"0");
        else{const auto value=std::get<network::Value>(prepared);for(auto name:{"AlreadyLoggedInProblem","InventoryProblem","GridProblem"})if(value.get("message").as_string()==login_failure(name).get("message").as_string())result=name;}
    }catch(const std::exception&){try{orchestrator.cleanup(id(10));result="InternalError";}catch(const std::exception&){result="THREW";}}
    std::cout<<result<<'|'<<events<<'|'<<(presence.active?1:0)<<'|'<<(info&&info->online?1:0)<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
