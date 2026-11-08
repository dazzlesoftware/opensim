// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/viewer_region.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/scene/terrain_layer.hpp"
#include <algorithm>
#include <ctime>
#include <cmath>
namespace opensim::scene {
using namespace framework;
using namespace network;
namespace {
std::vector<std::uint8_t> text(const std::string& value, std::size_t max = 255) {
    if (value.empty())
        return {};
    auto n = std::min(value.size(), max - 1);
    std::vector<std::uint8_t> out(value.begin(), value.begin() + n);
    out.push_back(0);
    return out;
}
std::vector<double> vector(Vector3 value) {
    return {value.x, value.y, value.z};
}
Vector3 vector(const lludp::FieldValue& value) {
    auto& a = std::get<std::vector<double>>(value);
    return {static_cast<float>(a.at(0)), static_cast<float>(a.at(1)), static_cast<float>(a.at(2))};
}
std::uint32_t number(const lludp::Block& block, const char* name) {
    return static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at(name)));
}
Vector3 camera_axis_normalized(Vector3 axis) {
    const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (length > 0.f)
        return {axis.x / length, axis.y / length, axis.z / length};
    return {};
}
Quaternion rotation_from_axes(Vector3 forward, Vector3 left, Vector3 up) {
    const float trace = forward.x + left.y + up.z + 1.f;
    if (trace >= 1.f) {
        const float scale = .5f / std::sqrt(trace);
        return {(left.z - up.y) * scale, (up.x - forward.z) * scale, (forward.y - left.x) * scale,
                .25f / scale};
    }
    const float largest = std::max(left.y, up.z);
    if (largest < forward.x) {
        const float length = std::sqrt(forward.x - (left.y + up.z) + 1.f);
        const float scale = .5f / length;
        return {.5f * length, (forward.y + left.x) * scale, (up.x + forward.z) * scale,
                (left.z - up.y) * scale};
    }
    if (largest == left.y) {
        const float length = std::sqrt(left.y - (up.z + forward.x) + 1.f);
        const float scale = .5f / length;
        return {(forward.y + left.x) * scale, .5f * length, (left.z + up.y) * scale,
                (up.x - forward.z) * scale};
    }
    const float length = std::sqrt(up.z - (forward.x + left.y) + 1.f);
    const float scale = .5f / length;
    return {(up.x + forward.z) * scale, (left.z + up.y) * scale, .5f * length, (forward.y - left.x) * scale};
}
}
ViewerRegion::ViewerRegion(World& world, AgentCircuitManager& circuits, CircuitTransport::Sender sender,
                           Admission admission, ViewerRegionOptions options,
                           CircuitTransportOptions transport_options)
    : world_(world), circuits_(circuits), admission_(std::move(admission)), options_(std::move(options)),
      transport_(
          circuits, std::move(sender),
          [this](const auto& c, const auto& l, const auto& e) { return admit(c, l, e); },
          [this](const auto& c, const auto& p) { packet(c, p); }, [this](const auto& c) { closed(c); },
          transport_options) {}
