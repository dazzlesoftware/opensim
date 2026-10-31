// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/agent_launch.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim;using namespace services;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Admission:ISimulationLogin {
    framework::AgentCircuitManager& circuits;framework::Uuid allowed;int queries=0,creates=0;
    Admission(framework::AgentCircuitManager& manager,framework::Uuid region):circuits(manager),allowed(region){}
    bool query_access(GridRegion& region,const framework::Uuid&,const std::optional<std::string>& home,bool teleport,const framework::Vector3&,std::vector<framework::Uuid>& features,LoginTransferContext& context,std::string& reason)override{
        ++queries;check(!home&&teleport&&features.empty(),"source access query arguments");check(context.inbound_version==0.8f&&context.outbound_version==0.8f&&context.wearables_count==-1,"fresh transfer context on each attempt");context.wearables_count=16;reason="access denied";return region.id==allowed;
    }
    bool create_agent(const GridRegion* source,GridRegion& region,const std::shared_ptr<framework::AgentCircuitData>& circuit,std::uint32_t flags,LoginTransferContext& context,std::string& reason)override{
        ++creates;check(!source&&region.id==allowed&&context.wearables_count==16,"query context reaches create-agent");check((flags&4096)!=0,"direct fallback uses ViaRegionID");circuits.add_new_circuit(circuit);reason="";return true;
    }
};
int main(){try{
    server::IniConfig config;SqliteRegionStore store(":memory:",true);GridService grid(store,config);
    const auto scope=framework::Uuid::random(),other_scope=framework::Uuid::random();
    std::shared_ptr<GridRegion> start;framework::Uuid allowed;
    for(int i=0;i<3;++i){auto region=std::make_shared<GridRegion>();region->id=framework::Uuid::random();region->name="Region"+std::to_string(i);region->scope=i==1?other_scope:scope;region->x=25600+i*256;region->y=25600;region->internal_endpoint=RegionEndpoint{"0.0.0.0",9000};region->external_host="127.0.0.1";
        auto row=std::make_shared<RegionData>(region_data(*region));row->scope=region->scope;row->data["flags"]=i?"6":"0";check(store.store(row),"store scoped launch region");if(i==0)start=region;if(i==2)allowed=region->id;
    }
    framework::AgentCircuitManager circuits;Admission admission(circuits,allowed);
    AgentLauncher launcher(&grid,&admission,nullptr,nullptr);
    const auto agent=framework::Uuid::random(),session=framework::Uuid::random();int factories=0;
    AgentLaunchRequest request;request.destination=start;request.account_scope=scope;request.where="last";request.flags=128;
    std::shared_ptr<framework::AgentCircuitData> created;
    request.make_agent=[&](const GridRegion& region,std::uint32_t code){++factories;check(region.id==start->id,"factory uses original destination");check(code<2147483647u,"circuit code follows Random.Next range");created=std::make_shared<framework::AgentCircuitData>();created->agent_id=agent;created->session_id=session;created->circuit_code=code;created->start_position={128,128,30};return created;};
    const auto result=launcher.launch(request);
    check(result.circuit==created&&result.destination->id==allowed&&result.where=="safe"&&result.reason.empty(),"scoped fallback admitted");
    check(factories==1&&admission.queries==2&&admission.creates==1,"one circuit reused and other-scope fallback excluded");
    check(circuits.authenticate_session(session,agent,created->circuit_code).authorised,"launched circuit retained by real credential manager");
    AgentLauncher unavailable(&grid,nullptr,nullptr,nullptr);const auto absent=unavailable.launch(request);
    check(!absent.circuit&&!absent.destination&&absent.where=="last"&&absent.reason.empty()&&factories==1,"missing connector stops before allocating circuit");
    request.account_scope=other_scope;
    const auto denied=launcher.launch(request);
    check(!denied.circuit&&denied.destination==start&&denied.reason=="access denied"&&denied.where=="last","failed attempts retain original destination and final reason");
    std::cout<<"PASS: scoped agent launch and circuit-manager handoff\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
