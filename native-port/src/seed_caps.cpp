// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/seed_caps.hpp"
#include <stdexcept>
#include <unordered_set>
namespace opensim::server {
void SeedCapsHandler::process_request(const network::HttpRequest& request,network::HttpResponse& response){
    auto region=backend_.region_name();auto agent=backend_.caps().agent_id();
    backend_.debug("[CAPS]: Received SEED caps request in "+region.value_or("")+" for agent "+agent.str());
    if(request.method!="POST"||request.content_type!="application/llsd+xml"){response.status=404;return;}
    if(!backend_.caps().wait_for_activation()){response.status=503;response.headers["Retry-After"]="30";return;}
    if(!backend_.check_client(backend_.caps().agent_id(),request)){
        auto id=backend_.caps().agent_id();auto ip=request.remote_ip;if(ip.find(':')!=std::string::npos)ip="["+ip+"]";
        backend_.warn("[CAPS]: Unauthorized CAPS client "+id.str()+" from "+ip+":"+std::to_string(request.remote_port));response.status=403;return;
    }
    std::optional<services::Caps::Requested> requested;
    try{requested=backend_.deserialize(request);}catch(const std::exception&){response.status=400;return;}
    if(!requested)throw std::invalid_argument("Null requested OSD array");
    services::Caps::Requested valid;std::unordered_set<std::string> seen;
    for(const auto& name:*requested){if(!name)throw std::invalid_argument("Null requested OSD");const auto& text=*name;if(text.empty()||text=="SEED")continue;
        auto& caps=backend_.caps();
        if(text=="ViewerBenefits"){caps.flags|=services::Caps::ViewerBenefits;continue;}
        if(text=="VTPBR"){if(backend_.region_size_x()==256&&backend_.region_size_y()==256)caps.flags|=services::Caps::PBR|services::Caps::TPBR;else caps.flags|=services::Caps::PBR;continue;}
        if(text=="VETPBR"){caps.flags|=services::Caps::PBR|services::Caps::TPBR;continue;}
        if(text=="ObjectAnimation")caps.flags|=services::Caps::ObjectAnim;
        else if(text=="EnvironmentSettings")caps.flags|=services::Caps::WLEnv;
        else if(text=="ExtEnvironment")caps.flags|=services::Caps::AdvEnv;
        else if(text=="ModifyMaterialParams")caps.flags|=services::Caps::PBR;
        if(seen.insert(text).second)valid.push_back(text);
    }
    std::string output="<llsd><map>";backend_.caps().append_details_xml(&valid,&output);output+="</map></llsd>";
    response.body=std::move(output);response.status=200;backend_.caps().flags|=services::Caps::SentSeeds;
}
}