ViewerRegion::~ViewerRegion() {
    close();
}
void ViewerRegion::callbacks(Event handshake, Event entered, Event logout, Event closed, PacketSink other) {
    std::lock_guard lock(mutex_);
    handshake_ = std::move(handshake);
    entered_ = std::move(entered);
    logout_ = std::move(logout);
    closed_ = std::move(closed);
    other_ = std::move(other);
}
namespace {
constexpr std::uint32_t block_flight = 0x80000u;
constexpr std::uint32_t fly_control = 0x2000u;

void apply_arrival_state(ViewerAgent& value, std::uint32_t region_flags) {
    const auto& circuit = value.circuit;
    value.position = circuit->start_position;
    value.camera_position = value.position;
    if (const auto& camera = circuit->transfer_camera) {
        value.camera_position = camera->center;
        value.camera_at_axis = camera->at_axis;
        value.camera_left_axis = camera->left_axis;
        value.camera_up_axis = camera->up_axis;
        value.camera_rotation = rotation_from_axes(camera->at_axis, camera->left_axis, camera->up_axis);
    }
    if (circuit->transfer_look_at)
        value.look_at = *circuit->transfer_look_at;
    if (const auto& movement = circuit->transfer_movement) {
        const auto& velocity = movement->velocity;
        if (std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z))
            value.velocity = velocity;
        const auto& rotation = movement->rotation;
        const float squared_length = rotation.x * rotation.x + rotation.y * rotation.y +
                                     rotation.z * rotation.z + rotation.w * rotation.w;
        if (squared_length > 0.f && std::isfinite(squared_length))
            value.rotation = rotation;
        value.controls = movement->controls;
        value.always_run = movement->always_run;
    }
    value.flying = (region_flags & block_flight) == 0 &&
                   ((circuit->teleport_flags & 8192u) != 0 || (value.controls & fly_control) != 0);
    if (value.flying)
        value.controls |= fly_control;
}
}
bool ViewerRegion::admit(const std::shared_ptr<AgentCircuitData>& circuit, const Login& login,
                         const UdpEndpoint& endpoint) {
    if (!admission_ || !admission_(circuit, login, endpoint))
        return false;
    ViewerAgent value;
    value.circuit = circuit;
    value.endpoint = endpoint;
    value.local_id = world_.allocate_local_id();
    value.connected = true;
    apply_arrival_state(value, region_flags());
    std::lock_guard lock(mutex_);
    const auto id = circuit->agent_id;
    const auto [entry, inserted] = agents_.try_emplace(id, std::move(value));
    if (!inserted) {
        entry->second = std::move(value);
    } else if (free_presence_slots_.empty()) {
        try {
            presence_slots_.push_back(id);
        } catch (...) {
            agents_.erase(entry);
            throw;
        }
    } else {
        presence_slots_[free_presence_slots_.back()] = id;
        free_presence_slots_.pop_back();
    }
    return true;
}
std::optional<ViewerAgent> ViewerRegion::agent(Uuid id) const {
    std::lock_guard lock(mutex_);
    auto i = agents_.find(id);
    return i == agents_.end() ? std::nullopt : std::optional(i->second);
}
std::optional<ViewerAgent> ViewerRegion::demote_after_crossing(Uuid id, Uuid session) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end() || entry->second.circuit->session_id != session ||
        !entry->second.root || entry->second.logout_requested)
        return std::nullopt;
    auto& avatar = entry->second;
    avatar.root = false;
    avatar.crossed_child = true;
    avatar.crossing_arrival_ready = false;
    avatar.seat.reset();
    avatar.ground_sitting = false;
    avatar.controls = 0;
    avatar.velocity = {};
    avatar.acceleration = {};
    return avatar;
}
bool ViewerRegion::stage_crossing_arrival(const std::shared_ptr<AgentCircuitData>& circuit) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(circuit->agent_id);
    if (entry == agents_.end() || !entry->second.crossed_child || entry->second.logout_requested ||
        entry->second.circuit->session_id != circuit->session_id)
        return false;
    entry->second.circuit = circuit;
    entry->second.crossing_arrival_ready = true;
    return true;
}
void ViewerRegion::cancel_crossing_arrival(Uuid id, Uuid session) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry != agents_.end() && entry->second.crossed_child && entry->second.circuit->session_id == session)
        entry->second.crossing_arrival_ready = false;
}
void ViewerRegion::restore_transferred_seat(ViewerAgent& avatar) {
    const auto& transfer = avatar.circuit->transfer_seat;
    if (!transfer)
        return;
    const auto part = world_.object(transfer->part);
    if (!part || part->is_attachment)
        return;
    const auto root = part->parent_id ? world_.object(part->parent_id) : part;
    if (!root)
        return;
    auto offset = transfer->offset;
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z))
        return;
    if (transfer->explicit_target && std::ranges::any_of(agents_, [&](const auto& entry) {
            const auto& seat = entry.second.seat;
            return seat && seat->explicit_target && seat->part == part->id;
        }))
        return;

    auto rotation = avatar.rotation;
    if (part->parent_id) {
        offset = framework::sdk_rotate_vector(offset, part->rotation);
        offset = {offset.x + part->position.x, offset.y + part->position.y, offset.z + part->position.z};
        rotation = framework::sdk_multiply_quaternion(part->rotation, rotation);
    }
    avatar.seat = ViewerAgent::Seat{part->id, root->local_id, offset, rotation,
                                   transfer->explicit_target, ++seat_order_};
    const auto world_offset = framework::sdk_rotate_vector(offset, root->rotation);
    avatar.position = {root->position.x + world_offset.x, root->position.y + world_offset.y,
                       root->position.z + world_offset.z};
    avatar.velocity = {};
    avatar.acceleration = {};
    avatar.flying = false;
}
std::optional<ViewerAgent> ViewerRegion::update_physics(Uuid id, Uuid session, Vector3 position,
                                                        Vector3 velocity, Vector3 acceleration, bool flying,
                                                        std::optional<framework::Quaternion> rotation,
                                                        std::array<float, 4> collision_plane,
                                                        Vector3 angular_velocity) {
    std::lock_guard lock(mutex_);
    auto i = agents_.find(id);
    if (i == agents_.end() || !i->second.root || i->second.circuit->session_id != session ||
        i->second.seat || i->second.ground_sitting)
        return {};
    auto& a = i->second;
    a.position = position;
    a.velocity = velocity;
    a.acceleration = acceleration;
    a.flying = flying;
    a.collision_plane = collision_plane;
    a.angular_velocity = angular_velocity;
    if (rotation)
        a.rotation = *rotation;
    return a;
}
std::optional<ViewerAgent> ViewerRegion::update_seat(Uuid id, Uuid session,
                                                     std::optional<ViewerAgent::Seat> seat, Vector3 position,
                                                     std::optional<framework::Quaternion> rotation,
                                                     std::optional<std::uint64_t> expected_seat) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end() || !entry->second.root || entry->second.circuit->session_id != session)
        return std::nullopt;
    if (expected_seat && (!entry->second.seat || entry->second.seat->order != *expected_seat))
        return std::nullopt;
    if (seat && seat->explicit_target)
        for (const auto& [other_id, other] : agents_)
            if (other_id != id && other.seat && other.seat->explicit_target && other.seat->part == seat->part)
                return std::nullopt;
    auto& avatar = entry->second;
    if (seat)
        seat->order = ++seat_order_;
    avatar.seat = std::move(seat);
    avatar.ground_sitting = false;
    avatar.position = position;
    if (rotation)
        avatar.rotation = *rotation;
    avatar.velocity = {};
    avatar.acceleration = {};
    avatar.flying = false;
    return avatar;
}
std::optional<ViewerAgent> ViewerRegion::update_ground_sit(Uuid id, Uuid session, bool sitting) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end() || !entry->second.root || entry->second.circuit->session_id != session ||
        entry->second.seat || entry->second.ground_sitting == sitting)
        return std::nullopt;
    auto& avatar = entry->second;
    avatar.ground_sitting = sitting;
    avatar.velocity = {};
    avatar.acceleration = {};
    avatar.flying = false;
    return avatar;
}
std::vector<ViewerAgent> ViewerRegion::refresh_seated_positions() {
    std::vector<ViewerAgent> seated;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [id, avatar] : agents_)
            if (avatar.seat)
                seated.push_back(avatar);
    }
    std::vector<ViewerAgent> reparented;
    for (const auto& avatar : seated) {
        const auto part = world_.object(avatar.seat->part);
        if (!part || part->is_attachment)
            continue;
        const auto root = part->parent_id ? world_.object(part->parent_id) : part;
        if (!root)
            continue;
        const auto offset = framework::sdk_rotate_vector(avatar.seat->offset, root->rotation);
        std::lock_guard lock(mutex_);
        const auto current = agents_.find(avatar.circuit->agent_id);
        if (current == agents_.end() || current->second.circuit->session_id != avatar.circuit->session_id ||
            !current->second.seat || current->second.seat->order != avatar.seat->order)
            continue;
        auto& updated = current->second;
        updated.position = {root->position.x + offset.x, root->position.y + offset.y,
                            root->position.z + offset.z};
        if (updated.seat->parent_local != root->local_id) {
            updated.seat->parent_local = root->local_id;
            reparented.push_back(updated);
        }
    }
    return reparented;
}
std::vector<ViewerAgent> ViewerRegion::agents() const {
    std::lock_guard lock(mutex_);
    std::vector<ViewerAgent> out;
    for (auto& [id, a] : agents_)
        out.push_back(a);
    return out;
}
std::vector<std::string> ViewerRegion::root_agent_names() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> names;
    for (const auto& id : presence_slots_) {
        if (!id)
            continue;
        const auto& agent = agents_.at(*id);
        if (agent.root)
            names.push_back(agent.circuit->name());
    }
    return names;
}
void ViewerRegion::receive(const UdpEndpoint& endpoint, std::span<const std::uint8_t> bytes) {
    transport_.receive(endpoint, bytes);
    poll();
}
void ViewerRegion::send_terrain(Uuid id) {
    std::vector<TerrainPatchCoordinate> patches;
    auto& terrain = world_.terrain();
    for (std::uint32_t y = 0; y < terrain.patches_y(); ++y)
        for (std::uint32_t x = 0; x < terrain.patches_x(); ++x)
            patches.push_back({x, y});
    for (auto& layer : terrain_layer_packets(terrain, patches)) {
        auto p = lludp::make_packet("LayerData");
        p.block("LayerID")["Type"] = std::uint64_t(layer.type);
        p.block("LayerData")["Data"] = std::move(layer.data);
        transport_.enqueue(id, std::move(p), ThrottleOutPacketType::Land);
    }
}
void ViewerRegion::broadcast_terrain_changes() {
    auto& terrain = world_.terrain();
    if (!terrain.tainted())
        return;
    const auto width = terrain.patches_x();
    const auto count = width * terrain.patches_y();
    std::vector<TerrainPatchCoordinate> patches;
    for (int index = terrain.next_taint(); index >= 0 && std::uint32_t(index) < count;
         index = terrain.next_taint(index + 1)) {
        patches.push_back({std::uint32_t(index) % width, std::uint32_t(index) / width});
    }
    if (patches.empty())
        return;
    const auto recipients = agents();
    for (auto& layer : terrain_layer_packets(terrain, patches)) {
        auto packet = lludp::make_packet("LayerData");
        packet.block("LayerID")["Type"] = std::uint64_t(layer.type);
        packet.block("LayerData")["Data"] = std::move(layer.data);
        for (const auto& viewer : recipients)
            if (viewer.root)
                transport_.enqueue_if_session(viewer.circuit->agent_id, viewer.circuit->session_id, packet,
                                              ThrottleOutPacketType::Land);
    }
}
void ViewerRegion::poll() {
    transport_.poll();
    auto clients = agents();
    for (auto& a : clients) {
        if (a.handshake_sent || !transport_.connection(a.circuit->agent_id))
            continue;
        {
            std::lock_guard lock(mutex_);
            auto i = agents_.find(a.circuit->agent_id);
            if (i == agents_.end() || i->second.handshake_sent)
                continue;
            i->second.handshake_sent = true;
        }
        transport_.send(a.circuit->agent_id, region_handshake(a.circuit->agent_id));
    }
}
std::optional<ViewerAgent> ViewerRegion::update_appearance(Uuid id, Uuid session,
                                                           std::shared_ptr<AvatarAppearance> appearance) {
    std::lock_guard lock(mutex_);
    const auto found = agents_.find(id);
    if (found == agents_.end() || !found->second.root || found->second.logout_requested ||
        found->second.circuit->session_id != session)
        return {};
    found->second.appearance_override = std::move(appearance);
    return found->second;
}
void ViewerRegion::set_heal_rate(Uuid id, Uuid session, float rate) {
    std::lock_guard lock(mutex_);
    const auto found = agents_.find(id);
    if (found != agents_.end() && found->second.circuit->session_id == session)
        found->second.heal_rate = std::clamp(rate, 0.f, 100.f);
}
std::optional<float> ViewerRegion::heal(Uuid id, Uuid session, float frame_seconds) {
    std::lock_guard lock(mutex_);
    const auto found = agents_.find(id);
    if (found == agents_.end() || !found->second.root || found->second.circuit->session_id != session)
        return {};
    auto& avatar = found->second;
    const float amount = avatar.heal_rate * frame_seconds;
    if (avatar.health <= 0.f || avatar.health == 100.f || amount <= 0.f)
        return {};
    avatar.health += amount;
    const bool reached_full_health = avatar.health > 100.f;
    if (reached_full_health)
        avatar.health = 100.f;
    if (!reached_full_health && !(std::abs(avatar.health - avatar.last_heal_health_sent) > 1.f))
        return {};
    avatar.last_heal_health_sent = avatar.health;
    return avatar.health;
}
bool ViewerRegion::update_health(Uuid id, Uuid session, float health) {
    std::lock_guard lock(mutex_);
    const auto found = agents_.find(id);
    if (found == agents_.end() || !found->second.root || found->second.circuit->session_id != session)
        return false;
    found->second.health = health;
    return true;
}
std::optional<ViewerAgent> ViewerRegion::update_active_group(Uuid id, Uuid session, ActiveViewerGroup group) {
    std::lock_guard lock(mutex_);
    const auto found = agents_.find(id);
    if (found == agents_.end() || found->second.circuit->session_id != session)
        return {};
    found->second.active_group_override = std::move(group);
    return found->second;
}
std::uint32_t ViewerRegion::region_flags() const {
    return options_.current_region_flags ? options_.current_region_flags() : options_.region_flags;
}
lludp::Packet ViewerRegion::region_handshake(Uuid agent, bool terrain_pbr) const {
    const auto flags = region_flags();
    auto p = lludp::make_packet("RegionHandshake");
    auto& r = p.block("RegionInfo");
    r["RegionFlags"] = std::uint64_t(flags);
    r["SimAccess"] = std::uint64_t(options_.current_access ? options_.current_access() : options_.access);
    r["SimName"] = text(options_.name);
    const auto [owner, billable_factor] = options_.current_estate_owner_and_billable_factor
        ? options_.current_estate_owner_and_billable_factor()
        : std::pair{options_.owner, options_.billable_factor};
    r["SimOwner"] = owner;
    r["IsEstateManager"] = options_.estate_manager ? options_.estate_manager(agent)
                                                   : (!agent.is_zero() && agent == owner);
    r["WaterHeight"] = double(options_.current_water_height ? options_.current_water_height() :
                                                           options_.water_height);
    r["BillableFactor"] = double(billable_factor);
    r["CacheID"] = options_.cache_id;
    const char* corners[] = {"00", "01", "10", "11"};
    const auto heights = options_.terrain_texture_heights ? options_.terrain_texture_heights()
                                                         : ViewerRegionOptions::TerrainTextureHeights{};
    const auto textures = options_.terrain_texture_assets ? options_.terrain_texture_assets(terrain_pbr)
                                                          : options_.terrain_details;
    for (int i = 0; i < 4; ++i) {
        r["TerrainDetail" + std::to_string(i)] = textures[i];
        r[std::string("TerrainStartHeight") + corners[i]] = double(heights.low[i]);
        r[std::string("TerrainHeightRange") + corners[i]] = double(heights.high[i]);
    }
    p.block("RegionInfo2")["RegionID"] = world_.region_id();
    auto& product = p.block("RegionInfo3");
    product["CPUClassID"] = std::int64_t(9);
    product["CPURatio"] = std::int64_t(1);
    product["ProductName"] = text(options_.product);
    auto info = lludp::make_block(p.spec->blocks.at(3));
    info["RegionFlagsExtended"] = std::uint64_t(flags);
    info["RegionProtocols"] = std::uint64_t(1) << 63;
    p.blocks["RegionInfo4"].push_back(std::move(info));
    return p;
}
lludp::Packet ViewerRegion::movement_complete(const ViewerAgent& a) const {
    auto p = lludp::make_packet("AgentMovementComplete");
    p.block("AgentData")["AgentID"] = a.circuit->agent_id;
    p.block("AgentData")["SessionID"] = a.circuit->session_id;
    auto& d = p.block("Data");
    auto pos = a.position;
    if (pos == Vector3{})
        pos = a.circuit->start_position;
    d["Position"] = vector(pos);
    d["LookAt"] = vector(a.look_at);
    d["RegionHandle"] = (std::uint64_t(options_.x) << 32) | options_.y;
    d["Timestamp"] = std::uint64_t(options_.unix_time ? options_.unix_time()
                                                      : static_cast<std::uint32_t>(std::time(nullptr)));
    p.block("SimData")["ChannelVersion"] = text(options_.channel_version, 65535);
    return p;
}
bool ViewerRegion::teleport_local(Uuid agent_id, Uuid session, const lludp::Packet& request) {
    const auto connection = transport_.connection(agent_id);
    const auto viewer = agent(agent_id);
    if (!connection || connection->session != session || !viewer || !viewer->root ||
        viewer->circuit->session_id != session)
        return false;
    packet(*connection, request);
    return true;
}

