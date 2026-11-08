// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/world.hpp"
#include "opensim/network/circuit_transport.hpp"
#include <array>
namespace opensim::scene {
struct ViewerAgent;
struct ViewerRegionOptions {
    std::string name = "Native Region", product = "Mainland", channel_version = "OpenSim native";
    std::uint32_t x = 256000, y = 256000, region_flags = 0;
    std::uint8_t access = 13;
    framework::Uuid owner, cache_id;
    float water_height = 20, billable_factor = 1;
    std::function<std::pair<framework::Uuid, float>()> current_estate_owner_and_billable_factor;
    std::function<float()> current_water_height;
    std::array<framework::Uuid, 4> terrain_details{framework::Uuid("b8d3965a-ad78-bf43-699b-bff8eca6c975"),
                                                   framework::Uuid("abb783e6-3e93-26c0-248a-247666855da3"),
                                                   framework::Uuid("179cdabd-398a-9b6b-1391-4dc333ba321f"),
                                                   framework::Uuid("beb169c7-11ea-fff2-efe5-0f24dc881df2")};
    struct TerrainTextureHeights {
        std::array<float, 4> low{10, 10, 10, 10}, high{60, 60, 60, 60};
    };
    std::function<TerrainTextureHeights()> terrain_texture_heights;
    std::function<std::array<framework::Uuid, 4>(bool)> terrain_texture_assets;
    std::function<std::uint32_t()> unix_time;
    std::function<bool(framework::Uuid)> estate_manager;
    std::function<std::uint32_t()> current_region_flags;
    std::function<std::uint8_t()> current_access;
    std::function<void(ViewerAgent&)> prepare_root;
    std::function<void(const ViewerAgent&, const ViewerAgent&)> local_seat_exit;
    std::function<bool(framework::Uuid)> teleport_pending;
    std::function<std::string(const ViewerAgent&, framework::Uuid, network::lludp::Packet&)> resolve_landmark;
};
struct ActiveViewerGroup {
    framework::Uuid id;
    std::string name, title;
    std::uint64_t powers = 0;
};
struct ViewerAgent {
    std::shared_ptr<framework::AgentCircuitData> circuit;
    network::UdpEndpoint endpoint;
    std::uint32_t local_id = 0, viewer_flags = 0, controls = 0;
    std::uint8_t agent_state = 0;
    bool connected = false, handshake_sent = false, handshake_reply = false, root = false,
         logout_requested = false;
    bool mute_collisions = false;
    bool crossed_child = false;
    bool crossing_arrival_ready = false;
    framework::Vector3 position, look_at{1, 0, 0}, velocity, acceleration;
    std::array<float, 4> collision_plane{0, 0, 0, 1};
    framework::Vector3 angular_velocity;
    framework::Vector3 camera_position;
    framework::Vector3 camera_at_axis, camera_left_axis;
    framework::Vector3 camera_up_axis;
    framework::Quaternion camera_rotation;
    float camera_fov = 1.04f;
    float health = 100.f;
    float heal_rate = .5f;
    float last_heal_health_sent = 0.f;
    std::uint32_t camera_fov_generation = 0;
    std::uint32_t view_width = 640;
    std::uint32_t view_height = 480;
    bool flying = false, always_run = false;
    framework::Quaternion rotation{0, 0, 0, 1};
    std::optional<framework::Quaternion> mouselook_rotation;
    struct Seat {
        framework::Uuid part;
        std::uint32_t parent_local;
        framework::Vector3 offset;
        framework::Quaternion rotation;
        bool explicit_target = true;
        std::uint64_t order = 0;
    };
    std::optional<Seat> seat;
    bool ground_sitting = false;
    std::shared_ptr<framework::AvatarAppearance> appearance_override;
    std::optional<ActiveViewerGroup> active_group_override;
    framework::Uuid active_group_id() const {
        return active_group_override ? active_group_override->id : circuit->active_group_id;
    }
    std::shared_ptr<framework::AvatarAppearance> appearance() const {
        return appearance_override ? appearance_override : circuit->appearance;
    }
};
// Viewer packet lifecycle for an owned world. Admission remains an explicit
// scene-host responsibility; registered circuit credentials alone are not an
// authorization decision. Terrain/avatar/object/physics streams attach here.
class ViewerRegion {
  public:
    using Admission = network::CircuitTransport::Admission;
    using Event = std::function<void(const ViewerAgent&)>;
    using PacketSink = std::function<void(const ViewerAgent&, const network::lludp::Packet&)>;
    ViewerRegion(World& world, framework::AgentCircuitManager& circuits,
                 network::CircuitTransport::Sender sender, Admission admission,
                 ViewerRegionOptions options = {}, network::CircuitTransportOptions transport_options = {});
    ~ViewerRegion();
    void receive(const network::UdpEndpoint& endpoint, std::span<const std::uint8_t> bytes);
    void poll();
    std::optional<ViewerAgent> update_appearance(framework::Uuid id, framework::Uuid session,
                                                 std::shared_ptr<framework::AvatarAppearance> appearance);
    void send_terrain(framework::Uuid agent);
    bool teleport_local(framework::Uuid agent, framework::Uuid session, const network::lludp::Packet&);
    void broadcast_terrain_changes();
    std::optional<ViewerAgent> agent(framework::Uuid id) const;
    bool update_health(framework::Uuid id, framework::Uuid session, float health);
    std::optional<float> heal(framework::Uuid id, framework::Uuid session, float frame_seconds);
    void set_heal_rate(framework::Uuid id, framework::Uuid session, float rate);
    std::optional<ViewerAgent> demote_after_crossing(framework::Uuid id, framework::Uuid session);
    bool stage_crossing_arrival(const std::shared_ptr<framework::AgentCircuitData>& circuit);
    void cancel_crossing_arrival(framework::Uuid id, framework::Uuid session);
    std::optional<ViewerAgent> update_seat(framework::Uuid id, framework::Uuid session,
                                           std::optional<ViewerAgent::Seat> seat, framework::Vector3 position,
                                           std::optional<framework::Quaternion> rotation = std::nullopt,
                                           std::optional<std::uint64_t> expected_seat = std::nullopt);
    std::optional<ViewerAgent> update_ground_sit(framework::Uuid id, framework::Uuid session, bool sitting);
    std::vector<ViewerAgent> refresh_seated_positions();
    std::optional<ViewerAgent> update_active_group(framework::Uuid id, framework::Uuid session,
                                                   ActiveViewerGroup group);
    std::optional<ViewerAgent> update_physics(framework::Uuid id, framework::Uuid session,
                                              framework::Vector3 position, framework::Vector3 velocity,
                                              framework::Vector3 acceleration, bool flying,
                                              std::optional<framework::Quaternion> rotation = {},
                                              std::array<float, 4> collision_plane = {0, 0, 0, 1},
                                              framework::Vector3 angular_velocity = {});
    std::vector<ViewerAgent> agents() const;
    std::vector<std::string> root_agent_names() const;
    network::CircuitTransport& transport() {
        return transport_;
    }
    void callbacks(Event handshake, Event entered, Event logout, Event closed, PacketSink other = {});
    void disconnect(framework::Uuid id);
    void close();
    network::lludp::Packet region_handshake(framework::Uuid agent = {}, bool terrain_pbr = false) const;
    network::lludp::Packet movement_complete(const ViewerAgent& agent) const;

  private:
    bool admit(const std::shared_ptr<framework::AgentCircuitData>& circuit, const framework::Login& login,
               const network::UdpEndpoint& endpoint);
    void packet(const network::CircuitConnection& connection, const network::lludp::Packet& packet);
    void closed(const network::CircuitConnection& connection);
    void restore_transferred_seat(ViewerAgent& avatar);
    std::uint32_t region_flags() const;
    World& world_;
    framework::AgentCircuitManager& circuits_;
    Admission admission_;
    ViewerRegionOptions options_;
    mutable std::mutex mutex_;
    std::map<framework::Uuid, ViewerAgent> agents_;
    // SceneGraph enumerates its dictionary in slot order, reusing the most
    // recently vacated slot for the next presence.
    std::vector<std::optional<framework::Uuid>> presence_slots_;
    std::vector<std::size_t> free_presence_slots_;
    std::uint64_t seat_order_ = 0;
    Event handshake_, entered_, logout_, closed_;
    PacketSink other_;
    network::CircuitTransport transport_;
};
}
