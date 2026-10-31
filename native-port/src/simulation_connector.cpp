// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/simulation_connector.hpp"
#include "opensim/network/osd_request.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/services/avatar_data.hpp"
namespace opensim::services {
using network::Value;using namespace network;
namespace {
Value real(float n){Value v;v.kind=Value::Kind::Real;v.real=n;return v;}
framework::Uuid constructor_uuid(std::string_view s){while(s.starts_with(' '))s.remove_prefix(1);if(s.size()<32)throw std::invalid_argument("Short UUID");return framework::Uuid(s.substr(0,s[8]=='-'?36:32));}
const Value& map(const Value& v){if(v.kind!=Value::Kind::Map)throw std::invalid_argument("Result is not a map");return v;}
}
bool SimulationConnector::create_object(GridRegion* destination,const framework::Vector3& position,std::shared_ptr<SimulationObject> object,bool){
    if(!destination)throw std::invalid_argument("Null destination");
    const auto server_uri=destination->server_uri();const auto path=object_path();
    if(!object)throw std::invalid_argument("Null simulation object");
    const auto uri=server_uri+path+object->object_uuid().str()+"/";
    try{
        auto args=Value::object({});args.set("sog",object->to_xml2());args.set("extra",object->extra_to_xml_string());args.set("modified",Value(object->has_group_changed()));args.set("new_position",framework::vector3_string(position));
        auto state=object->state_snapshot();if(!state)throw std::invalid_argument("Null object state snapshot");if(!state->empty())args.set("state",*state);
        args.set("destination_x",std::to_string(destination->x));args.set("destination_y",std::to_string(destination->y));args.set("destination_name",destination->name.value_or(""));args.set("destination_uuid",destination->id.str());
        auto result=object_request_(uri,args,40000,false);return osd_boolean(result.get("success"));
    }catch(const std::exception&){return false;}
}
bool SimulationConnector::release_agent(const framework::Uuid& region,const framework::Uuid& agent,const std::string& uri){return release_agent(region,agent,std::optional<std::string>(uri));}
bool SimulationConnector::release_agent(const framework::Uuid&,const framework::Uuid&,const std::optional<std::string>& uri){try{delete_request_(uri,10000);}catch(const std::exception&){}return true;}
bool SimulationConnector::close_agent(GridRegion& destination,const framework::Uuid& agent,const std::optional<std::string>& auth){return close_agent(&destination,agent,auth);}
bool SimulationConnector::close_agent(GridRegion* destination,const framework::Uuid& agent,const std::optional<std::string>& auth){
    if(!destination)throw std::invalid_argument("Null destination");
    const auto uri=destination->server_uri()+agent_path()+agent.str()+"/"+destination->id.str()+"/?auth="+auth.value_or("");
    try{delete_request_(uri,10000);}catch(const std::exception&){}return true;
}
void SimulationConnector::pack_data(Value& args,const GridRegion* source,framework::AgentCircuitData&,const GridRegion& destination,std::uint32_t flags){
    if(source){args.set("source_x",std::to_string(source->x));args.set("source_y",std::to_string(source->y));args.set("source_name",source->name.value_or(""));args.set("source_uuid",source->id.str());if(source->raw_server_uri&&!source->raw_server_uri->empty())args.set("source_server_uri",*source->raw_server_uri);}
    args.set("destination_x",std::to_string(destination.x));args.set("destination_y",std::to_string(destination.y));args.set("destination_name",destination.name.value_or(""));args.set("destination_uuid",destination.id.str());args.set("teleport_flags",std::to_string(flags));
}
bool SimulationConnector::query_access(GridRegion& d,const framework::Uuid& a,const std::optional<std::string>& h,bool t,const framework::Vector3& p,std::vector<framework::Uuid>& f,LoginTransferContext& c,std::string& r){return query_access(&d,a,h,t,p,&f,&c,r);}
bool SimulationConnector::query_access(GridRegion* destination,const framework::Uuid& agent,const std::optional<std::string>& home,bool via_teleport,const framework::Vector3& position,std::vector<framework::Uuid>* features,LoginTransferContext* context,std::string& reason){
    reason="Failed to contact destination";if(!destination)throw std::invalid_argument("Null destination");const auto uri=destination->server_uri()+agent_path()+agent.str()+"/"+destination->id.str()+"/";
    Value request=Value::object({});request.set("viaTeleport",Value(via_teleport));request.set("position",framework::vector3_string(position));request.set("my_version","SIMULATION/0.3");request.set("simulation_service_supported_min",real(0.3f));request.set("simulation_service_supported_max",real(0.8f));request.set("simulation_service_accepted_min",real(0.3f));request.set("simulation_service_accepted_max",real(0.8f));if(!context)throw std::invalid_argument("Null transfer context");request.set("context",context->pack());
    Value available;available.kind=Value::Kind::Array;if(!features)throw std::invalid_argument("Null features");for(const auto& f:*features)available.array.emplace_back(f.str());request.set("features",std::move(available));if(home)request.set("agent_home_uri",*home);
    try{
        const auto result=request_(uri,request,"QUERYACCESS",false,true);bool success=osd_boolean(result.get("success")),has_result=false;
        if(result.contains("_Result")){has_result=true;const auto& data=map(result.get("_Result"));success=osd_boolean(data.get("success"));reason=osd_string(data.get("reason"));
            if(data.contains("negotiated_inbound_version")){context->inbound_version=static_cast<float>(osd_real(data.get("negotiated_inbound_version")));context->outbound_version=static_cast<float>(osd_real(data.get("negotiated_outbound_version")));}
            else if(data.contains("version")){auto version=osd_string(data.get("version"));auto slash=version.find('/');if(slash!=std::string::npos){auto value=version.substr(slash+1);value=value.substr(0,value.find('/'));float n;if(!parse_avatar_float(value,n))throw std::invalid_argument("Invalid version");context->inbound_version=context->outbound_version=n;}}
        }
        if(!success||context->inbound_version==0||context->outbound_version==0){if(!has_result){if(result.contains("Message")){const auto message=osd_string(result.get("Message"));if(message=="Service request failed: [MethodNotAllowed] MethodNotAllowed")return true;reason=message;}else reason="Communications failure";}return false;}
        features->clear();const auto& advertised=result.get("features");if(advertised.kind==Value::Kind::Array)for(const auto& f:advertised.array)features->push_back(constructor_uuid(osd_string(f)));
        context->wearables_count=context->outbound_version<0.5f?15:context->outbound_version<0.6f?16:-1;return success;
    }catch(const std::exception&){return false;}
}
bool SimulationConnector::create_agent(const GridRegion* s,GridRegion& d,const std::shared_ptr<framework::AgentCircuitData>& c,std::uint32_t f,LoginTransferContext& x,std::string& r){return create_agent(s,&d,c,f,&x,r);}
bool SimulationConnector::create_agent(const GridRegion* source,GridRegion* destination,const std::shared_ptr<framework::AgentCircuitData>& circuit,std::uint32_t flags,LoginTransferContext* context,std::string& reason){
    reason.clear();if(!destination){reason="Destination not found";return false;}if(!circuit)throw std::invalid_argument("Null agent circuit");const auto uri=destination->server_uri()+agent_path()+circuit->agent_id.str()+"/";
    try{
        auto args=circuit->pack(context);LoginTransferContext defaults;if(!context)context=&defaults;args.set("context",context->pack());pack_data(args,source,*circuit,*destination,flags);
        auto result=request_(uri,args,"POST",true,false);if(osd_boolean(result.get("success"))&&result.get("_Result").kind==Value::Kind::Map){const auto& data=result.get("_Result");reason=osd_string(data.get("reason"));return osd_boolean(data.get("success"));}
        result=request_(uri,args,"POST",false,false);if(osd_boolean(result.get("success"))&&result.get("_Result").kind==Value::Kind::Map){const auto& data=result.get("_Result");reason=osd_string(data.get("reason"));return osd_boolean(data.get("success"));}
        reason=osd_string(result.get("Message"));return false;
    }catch(const std::exception& e){reason=e.what();return false;}
}
}