void ViewerRegion::packet(const CircuitConnection& connection, const lludp::Packet& p) {
    const auto teleport_failure = [&](const std::string& reason) {
        auto failure = lludp::make_packet("TeleportFailed");
        failure.block("Info")["AgentID"] = connection.agent;
        failure.block("Info")["Reason"] = text(reason);
        transport_.enqueue_if_session(connection.agent, connection.session, std::move(failure),
                                      ThrottleOutPacketType::Unknown);
    };
    if (p.spec->name == "TeleportLocationRequest") {
        if (options_.teleport_pending && options_.teleport_pending(connection.session)) {
            teleport_failure("Previous teleport process incomplete.  Please retry shortly.");
            return;
        }
        const auto requested = std::get<std::uint64_t>(p.block("Info").at("RegionHandle"));
        const auto local = (std::uint64_t(options_.x) << 32) | options_.y;
        const auto x = static_cast<std::uint32_t>(requested >> 32), y = static_cast<std::uint32_t>(requested);
        if (requested != local && x >= options_.x && y >= options_.y &&
            x - options_.x < world_.terrain().width() && y - options_.y < world_.terrain().height()) {
            auto normalized = p;
            auto& info = normalized.block("Info");
            info["RegionHandle"] = local;
            auto& position = std::get<std::vector<double>>(info.at("Position"));
            position.at(0) += x - options_.x;
            position.at(1) += y - options_.y;
            packet(connection, normalized);
            return;
        }
    }
    if (p.spec->name == "TeleportLandmarkRequest") {
        const auto viewer = agent(connection.agent);
        const auto& info = p.block("Info");
        if (!viewer || !viewer->root || std::get<Uuid>(info.at("AgentID")) != connection.agent ||
            std::get<Uuid>(info.at("SessionID")) != connection.session)
            return;
        const auto landmark = std::get<Uuid>(info.at("LandmarkID"));
        if (options_.teleport_pending && options_.teleport_pending(connection.session)) {
            if (landmark.is_zero())
                teleport_failure("Already processing a teleport");
            return;
        }
        auto destination = lludp::make_packet("TeleportLocationRequest");
        destination.block("Info")["RequestedFlags"] = std::uint64_t(landmark.is_zero() ? 34 : 10);
        const auto reason = options_.resolve_landmark
                                ? options_.resolve_landmark(*viewer, landmark, destination)
                                : (landmark.is_zero() ? "Your home region not found"
                                                      : "Could not find the landmark asset data");
        if (reason.empty())
            packet(connection, destination);
        else
            teleport_failure(reason);
        return;
    }
    auto value = agent(connection.agent);
    if (!value)
        return;
    auto a = *value;
    Event event;
    PacketSink other;
    std::optional<ViewerAgent> before_seat_exit;
    bool move = false, logout = false, local_teleport = false, invalid_teleport = false;
    {
        std::lock_guard lock(mutex_);
        auto i = agents_.find(connection.agent);
        if (i == agents_.end() || i->second.circuit->session_id != connection.session)
            return;
        auto& current = i->second;
        if (p.spec->name == "RegionHandshakeReply") {
            current.handshake_reply = true;
            current.viewer_flags = number(p.block("RegionInfo"), "Flags");
            event = handshake_;
        } else if (p.spec->name == "CompleteAgentMovement") {
            if (number(p.block("AgentData"), "CircuitCode") != connection.circuit_code ||
                current.root || (current.crossed_child && !current.crossing_arrival_ready))
                return;
            if (current.crossing_arrival_ready) {
                current.appearance_override.reset();
                current.active_group_override.reset();
                current.mouselook_rotation.reset();
                apply_arrival_state(current, region_flags());
            }
            restore_transferred_seat(current);
            if (options_.prepare_root)
                options_.prepare_root(current);
            // Teleports reset the camera center after landing. Crossings retain the transferred center.
            if (current.circuit->teleport_flags != 0) {
                current.camera_position = current.position;
                current.velocity = {};
            }
            current.root = true;
            current.crossed_child = false;
            current.crossing_arrival_ready = false;
            event = entered_;
            move = true;
        } else if (p.spec->name == "TeleportLocationRequest") {
            const auto& info = p.block("Info");
            const auto handle = (std::uint64_t(options_.x) << 32) | options_.y;
            if (!current.root)
                return;
            const auto position = vector(info.at("Position"));
            const auto look_at = vector(info.at("LookAt"));
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
                !std::isfinite(look_at.x) || !std::isfinite(look_at.y) || !std::isfinite(look_at.z)) {
                invalid_teleport = true;
            } else if (std::get<std::uint64_t>(info.at("RegionHandle")) != handle) {
                other = other_;
            } else {
                if (current.seat || current.ground_sitting)
                    before_seat_exit = current;
                current.position = position;
                current.camera_position = position;
                current.seat.reset();
                current.ground_sitting = false;
                current.look_at = look_at;
                current.look_at.z = 0;
                if (std::abs(look_at.x) < 0.01f && std::abs(look_at.y) < 0.01f)
                    current.look_at = {1, 0, 0};
                current.velocity = {};
                current.acceleration = {};
                if (options_.prepare_root)
                    options_.prepare_root(current);
                local_teleport = true;
                other = other_;
            }
        } else if (p.spec->name == "AgentUpdate") {
            auto& b = p.block("AgentData");
            current.controls = number(b, "ControlFlags");
            current.mute_collisions = (number(b, "Flags") & 0x80u) != 0;
            current.agent_state = static_cast<std::uint8_t>(number(b, "State"));
            current.flying = !current.seat && !current.ground_sitting &&
                             (region_flags() & block_flight) == 0 && (current.controls & fly_control) != 0;
            current.look_at = vector(b.at("CameraAtAxis"));
            current.camera_position = vector(b.at("CameraCenter"));
            const auto left = vector(b.at("CameraLeftAxis"));
            const auto up = vector(b.at("CameraUpAxis"));
            current.camera_at_axis = camera_axis_normalized(current.look_at);
            current.camera_left_axis = camera_axis_normalized(left);
            current.camera_up_axis = camera_axis_normalized(up);
            current.camera_rotation = rotation_from_axes(current.camera_at_axis,
                                                        current.camera_left_axis,
                                                        current.camera_up_axis);
            constexpr std::uint32_t mouselook = 0x40000u;
            if (current.controls & mouselook)
                current.mouselook_rotation = rotation_from_axes(current.look_at, left, up);
            else
                current.mouselook_rotation.reset();
            auto& q = std::get<std::vector<double>>(b.at("BodyRotation"));
            current.rotation = {static_cast<float>(q.at(0)), static_cast<float>(q.at(1)),
                                static_cast<float>(q.at(2)), static_cast<float>(q.at(3))};
            other = other_;
        } else if (p.spec->name == "AgentFOV") {
            const auto& fov = p.block("FOVBlock");
            const auto generation = number(fov, "GenCounter");
            if (generation == 0 || generation > current.camera_fov_generation) {
                current.camera_fov_generation = generation;
                current.camera_fov = static_cast<float>(std::get<double>(fov.at("VerticalAngle")));
            }
        } else if (p.spec->name == "AgentHeightWidth") {
            const auto& viewport = p.block("HeightWidthBlock");
            current.view_width = number(viewport, "Width");
            current.view_height = number(viewport, "Height");
        } else if (p.spec->name == "SetAlwaysRun") {
            current.always_run = std::get<bool>(p.block("AgentData").at("AlwaysRun"));
            other = other_;
        } else if (p.spec->name == "LogoutRequest") {
            current.logout_requested = true;
            event = logout_;
            logout = true;
        } else
            other = other_;
        a = current;
    }
    if (p.spec->name == "AgentThrottle" &&
        number(p.block("AgentData"), "CircuitCode") == connection.circuit_code)
        transport_.set_throttles(connection.agent,
                                 std::get<std::vector<std::uint8_t>>(p.block("Throttle").at("Throttles")));
    if (move)
        transport_.send(connection.agent, movement_complete(a));
    if (invalid_teleport) {
        auto failure = lludp::make_packet("TeleportFailed");
        auto& fields = failure.block("Info");
        fields["AgentID"] = connection.agent;
        fields["Reason"] = text("Invalid teleport coordinates");
        transport_.send(connection.agent, std::move(failure));
    }
    if (local_teleport) {
        if (before_seat_exit && options_.local_seat_exit)
            options_.local_seat_exit(*before_seat_exit, a);
        const auto& request = p.block("Info");
        const auto requested =
            request.contains("RequestedFlags") ? std::get<std::uint64_t>(request.at("RequestedFlags")) : 16u;
        const std::uint64_t flags = requested | (a.flying ? 8192u : 0u);
        auto start = lludp::make_packet("TeleportStart");
        start.block("Info")["TeleportFlags"] = flags;
        transport_.send(connection.agent, std::move(start));
        auto reply = lludp::make_packet("TeleportLocal");
        auto& info = reply.block("Info");
        info["AgentID"] = connection.agent;
        info["LocationID"] = std::uint64_t{2};
        info["Position"] = vector(a.position);
        info["LookAt"] = vector(a.look_at);
        info["TeleportFlags"] = flags;
        transport_.send(connection.agent, std::move(reply));
    }
    if (logout) {
        auto reply = lludp::make_packet("LogoutReply");
        reply.block("AgentData")["AgentID"] = connection.agent;
        reply.block("AgentData")["SessionID"] = connection.session;
        auto item = lludp::make_block(reply.spec->blocks.at(1));
        item["ItemID"] = Uuid{};
        reply.blocks["InventoryData"].push_back(std::move(item));
        transport_.send(connection.agent, std::move(reply));
    }
    if (event)
        event(a);
    if (other)
        other(a, p);
}
void ViewerRegion::closed(const CircuitConnection& connection) {
    Event event;
    std::optional<ViewerAgent> value;
    {
        std::lock_guard lock(mutex_);
        const auto i = agents_.find(connection.agent);
        if (i == agents_.end() || i->second.circuit->session_id != connection.session)
            return;
        value = i->second;
        const auto slot = std::find(presence_slots_.begin(), presence_slots_.end(), connection.agent);
        free_presence_slots_.push_back(static_cast<std::size_t>(slot - presence_slots_.begin()));
        slot->reset();
        agents_.erase(i);
        event = closed_;
    }
    if (event)
        event(*value);
}
void ViewerRegion::disconnect(Uuid id) {
    transport_.remove_client(id);
    circuits_.remove_circuit(id);
}
void ViewerRegion::close() {
    transport_.close();
}
}
