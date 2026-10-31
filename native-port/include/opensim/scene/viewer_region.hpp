// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/world.hpp"
#include "opensim/network/circuit_transport.hpp"
#include <array>
namespace opensim::scene {
struct ViewerAgent;
struct ViewerRegionOptions {
 std::string name="Native Region",product="Mainland",channel_version="OpenSim native";
 std::uint32_t x=256000,y=256000,region_flags=0;
 std::uint8_t access=13;
 framework::Uuid owner,cache_id;
 float water_height=20,billable_factor=1;
 std::array<framework::Uuid,4> terrain_details{
  framework::Uuid("b8d3965a-ad78-bf43-699b-bff8eca6c975"),framework::Uuid("abb783e6-3e93-26c0-248a-247666855da3"),
  framework::Uuid("179cdabd-398a-9b6b-1391-4dc333ba321f"),framework::Uuid("beb169c7-11ea-fff2-efe5-0f24dc881df2")};
 std::array<float,4> terrain_start{10,10,10,10},terrain_range{60,60,60,60};
 std::function<std::uint32_t()> unix_time;
 std::function<bool(framework::Uuid)> estate_manager;
 std::function<void(ViewerAgent&)> prepare_root;
};
struct ViewerAgent {
 std::shared_ptr<framework::AgentCircuitData> circuit;
 network::UdpEndpoint endpoint;
 std::uint32_t local_id=0,viewer_flags=0,controls=0;
 bool connected=false,handshake_sent=false,handshake_reply=false,root=false,logout_requested=false;
 framework::Vector3 position,look_at{1,0,0},velocity,acceleration;
 bool flying=false,always_run=false;
 framework::Quaternion rotation{0,0,0,1};
};
// Viewer packet lifecycle for an owned world. Admission remains an explicit
// scene-host responsibility; registered circuit credentials alone are not an
// authorization decision. Terrain/avatar/object/physics streams attach here.
class ViewerRegion {
public:
 using Admission=network::CircuitTransport::Admission;
 using Event=std::function<void(const ViewerAgent&)>;
 using PacketSink=std::function<void(const ViewerAgent&,const network::lludp::Packet&)>;
 ViewerRegion(World& world,framework::AgentCircuitManager& circuits,
              network::CircuitTransport::Sender sender,Admission admission,
              ViewerRegionOptions options={},network::CircuitTransportOptions transport_options={});
 ~ViewerRegion();
 void receive(const network::UdpEndpoint& endpoint,std::span<const std::uint8_t> bytes);
 void poll();
 void send_terrain(framework::Uuid agent);
 std::optional<ViewerAgent> agent(framework::Uuid id)const;
 std::optional<ViewerAgent> update_physics(framework::Uuid id,framework::Uuid session,framework::Vector3 position,
  framework::Vector3 velocity,framework::Vector3 acceleration,bool flying);
 std::vector<ViewerAgent> agents()const;
 network::CircuitTransport& transport(){return transport_;}
 void callbacks(Event handshake,Event entered,Event logout,Event closed,PacketSink other={});
 void disconnect(framework::Uuid id);
 void close();
 network::lludp::Packet region_handshake(framework::Uuid agent={})const;
 network::lludp::Packet movement_complete(const ViewerAgent& agent)const;
private:
 bool admit(const std::shared_ptr<framework::AgentCircuitData>& circuit,const framework::Login& login,const network::UdpEndpoint& endpoint);
 void packet(const network::CircuitConnection& connection,const network::lludp::Packet& packet);
 void closed(const network::CircuitConnection& connection);
 World& world_;framework::AgentCircuitManager& circuits_;Admission admission_;ViewerRegionOptions options_;
 mutable std::mutex mutex_;std::map<framework::Uuid,ViewerAgent> agents_;
 Event handshake_,entered_,logout_,closed_;PacketSink other_;
 network::CircuitTransport transport_;
};
}
