// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/viewer_region.hpp"
#include "opensim/scene/terrain_layer.hpp"
#include <ctime>
namespace opensim::scene {
using namespace framework;using namespace network;
namespace {
std::vector<std::uint8_t> text(const std::string& value,std::size_t max=255){if(value.empty())return {};auto n=std::min(value.size(),max-1);std::vector<std::uint8_t> out(value.begin(),value.begin()+n);out.push_back(0);return out;}
std::vector<double> vector(Vector3 value){return {value.x,value.y,value.z};}
Vector3 vector(const lludp::FieldValue& value){auto& a=std::get<std::vector<double>>(value);return {static_cast<float>(a.at(0)),static_cast<float>(a.at(1)),static_cast<float>(a.at(2))};}
std::uint32_t number(const lludp::Block& block,const char* name){return static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at(name)));}
}
ViewerRegion::ViewerRegion(World& world,AgentCircuitManager& circuits,CircuitTransport::Sender sender,Admission admission,ViewerRegionOptions options,CircuitTransportOptions transport_options)
 :world_(world),circuits_(circuits),admission_(std::move(admission)),options_(std::move(options)),
 transport_(circuits,std::move(sender),[this](const auto& c,const auto& l,const auto& e){return admit(c,l,e);},
 [this](const auto& c,const auto& p){packet(c,p);},[this](const auto& c){closed(c);},transport_options){}
