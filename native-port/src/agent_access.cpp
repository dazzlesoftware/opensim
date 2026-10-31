// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/agent_access.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/simulation_body.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/services/avatar_data.hpp"
#include <algorithm>
namespace opensim::server {
using namespace network;
namespace {
std::string number(float n){return services::avatar_float_string(n);}
Value real(float n){Value value;value.kind=Value::Kind::Real;value.real=n;return value;}
framework::Uuid uuid(std::string_view text){while(text.starts_with(' '))text.remove_prefix(1);if(text.size()<32)throw std::invalid_argument("Invalid UUID feature");return framework::Uuid(text.substr(0,text[8]=='-'?36:32));}
}
bool agent_path_parameters(std::string_view path,AgentPathParameters& result){result={};while(path.starts_with('/'))path.remove_prefix(1);while(path.ends_with('/'))path.remove_suffix(1);std::vector<std::string_view> parts;std::size_t start=0;while(true){auto end=path.find('/',start);parts.push_back(path.substr(start,end==std::string_view::npos?end:end-start));if(end==std::string_view::npos)break;start=end+1;}if(parts.size()<=1||!framework::try_parse_sdk_uuid(parts[1],result.agent))return false;if(parts.size()>=3)framework::try_parse_sdk_uuid(parts[2],result.region);if(parts.size()>=4)result.action=parts[3];return true;}
void AgentAccessHandler::process_admission(const HttpRequest& request,HttpResponse& response){
 response.keep_alive=false;response.content_type="application/json";if(!service_){response.status=500;response.body="false";return;}
 for(const auto& [name,value]:request.headers){auto lower=name;for(auto& c:lower)if(c>='A'&&c<='Z')c+=32;if(lower=="x-secondlife-shard"){response.status=406;response.body="false";break;}}
 AgentPathParameters parameters;if(!agent_path_parameters(request.path,parameters)){response.status=404;response.body="false";return;}
 if(request.method=="QUERYACCESS"||request.method=="POST"){if(parameters.agent.is_zero()||(request.method=="QUERYACCESS"&&parameters.region.is_zero())){response.status=400;response.body="false";return;}auto args=simulation_json_body(request);if(!args){response.status=400;response.body="false";return;}if(request.method=="QUERYACCESS")query_access(*args,response,parameters.agent,parameters.region);else post_agent(*args,response,parameters.agent,request.remote_ip);return;}
 if(request.method=="DELETE"&&lifecycle_){if(parameters.agent.is_zero()||parameters.region.is_zero()){response.status=400;response.body="false";return;}lifecycle_->delete_agent(response,parameters.agent,parameters.action,parameters.region,agent_delete_auth(request.raw_target),request.remote_ip);return;}
 if(request.method=="PUT"&&updates_){auto args=simulation_json_body(request);if(!args){response.status=400;response.body="false";return;}updates_->put_agent(*args,response);return;}
 response.status=405;response.body="false";
}
void AgentAccessHandler::post_agent(const Value& args,HttpResponse& response,const framework::Uuid&,const std::optional<std::string>& caller_address){
 if(args.view().kind!=Value::Kind::Map)throw std::invalid_argument("Agent post requires an OSD map");framework::EntityTransferContext context;if(args.contains("context")&&args.get("context").view().kind==Value::Kind::Map)context.unpack(args.get("context").view());services::GridRegion destination;destination.name.reset();
 if(args.contains("destination_x"))services::parse_account_integer(osd_string(args.get("destination_x")),destination.x);if(args.contains("destination_y"))services::parse_account_integer(osd_string(args.get("destination_y")),destination.y);if(args.contains("destination_uuid"))framework::try_parse_sdk_uuid(osd_string(args.get("destination_uuid")),destination.id);if(args.contains("destination_name"))destination.name=osd_to_string(args.get("destination_name"));std::uint32_t flags=args.contains("teleport_flags")?osd_unsigned(args.get("teleport_flags")):0;
 auto circuit=std::make_shared<framework::AgentCircuitData>();try{circuit->unpack(args.view());}catch(const std::exception&){response.status=400;response.body="false";return;}
 std::optional<services::GridRegion> source;if(args.contains("source_uuid")){source.emplace();source->id=uuid(osd_string(args.get("source_uuid")));source->x=services::account_integer(osd_string(args.get("source_x")));source->y=services::account_integer(osd_string(args.get("source_y")));source->name=osd_string(args.get("source_name"));source->raw_server_uri=args.contains("source_server_uri")?std::optional<std::string>(osd_string(args.get("source_server_uri"))):std::nullopt;}
 if(!service_)throw std::invalid_argument("Null simulation service");std::string reason;bool accepted=service_->create_agent(source?&*source:nullptr,destination,circuit,flags,context,reason);Value result=Value::object({});result.set("reason",reason);result.set("success",Value(accepted));if(!caller_address)throw std::invalid_argument("Null caller endpoint");result.set("your_ip",*caller_address);response.status=200;response.body=osd_json(result);response.keep_alive=(flags&(128u|1073741824u))!=128u;
}
void AgentAccessHandler::query_access(const Value& args, HttpResponse& response,const framework::Uuid& agent,const framework::Uuid& region){
 if(args.view().kind!=Value::Kind::Map)throw std::invalid_argument("Access request requires an OSD map");bool teleport=true;if(args.contains("viaTeleport"))teleport=osd_boolean(args.get("viaTeleport"));framework::Vector3 position{};if(args.contains("position"))position=framework::parse_sdk_vector3(osd_string(args.get("position")));std::optional<std::string> home;if(args.contains("agent_home_uri"))home=osd_string(args.get("agent_home_uri"));
 float legacy=0;if(args.contains("my_version")){auto text=osd_string(args.get("my_version"));auto slash=text.find('/');if(slash!=std::string::npos){auto next=text.find('/',slash+1);if(!services::parse_avatar_float(text.substr(slash+1,next==std::string::npos?next:next-slash-1),legacy))throw std::invalid_argument("Invalid legacy protocol version");}}
 framework::EntityTransferContext context;if(args.contains("context")&&args.get("context").view().kind==Value::Kind::Map)context.unpack(args.get("context").view());auto read=[&](const char* key){return args.contains(key)?static_cast<float>(osd_real(args.get(key))):0.f;};float provided_min=read("simulation_service_supported_min"),provided_max=read("simulation_service_supported_max"),required_min=read("simulation_service_accepted_min"),required_max=read("simulation_service_accepted_max");
 Value result=Value::object({});float version=0,inbound=0,outbound=0;response.status=200;auto fail=[&](std::string reason){result.set("success",Value(false));result.set("reason",std::move(reason));response.body=osd_json(result,true);};
 if(provided_min==0){if(legacy==0){fail("Your region is running a old version of opensim no longer supported. Consider updating it");return;}version=legacy;if(version<0.3f||version>0.8f){fail("Your region protocol version is "+number(legacy)+" and we accept only 0.3 - 0.8. No version overlap.");return;}}
 else{if(provided_min>0.8f||provided_max<0.3f){fail("Your region provide protocol versions "+number(provided_min)+" - "+number(provided_max)+" and we accept only 0.3 - 0.8. No version overlap.");return;}if(required_min>0.8f||required_max<0.3f){fail("You require region protocol versions "+number(required_min)+" - "+number(required_max)+" and we provide only 0.3 - 0.8. No version overlap.");return;}outbound=std::min(provided_max,0.8f);inbound=std::min(required_max,0.8f);}
 std::vector<framework::Uuid> features;if(args.contains("features")&&args.get("features").view().kind==Value::Kind::Array)for(const auto& value:args.get("features").view().array)features.push_back(uuid(osd_string(value)));services::GridRegion destination;destination.id=region;context.inbound_version=provided_min==0?version:inbound;context.outbound_version=provided_min==0?version:outbound;if(!service_)throw std::invalid_argument("Null simulation service");std::string reason;bool accepted=service_->query_access(destination,agent,home,teleport,position,features,context,reason);
 result.set("success",Value(accepted));result.set("reason",reason);result.set("version","SIMULATION/"+number(version));result.set("negotiated_inbound_version",real(inbound));result.set("negotiated_outbound_version",real(outbound));Value wanted;wanted.kind=Value::Kind::Array;for(const auto& feature:features)wanted.array.emplace_back(feature.str());result.set("features",std::move(wanted));response.keep_alive=accepted;response.body=osd_json(result,true);
}
}
