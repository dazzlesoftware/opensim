#include "opensim/services/simulation_connector.hpp"
#include "opensim/network/osd_request.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "osd_fixture_value.hpp"
#include <bit>
#include <iostream>
using namespace opensim;using namespace services;using namespace network;
framework::Uuid id(int n){return framework::Uuid("00000000-0000-0000-0000-00000000000"+std::to_string(n));}
std::string hex(std::string_view s){constexpr char d[]="0123456789abcdef";std::string r;for(unsigned char c:s){r+=d[c>>4];r+=d[c&15];}return r;}
int main(){std::string line;while(std::getline(std::cin,line)){std::istringstream in(line);std::string mode,first,second;int profile;in>>mode>>profile>>first>>second;if(mode=="C"){std::cout<<hex(osd_json(canonical_osd_response(fixture::unhex(first)),true))<<'\n';continue;}
    GridRegion destination;destination.id=id(1);destination.x=25600;destination.y=51200;destination.name="Destination 試験";destination.set_server_uri("http://127.0.0.1:9000/base/");GridRegion source;source.id=id(2);source.x=-256;source.y=1024;source.name="Source";source.raw_server_uri="http://source:9000/raw";
    auto circuit=std::make_shared<framework::AgentCircuitData>();circuit->agent_id=id(7);circuit->first_name="First";circuit->last_name="Last";circuit->caps_path="caps/";circuit->session_id=id(8);circuit->secure_session_id=id(9);circuit->circuit_code=4294967295u;circuit->start_position={1.25f,2,30};
    if(profile&8)circuit->appearance=std::make_shared<framework::AvatarAppearance>();
    framework::EntityTransferContext context;context.wearables_count=3;if(profile&4)context.inbound_version=context.outbound_version=0;
    std::vector<framework::Uuid> features{id(3),id(4)};Value trace;trace.kind=Value::Kind::Array;int calls=0;
    SimulationConnector connector([&](const auto& uri,const auto& data,const auto& method,bool compressed,bool keepalive){Value call=Value::object({});call.set("uri",uri);call.set("method",method);call.set("compressed",Value(compressed));call.set("keepalive",Value(keepalive));call.set("data",data);trace.array.push_back(call);auto reply=calls++==0?first:second;if(reply=="throw")throw std::runtime_error("fixture failure");return parse_osd_json(fixture::unhex(reply));});
    std::string reason="before",status;try{bool passed;if(mode=="Q")passed=connector.query_access(profile&32?nullptr:&destination,id(7),profile&1?std::optional<std::string>("http://home/"):std::nullopt,(profile&2)==0,{1.25f,2,30},profile&16?nullptr:&features,profile&8?nullptr:&context,reason);
    else passed=connector.create_agent(profile&1?&source:nullptr,profile&4?nullptr:&destination,profile&16?nullptr:circuit,0xffffffffu,profile&2?nullptr:&context,reason);status=passed?"1":"0";}catch(...){status="EX";}
    std::cout<<status<<'|'<<hex(reason)<<'|'<<std::hex<<std::bit_cast<std::uint32_t>(context.inbound_version)<<':'<<std::bit_cast<std::uint32_t>(context.outbound_version)<<':'<<std::dec<<context.wearables_count<<'|';for(const auto& f:features)std::cout<<f.str()<<',';std::cout<<'|'<<hex(osd_json(trace,true))<<'\n';
}}