ViewerRegion::~ViewerRegion(){close();}
void ViewerRegion::callbacks(Event handshake,Event entered,Event logout,Event closed,PacketSink other){std::lock_guard lock(mutex_);handshake_=std::move(handshake);entered_=std::move(entered);logout_=std::move(logout);closed_=std::move(closed);other_=std::move(other);}
bool ViewerRegion::admit(const std::shared_ptr<AgentCircuitData>& circuit,const Login& login,const UdpEndpoint& endpoint){
 if(!admission_||!admission_(circuit,login,endpoint))return false;
 ViewerAgent value;value.circuit=circuit;value.endpoint=endpoint;value.local_id=world_.allocate_local_id();value.position=circuit->start_position;value.connected=true;
 std::lock_guard lock(mutex_);agents_[circuit->agent_id]=std::move(value);return true;
}
std::optional<ViewerAgent> ViewerRegion::agent(Uuid id)const{std::lock_guard lock(mutex_);auto i=agents_.find(id);return i==agents_.end()?std::nullopt:std::optional(i->second);}
std::optional<ViewerAgent> ViewerRegion::update_physics(Uuid id,Uuid session,Vector3 position,Vector3 velocity,Vector3 acceleration,bool flying){std::lock_guard lock(mutex_);auto i=agents_.find(id);if(i==agents_.end()||!i->second.root||i->second.circuit->session_id!=session)return {};auto& a=i->second;a.position=position;a.velocity=velocity;a.acceleration=acceleration;a.flying=flying;return a;}
std::vector<ViewerAgent> ViewerRegion::agents()const{std::lock_guard lock(mutex_);std::vector<ViewerAgent> out;for(auto& [id,a]:agents_)out.push_back(a);return out;}
void ViewerRegion::receive(const UdpEndpoint& endpoint,std::span<const std::uint8_t> bytes){transport_.receive(endpoint,bytes);poll();}
void ViewerRegion::send_terrain(Uuid id){
 std::vector<TerrainPatchCoordinate> patches;auto& terrain=world_.terrain();
 for(std::uint32_t y=0;y<terrain.patches_y();++y)for(std::uint32_t x=0;x<terrain.patches_x();++x)patches.push_back({x,y});
 for(auto& layer:terrain_layer_packets(terrain,patches)){auto p=lludp::make_packet("LayerData");p.block("LayerID")["Type"]=std::uint64_t(layer.type);p.block("LayerData")["Data"]=std::move(layer.data);transport_.enqueue(id,std::move(p),ThrottleOutPacketType::Land);}
}
void ViewerRegion::poll(){
 transport_.poll();auto clients=agents();for(auto& a:clients){if(a.handshake_sent||!transport_.connection(a.circuit->agent_id))continue;
  {std::lock_guard lock(mutex_);auto i=agents_.find(a.circuit->agent_id);if(i==agents_.end()||i->second.handshake_sent)continue;i->second.handshake_sent=true;}
  transport_.send(a.circuit->agent_id,region_handshake(a.circuit->agent_id));
 }
}
lludp::Packet ViewerRegion::region_handshake(Uuid agent)const{
 auto p=lludp::make_packet("RegionHandshake");auto& r=p.block("RegionInfo");r["RegionFlags"]=std::uint64_t(options_.region_flags);r["SimAccess"]=std::uint64_t(options_.access);r["SimName"]=text(options_.name);r["SimOwner"]=options_.owner;r["IsEstateManager"]=options_.estate_manager?options_.estate_manager(agent):(!agent.is_zero()&&agent==options_.owner);r["WaterHeight"]=double(options_.water_height);r["BillableFactor"]=double(options_.billable_factor);r["CacheID"]=options_.cache_id;
 const char* corners[]={"00","01","10","11"};for(int i=0;i<4;++i){r["TerrainDetail"+std::to_string(i)]=options_.terrain_details[i];r[std::string("TerrainStartHeight")+corners[i]]=double(options_.terrain_start[i]);r[std::string("TerrainHeightRange")+corners[i]]=double(options_.terrain_range[i]);}
 p.block("RegionInfo2")["RegionID"]=world_.region_id();auto& product=p.block("RegionInfo3");product["CPUClassID"]=std::int64_t(9);product["CPURatio"]=std::int64_t(1);product["ProductName"]=text(options_.product);
 auto info=lludp::make_block(p.spec->blocks.at(3));info["RegionFlagsExtended"]=std::uint64_t(options_.region_flags);info["RegionProtocols"]=std::uint64_t(1)<<63;p.blocks["RegionInfo4"].push_back(std::move(info));return p;
}
lludp::Packet ViewerRegion::movement_complete(const ViewerAgent& a)const{
 auto p=lludp::make_packet("AgentMovementComplete");p.block("AgentData")["AgentID"]=a.circuit->agent_id;p.block("AgentData")["SessionID"]=a.circuit->session_id;auto& d=p.block("Data");auto pos=a.position;if(pos==Vector3{})pos=a.circuit->start_position;d["Position"]=vector(pos);d["LookAt"]=vector(a.look_at);d["RegionHandle"]=(std::uint64_t(options_.x)<<32)|options_.y;d["Timestamp"]=std::uint64_t(options_.unix_time?options_.unix_time():static_cast<std::uint32_t>(std::time(nullptr)));p.block("SimData")["ChannelVersion"]=text(options_.channel_version,65535);return p;
}
void ViewerRegion::packet(const CircuitConnection& connection,const lludp::Packet& p){
 auto value=agent(connection.agent);if(!value)return;auto a=*value;Event event;PacketSink other;bool move=false,logout=false;
 {std::lock_guard lock(mutex_);auto i=agents_.find(connection.agent);if(i==agents_.end())return;auto& current=i->second;
  if(p.spec->name=="RegionHandshakeReply"){current.handshake_reply=true;current.viewer_flags=number(p.block("RegionInfo"),"Flags");event=handshake_;}
  else if(p.spec->name=="CompleteAgentMovement"){if(number(p.block("AgentData"),"CircuitCode")!=connection.circuit_code||current.root)return;if(options_.prepare_root)options_.prepare_root(current);current.root=true;event=entered_;move=true;}
  else if(p.spec->name=="AgentUpdate"){
   auto& b=p.block("AgentData");current.controls=number(b,"ControlFlags");current.look_at=vector(b.at("CameraAtAxis"));auto& q=std::get<std::vector<double>>(b.at("BodyRotation"));current.rotation={static_cast<float>(q.at(0)),static_cast<float>(q.at(1)),static_cast<float>(q.at(2)),static_cast<float>(q.at(3))};other=other_;
  }else if(p.spec->name=="SetAlwaysRun"){current.always_run=std::get<bool>(p.block("AgentData").at("AlwaysRun"));other=other_;}
  else if(p.spec->name=="LogoutRequest"){current.logout_requested=true;event=logout_;logout=true;}
  else other=other_;a=current;
 }
 if(p.spec->name=="AgentThrottle"&&number(p.block("AgentData"),"CircuitCode")==connection.circuit_code)transport_.set_throttles(connection.agent,std::get<std::vector<std::uint8_t>>(p.block("Throttle").at("Throttles")));
 if(move)transport_.send(connection.agent,movement_complete(a));
 if(logout){auto reply=lludp::make_packet("LogoutReply");reply.block("AgentData")["AgentID"]=connection.agent;reply.block("AgentData")["SessionID"]=connection.session;auto item=lludp::make_block(reply.spec->blocks.at(1));item["ItemID"]=Uuid{};reply.blocks["InventoryData"].push_back(std::move(item));transport_.send(connection.agent,std::move(reply));}
 if(event)event(a);if(other)other(a,p);
}
void ViewerRegion::closed(const CircuitConnection& connection){Event event;std::optional<ViewerAgent> value;{std::lock_guard lock(mutex_);auto i=agents_.find(connection.agent);if(i!=agents_.end()){value=i->second;agents_.erase(i);}event=closed_;}if(value&&event)event(*value);}
void ViewerRegion::disconnect(Uuid id){transport_.remove_client(id);circuits_.remove_circuit(id);}
void ViewerRegion::close(){transport_.close();}
}
