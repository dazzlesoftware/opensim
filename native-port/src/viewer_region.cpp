// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/viewer_region.hpp"
#include "opensim/framework/background_task_queue.hpp"
#include "circuit_trace.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/scene/terrain_layer.hpp"
#include <algorithm>
#include <ctime>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <utility>

namespace opensim::scene {
using namespace framework;
using namespace network;

namespace {
std::vector<std::uint8_t> text(const std::string &value, std::size_t max = 255) {
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

Vector3 vector(const lludp::FieldValue &value) {
    auto &a = std::get<std::vector<double>>(value);
    return {static_cast<float>(a.at(0)), static_cast<float>(a.at(1)), static_cast<float>(a.at(2))};
}

std::uint32_t number(const lludp::Block &block, const char *name) {
    return static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at(name)));
}

Vector3 camera_axis_normalized(Vector3 axis) {
    const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (length > 0.f)
        return {axis.x / length, axis.y / length, axis.z / length};
    return {};
}

Quaternion rotation_from_horizontal_look(Vector3 look_at) {
    const float length_squared = look_at.x * look_at.x + look_at.y * look_at.y;
    if (length_squared < 0.0001f)
        return {0, 0, 0, 1};

    const float half_yaw = 0.5f * std::acos(look_at.x / std::sqrt(length_squared));
    const float yaw_sine = std::sin(half_yaw);
    return {0, 0, look_at.y >= 0 ? yaw_sine : -yaw_sine, std::cos(half_yaw)};
}

Quaternion rotation_from_axes(Vector3 forward, Vector3 left, Vector3 up) {
    const float trace = forward.x + left.y + up.z + 1.f;
    if (trace >= 1.f) {
        const float scale = .5f / std::sqrt(trace);
        return {(left.z - up.y) * scale, (up.x - forward.z) * scale, (forward.y - left.x) * scale, .25f / scale};
    }
    const float largest = std::max(left.y, up.z);
    if (largest < forward.x) {
        const float length = std::sqrt(forward.x - (left.y + up.z) + 1.f);
        const float scale = .5f / length;
        return {.5f * length, (forward.y + left.x) * scale, (up.x + forward.z) * scale, (left.z - up.y) * scale};
    }
    if (largest == left.y) {
        const float length = std::sqrt(left.y - (up.z + forward.x) + 1.f);
        const float scale = .5f / length;
        return {(forward.y + left.x) * scale, .5f * length, (left.z + up.y) * scale, (up.x - forward.z) * scale};
    }
    const float length = std::sqrt(up.z - (forward.x + left.y) + 1.f);
    const float scale = .5f / length;
    return {(up.x + forward.z) * scale, (left.z + up.y) * scale, .5f * length, (forward.y - left.x) * scale};
}
} // namespace

ViewerRegion::ViewerRegion(World &world, AgentCircuitManager &circuits, CircuitTransport::Sender sender,
                           Admission admission, ViewerRegionOptions options, CircuitTransportOptions transport_options)
    : world_(world), circuits_(circuits), admission_(std::move(admission)), options_(std::move(options)),
      transport_(
          circuits, std::move(sender),
          [this](const auto &circuit, const auto &login, const auto &endpoint) {
              return admit(circuit, login, endpoint);
          },
          [this](const auto &circuit, const auto &incoming) { packet(circuit, incoming); },
          [this](const auto &circuit) { closed(circuit); }, transport_options) {
    transport_.set_reuse_callbacks(
        [this](const auto &connection) {
            const auto presence = agent(connection.agent);
            return presence && presence->circuit->session_id == connection.session;
        },
        [this](const auto &connection) { transport_.send_if_current(connection, region_handshake(connection.agent)); });
}

ViewerRegion::~ViewerRegion() {
    close();
}

void ViewerRegion::enable_background_admission(const std::shared_ptr<void> &owner,
                                               std::function<void(std::exception_ptr)> error) {
    const auto weak_viewer = weak_from_this();
    if (!owner || weak_viewer.expired())
        throw std::invalid_argument("Background admission requires shared viewer and host ownership");
    if (closing_ || admission_queue_)
        throw std::logic_error("Viewer admission dispatcher is already configured or closed");
    admission_queue_ = std::make_shared<framework::BackgroundTaskQueue>(std::move(error));
    std::weak_ptr<framework::BackgroundTaskQueue> weak_queue = admission_queue_;
    std::weak_ptr<void> weak_owner = owner;
    transport_.set_admission_scheduler([weak_queue, weak_owner, weak_viewer](auto job) {
        const auto queue = weak_queue.lock();
        if (!queue || !queue->submit([weak_owner, weak_viewer, job = std::move(job)] {
                // Destruction order matters: release the viewer before the
                // owner containing its World and callback dependencies.
                const auto lifetime = weak_owner.lock();
                const auto viewer = weak_viewer.lock();
                if (!lifetime || !viewer || viewer->closing_)
                    return;
                job();
                if (!viewer->closing_)
                    viewer->poll();
            }))
            throw std::runtime_error("Viewer admission dispatcher is closed");
    });
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

float clamp_draw_distance(float distance, float maximum) {
    return std::min(std::max(distance, 32.f), maximum);
}

void apply_arrival_state(ViewerAgent &value, std::uint32_t region_flags, const ViewerRegionOptions &options) {
    const auto &circuit = value.circuit;
    value.position = circuit->start_position;
    value.camera_position = value.position;
    value.draw_distance = circuit->start_far > 32.f ? std::min(circuit->start_far, options.max_draw_distance)
                                                    : options.default_draw_distance;
    if (const auto &camera = circuit->transfer_camera) {
        value.camera_position = camera->center;
        value.camera_at_axis = camera->at_axis;
        value.camera_left_axis = camera->left_axis;
        value.camera_up_axis = camera->up_axis;
        value.camera_rotation = rotation_from_axes(camera->at_axis, camera->left_axis, camera->up_axis);
        if (camera->draw_distance)
            value.draw_distance = clamp_draw_distance(*camera->draw_distance, options.max_draw_distance);
    }
    if (circuit->transfer_look_at)
        value.look_at = *circuit->transfer_look_at;
    if (const auto &movement = circuit->transfer_movement) {
        const auto &velocity = movement->velocity;
        if (std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z))
            value.velocity = velocity;
        const auto &rotation = movement->rotation;
        const float squared_length =
            rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
        if (squared_length > 0.f && std::isfinite(squared_length))
            value.rotation = rotation;
        value.controls = movement->controls;
        value.always_run = movement->always_run;
        value.head_rotation = movement->head_rotation;
    }
    value.flying = (region_flags & block_flight) == 0 &&
                   ((circuit->teleport_flags & 8192u) != 0 || (value.controls & fly_control) != 0);
    if (value.flying)
        value.controls |= fly_control;
}
} // namespace

bool ViewerRegion::admit(const std::shared_ptr<AgentCircuitData> &circuit, const Login &login,
                         const UdpEndpoint &endpoint) {
    if (closing_ || !admission_ || !admission_(circuit, login, endpoint))
        return false;
    users_logging_in_.fetch_add(1, std::memory_order_relaxed);

    struct AdmissionCounter {
        std::atomic<std::size_t> &count;

        ~AdmissionCounter() {
            count.fetch_sub(1, std::memory_order_relaxed);
        }
    } pending{users_logging_in_};

    ViewerAgent value;
    value.circuit = circuit;
    value.endpoint = endpoint;
    value.connected = true;
    // A neighbour circuit remains a child until an incoming crossing is staged.
    value.crossed_child = circuit->neighbour_child || circuit->awaiting_transfer_data;
    value.position = circuit->start_position;
    if (!closing_) {
        value.local_id = world_.allocate_local_id();
        apply_arrival_state(value, region_flags(), options_);
    }
    Event aborted;
    bool inserted = false;
    {
        std::lock_guard lock(mutex_);
        if (closing_)
            aborted = closed_;
        else
            inserted = insert_presence_locked(value, true);
    }
    // The host may have created per-client resources before close began.
    // No presence/transport entry exists yet to deliver their usual teardown.
    if (aborted)
        aborted(value);
    network::detail::circuit_trace(inserted ? "presence-inserted" : "presence-rejected", circuit->agent_id);
    return inserted;
}

bool ViewerRegion::insert_presence(ViewerAgent value, bool replace) {
    std::lock_guard lock(mutex_);
    return insert_presence_locked(std::move(value), replace);
}

bool ViewerRegion::insert_presence_locked(ViewerAgent value, bool replace) {
    const auto id = value.circuit->agent_id;
    const auto [entry, inserted] = agents_.try_emplace(id, std::move(value));
    if (!inserted) {
        if (!replace || entry->second.npc_owner)
            return false;
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

bool ViewerRegion::add_npc(const std::shared_ptr<AgentCircuitData> &circuit, Uuid owner, bool sense_as_agent) {
    if (!circuit || circuit->agent_id.is_zero())
        return false;
    ViewerAgent avatar;
    avatar.circuit = circuit;
    avatar.local_id = world_.allocate_local_id();
    avatar.npc_owner = owner;
    using namespace std::chrono;
    const auto now = floor<seconds>(system_clock::now());
    const auto day = floor<days>(now);
    const year_month_day date{day};
    const hh_mm_ss time{now - day};
    char born[32];
    std::snprintf(born, sizeof(born), "%02u/%02u/%04d %02lld:%02lld:%02lld", unsigned(date.month()),
                  unsigned(date.day()), int(date.year()), static_cast<long long>(time.hours().count()),
                  static_cast<long long>(time.minutes().count()), static_cast<long long>(time.seconds().count()));
    avatar.npc_profile_born.assign(born, born + std::char_traits<char>::length(born));
    avatar.npc_sense_as_agent = sense_as_agent;
    avatar.root = true;
    apply_arrival_state(avatar, region_flags(), options_);
    return insert_presence(std::move(avatar), false);
}

std::optional<ViewerAgent> ViewerRegion::set_npc_rotation(Uuid id, framework::Quaternion rotation) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end() || !entry->second.npc_owner)
        return std::nullopt;
    entry->second.rotation = rotation;
    return entry->second;
}

std::optional<ViewerAgent> ViewerRegion::set_agent_rotation(Uuid id, framework::Quaternion rotation) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end())
        return std::nullopt;
    entry->second.rotation = rotation;
    return entry->second;
}

void ViewerRegion::set_npc_profile(Uuid id, std::optional<std::u16string> about, std::optional<Uuid> image) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end() || !entry->second.npc_owner)
        return;
    if (about) {
        about->resize(std::min<std::size_t>(about->size(), 255));
        entry->second.npc_profile_about = std::move(*about);
    }
    if (image)
        entry->second.npc_profile_image = *image;
}

void ViewerRegion::set_npc_running(Uuid id, bool running) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry != agents_.end() && entry->second.npc_owner)
        entry->second.always_run = running;
}

bool ViewerRegion::remove_npc(Uuid id) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end() || !entry->second.npc_owner)
        return false;
    const auto slot = std::find(presence_slots_.begin(), presence_slots_.end(), id);
    free_presence_slots_.push_back(static_cast<std::size_t>(slot - presence_slots_.begin()));
    slot->reset();
    agents_.erase(entry);
    return true;
}

std::optional<ViewerAgent> ViewerRegion::agent(Uuid id) const {
    std::lock_guard lock(mutex_);
    auto i = agents_.find(id);
    return i == agents_.end() ? std::nullopt : std::optional(i->second);
}

bool ViewerRegion::set_transfer_child(Uuid id, Uuid session, bool child,
                                      const std::shared_ptr<AgentCircuitData> &expected_circuit) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end() || entry->second.circuit->session_id != session || entry->second.logout_requested ||
        (expected_circuit && entry->second.circuit != expected_circuit))
        return false;
    auto &avatar = entry->second;
    if (child) {
        if (!avatar.root || avatar.transfer_child)
            return false;
        if (network::detail::circuit_trace_enabled())
            network::detail::circuit_trace("transfer-child", id, " region=", world_.region_id().str(),
                                           " circuit=", avatar.circuit.get());
        avatar.root = false;
        avatar.transfer_child = true;
        avatar.root_arrival_signal = std::make_shared<std::atomic_bool>(false);
    } else if (avatar.transfer_child) {
        avatar.root = true;
        avatar.transfer_child = false;
    }
    return true;
}

std::optional<ViewerAgent> ViewerRegion::demote_after_crossing(Uuid id, Uuid session) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end() || entry->second.circuit->session_id != session ||
        (!entry->second.root && !entry->second.transfer_child) || entry->second.logout_requested)
        return std::nullopt;
    auto &avatar = entry->second;
    if (network::detail::circuit_trace_enabled())
        network::detail::circuit_trace("retain-child", id, " region=", world_.region_id().str(),
                                       " circuit=", avatar.circuit.get());
    avatar.root = false;
    avatar.transfer_child = false;
    avatar.crossed_child = true;
    avatar.crossing_arrival_ready = false;
    avatar.origin_region_id = {};
    avatar.root_arrival_signal = std::make_shared<std::atomic_bool>(false);
    avatar.transfer_group_information = false;
    avatar.transfer_crossing_flags = 0;
    avatar.circuit->teleport_flags = 0;
    avatar.early_crossing_completion.reset();
    avatar.seat.reset();
    avatar.ground_sitting = false;
    avatar.controls = 0;
    avatar.velocity = {};
    avatar.acceleration = {};
    return avatar;
}

std::optional<ViewerAgent> ViewerRegion::update_child_position(const std::shared_ptr<AgentCircuitData> &circuit,
                                                               Vector3 position, Vector3 camera, float draw_distance) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(circuit->agent_id);
    if (entry == agents_.end() || entry->second.circuit != circuit || entry->second.root ||
        entry->second.logout_requested || entry->second.crossing_arrival_ready)
        return std::nullopt;
    auto &child = entry->second;
    child.position = position;
    child.camera_position = camera;
    child.draw_distance = clamp_draw_distance(draw_distance, options_.max_draw_distance);
    return child;
}

bool ViewerRegion::stage_crossing_arrival(const std::shared_ptr<AgentCircuitData> &circuit, Uuid origin_region,
                                          bool group_information, std::optional<ActiveViewerGroup> group,
                                          std::shared_ptr<std::atomic_bool> *root_arrival_signal,
                                          std::uint8_t crossing_flags) {
    std::optional<ViewerAgent::EarlyCrossingCompletion> pending;
    {
        std::lock_guard lock(mutex_);
        const auto entry = agents_.find(circuit->agent_id);
        if (entry == agents_.end() || entry->second.root || entry->second.logout_requested ||
            entry->second.circuit->session_id != circuit->session_id)
            return false;
        auto &viewer = entry->second;
        if (network::detail::circuit_trace_enabled())
            network::detail::circuit_trace("stage-arrival", circuit->agent_id, " region=", world_.region_id().str(),
                                           " previous=", viewer.circuit.get(), " circuit=", circuit.get());
        if (viewer.circuit != circuit) {
            viewer.retained_crossing_circuit = viewer.circuit;
            viewer.root_arrival_signal = std::make_shared<std::atomic_bool>(false);
        }
        if (root_arrival_signal)
            *root_arrival_signal = viewer.root_arrival_signal;
        viewer.circuit = circuit;
        viewer.appearance_override.reset();
        viewer.transfer_group_information = group_information;
        viewer.transfer_crossing_flags = crossing_flags;
        if (group)
            viewer.active_group_override = std::move(group);
        viewer.crossed_child = true;
        viewer.crossing_arrival_ready = true;
        viewer.origin_region_id = origin_region;
        apply_arrival_state(viewer, region_flags(), options_);
        pending = std::exchange(viewer.early_crossing_completion, std::nullopt);
    }
    if (pending && std::chrono::steady_clock::now() < pending->deadline) {
        const auto connection = transport_.connection(circuit->agent_id);
        if (connection && connection->generation == pending->connection.generation &&
            connection->session == circuit->session_id) {
            auto completion = lludp::make_packet("CompleteAgentMovement");
            completion.block("AgentData")["CircuitCode"] = std::uint64_t{circuit->circuit_code};
            packet(pending->connection, completion);
        }
    }
    return true;
}

std::shared_ptr<AgentCircuitData> ViewerRegion::cancel_crossing_arrival(Uuid id, Uuid session) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end() || !entry->second.crossed_child || entry->second.circuit->session_id != session)
        return {};
    auto &viewer = entry->second;
    if (viewer.retained_crossing_circuit)
        viewer.circuit = std::move(viewer.retained_crossing_circuit);
    viewer.crossing_arrival_ready = false;
    viewer.origin_region_id = {};
    viewer.root_arrival_signal = std::make_shared<std::atomic_bool>(false);
    viewer.transfer_group_information = false;
    viewer.transfer_crossing_flags = 0;
    viewer.early_crossing_completion.reset();
    return viewer.circuit;
}

void ViewerRegion::restore_transferred_seat(ViewerAgent &avatar) {
    const auto &transfer = avatar.circuit->transfer_seat;
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
    if (transfer->explicit_target && std::ranges::any_of(agents_, [&](const auto &entry) {
            const auto &seat = entry.second.seat;
            return seat && seat->explicit_target && seat->part == part->id;
        }))
        return;

    auto rotation = avatar.rotation;
    if (part->parent_id) {
        offset = framework::sdk_rotate_vector(offset, part->rotation);
        offset = {offset.x + part->position.x, offset.y + part->position.y, offset.z + part->position.z};
        rotation = framework::sdk_multiply_quaternion(part->rotation, rotation);
    }
    avatar.seat =
        ViewerAgent::Seat{part->id, root->local_id, offset, rotation, transfer->explicit_target, ++seat_order_};
    const auto world_offset = framework::sdk_rotate_vector(offset, root->rotation);
    avatar.position = {root->position.x + world_offset.x, root->position.y + world_offset.y,
                       root->position.z + world_offset.z};
    avatar.velocity = {};
    avatar.acceleration = {};
    avatar.flying = false;
}

std::optional<ViewerAgent> ViewerRegion::update_physics(Uuid id, Uuid session, Vector3 position, Vector3 velocity,
                                                        Vector3 acceleration, bool flying,
                                                        std::optional<framework::Quaternion> rotation,
                                                        std::array<float, 4> collision_plane,
                                                        Vector3 angular_velocity) {
    std::lock_guard lock(mutex_);
    auto i = agents_.find(id);
    if (i == agents_.end() || !i->second.root || i->second.circuit->session_id != session || i->second.seat ||
        i->second.ground_sitting)
        return {};
    auto &a = i->second;
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

std::optional<ViewerAgent> ViewerRegion::update_seat(Uuid id, Uuid session, std::optional<ViewerAgent::Seat> seat,
                                                     Vector3 position, std::optional<framework::Quaternion> rotation,
                                                     std::optional<std::uint64_t> expected_seat) {
    std::lock_guard lock(mutex_);
    const auto entry = agents_.find(id);
    if (entry == agents_.end() || !entry->second.root || entry->second.circuit->session_id != session)
        return std::nullopt;
    if (expected_seat && (!entry->second.seat || entry->second.seat->order != *expected_seat))
        return std::nullopt;
    if (seat && seat->explicit_target)
        for (const auto &[other_id, other] : agents_)
            if (other_id != id && other.seat && other.seat->explicit_target && other.seat->part == seat->part)
                return std::nullopt;
    auto &avatar = entry->second;
    if (seat)
        seat->order = ++seat_order_;
    avatar.seat = std::move(seat);
    if (avatar.seat && avatar.npc_owner)
        avatar.controls = 0;
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
    auto &avatar = entry->second;
    avatar.ground_sitting = sitting;
    avatar.velocity = {};
    avatar.acceleration = {};
    avatar.flying = false;
    return avatar;
}

std::vector<ViewerAgent> ViewerRegion::refresh_seated_positions(Uuid expected_root) {
    std::vector<ViewerAgent> seated;
    {
        std::lock_guard lock(mutex_);
        for (const auto &[id, avatar] : agents_)
            if (avatar.seat)
                seated.push_back(avatar);
    }
    std::vector<ViewerAgent> reparented;
    for (const auto &avatar : seated) {
        const auto part = world_.object(avatar.seat->part);
        if (!part || part->is_attachment)
            continue;
        const auto root = part->parent_id ? world_.object(part->parent_id) : part;
        if (!root)
            continue;
        if (!expected_root.is_zero() && root->id != expected_root)
            continue;
        const auto offset = framework::sdk_rotate_vector(avatar.seat->offset, root->rotation);
        std::lock_guard lock(mutex_);
        const auto current = agents_.find(avatar.circuit->agent_id);
        if (current == agents_.end() || current->second.circuit->session_id != avatar.circuit->session_id ||
            !current->second.seat || current->second.seat->order != avatar.seat->order)
            continue;
        auto &updated = current->second;
        updated.position = {root->position.x + offset.x, root->position.y + offset.y, root->position.z + offset.z};
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
    for (auto &[id, a] : agents_)
        out.push_back(a);
    return out;
}

std::vector<ViewerAgent> ViewerRegion::root_presences() const {
    std::lock_guard lock(mutex_);
    std::vector<ViewerAgent> presences;
    for (const auto &id : presence_slots_) {
        if (!id)
            continue;
        const auto &agent = agents_.at(*id);
        if (agent.root && !agent.logout_requested)
            presences.push_back(agent);
    }
    return presences;
}

std::vector<std::string> ViewerRegion::root_agent_names() const {
    std::vector<std::string> names;
    for (auto &avatar : root_avatars())
        names.push_back(std::move(avatar.name));
    return names;
}

std::vector<RootAvatar> ViewerRegion::root_avatars() const {
    std::lock_guard lock(mutex_);
    std::vector<RootAvatar> avatars;
    for (const auto &id : presence_slots_) {
        if (!id)
            continue;
        const auto &agent = agents_.at(*id);
        if (agent.root && !agent.npc_owner)
            avatars.push_back({*id, agent.position, agent.circuit->name()});
    }
    return avatars;
}

void ViewerRegion::receive(const UdpEndpoint &endpoint, std::span<const std::uint8_t> bytes) {
    if (closing_)
        return;
    transport_.receive(endpoint, bytes);
    poll();
}

void ViewerRegion::send_terrain(Uuid id) {
    std::vector<TerrainPatchCoordinate> patches;
    auto &terrain = world_.terrain();
    for (std::uint32_t y = 0; y < terrain.patches_y(); ++y)
        for (std::uint32_t x = 0; x < terrain.patches_x(); ++x)
            patches.push_back({x, y});
    for (auto &layer : terrain_layer_packets(terrain, patches)) {
        auto p = lludp::make_packet("LayerData");
        p.block("LayerID")["Type"] = std::uint64_t(layer.type);
        p.block("LayerData")["Data"] = std::move(layer.data);
        transport_.enqueue(id, std::move(p), ThrottleOutPacketType::Land);
    }
}

void ViewerRegion::broadcast_terrain_changes() {
    auto &terrain = world_.terrain();
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
    for (auto &layer : terrain_layer_packets(terrain, patches)) {
        auto packet = lludp::make_packet("LayerData");
        packet.block("LayerID")["Type"] = std::uint64_t(layer.type);
        packet.block("LayerData")["Data"] = std::move(layer.data);
        for (const auto &viewer : recipients)
            if (viewer.root)
                transport_.enqueue_if_session(viewer.circuit->agent_id, viewer.circuit->session_id, packet,
                                              ThrottleOutPacketType::Land);
    }
}

void ViewerRegion::poll() {
    transport_.poll();
    auto clients = agents();
    for (auto &a : clients) {
        if (a.handshake_sent || a.circuit->teleport_flags != 0 || !transport_.connection(a.circuit->agent_id))
            continue;
        {
            std::lock_guard lock(mutex_);
            auto i = agents_.find(a.circuit->agent_id);
            if (i == agents_.end() || i->second.handshake_sent || i->second.circuit->teleport_flags != 0)
                continue;
            i->second.handshake_sent = true;
        }
        network::detail::circuit_trace("handshake-ready", a.circuit->agent_id);
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

void ViewerRegion::set_speed_modifier(Uuid id, float modifier) {
    std::lock_guard lock(mutex_);
    if (const auto avatar = agents_.find(id); avatar != agents_.end())
        avatar->second.speed_modifier = modifier;
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
    auto &avatar = found->second;
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
    auto &r = p.block("RegionInfo");
    r["RegionFlags"] = std::uint64_t(flags);
    r["SimAccess"] = std::uint64_t(options_.current_access ? options_.current_access() : options_.access);
    r["SimName"] = text(options_.name);
    const auto [owner, billable_factor] = options_.current_estate_owner_and_billable_factor
                                              ? options_.current_estate_owner_and_billable_factor()
                                              : std::pair{options_.owner, options_.billable_factor};
    r["SimOwner"] = owner;
    r["IsEstateManager"] =
        options_.estate_manager ? options_.estate_manager(agent) : (!agent.is_zero() && agent == owner);
    r["WaterHeight"] = double(options_.current_water_height ? options_.current_water_height() : options_.water_height);
    r["BillableFactor"] = double(billable_factor);
    r["CacheID"] = options_.cache_id;
    const char *corners[] = {"00", "01", "10", "11"};
    const auto heights = options_.terrain_texture_heights ? options_.terrain_texture_heights()
                                                          : ViewerRegionOptions::TerrainTextureHeights{};
    const auto textures =
        options_.terrain_texture_assets ? options_.terrain_texture_assets(terrain_pbr) : options_.terrain_details;
    for (int i = 0; i < 4; ++i) {
        r["TerrainDetail" + std::to_string(i)] = textures[i];
        r[std::string("TerrainStartHeight") + corners[i]] = double(heights.low[i]);
        r[std::string("TerrainHeightRange") + corners[i]] = double(heights.high[i]);
    }
    p.block("RegionInfo2")["RegionID"] = world_.region_id();
    auto &product = p.block("RegionInfo3");
    product["CPUClassID"] = std::int64_t(9);
    product["CPURatio"] = std::int64_t(1);
    product["ProductName"] = text(options_.product);
    auto info = lludp::make_block(p.spec->blocks.at(3));
    info["RegionFlagsExtended"] = std::uint64_t(flags);
    info["RegionProtocols"] = std::uint64_t(1) << 63;
    p.blocks["RegionInfo4"].push_back(std::move(info));
    return p;
}

lludp::Packet ViewerRegion::movement_complete(const ViewerAgent &a) const {
    auto p = lludp::make_packet("AgentMovementComplete");
    p.block("AgentData")["AgentID"] = a.circuit->agent_id;
    p.block("AgentData")["SessionID"] = a.circuit->session_id;
    auto &d = p.block("Data");
    auto pos = a.position;
    if (pos == Vector3{})
        pos = a.circuit->start_position;
    d["Position"] = vector(pos);
    d["LookAt"] = vector(a.look_at);
    d["RegionHandle"] = (std::uint64_t(options_.x) << 32) | options_.y;
    d["Timestamp"] =
        std::uint64_t(options_.unix_time ? options_.unix_time() : static_cast<std::uint32_t>(std::time(nullptr)));
    p.block("SimData")["ChannelVersion"] = text(options_.channel_version, 65535);
    return p;
}

bool ViewerRegion::request_teleport(Uuid agent_id, Uuid session, const lludp::Packet &request) {
    const auto connection = transport_.connection(agent_id);
    const auto viewer = agent(agent_id);
    if (!connection || connection->session != session || !viewer || !viewer->root || viewer->logout_requested ||
        viewer->circuit->session_id != session)
        return false;
    packet(*connection, request);
    return true;
}

void ViewerRegion::packet(const CircuitConnection &connection, const lludp::Packet &p) {
    // The source inbound handler consumes one packet at a time. Admission can
    // replay queued packets from another worker, but host callbacks stay serial.
    std::lock_guard packet_lock(packet_mutex_);
    const auto teleport_failure = [&](const std::string &reason) {
        auto failure = lludp::make_packet("TeleportFailed");
        failure.block("Info")["AgentID"] = connection.agent;
        failure.block("Info")["Reason"] = text(reason);
        transport_.enqueue_if_session(connection.agent, connection.session, std::move(failure),
                                      ThrottleOutPacketType::Unknown);
    };
    if (p.spec->name == "TeleportLocationRequest" && options_.teleport_pending &&
        options_.teleport_pending(connection.session)) {
        teleport_failure("Previous teleport process incomplete.  Please retry shortly.");
        return;
    }
    if (p.spec->name == "AgentUpdate" && options_.teleport_pending && options_.teleport_pending(connection.session))
        return;
    if (p.spec->name == "TeleportLandmarkRequest") {
        const auto viewer = agent(connection.agent);
        const auto &info = p.block("Info");
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
        const auto reason = options_.resolve_landmark ? options_.resolve_landmark(*viewer, landmark, destination)
                                                      : (landmark.is_zero() ? "Your home region not found"
                                                                            : "Could not find the landmark asset data");
        if (reason.empty())
            packet(connection, destination);
        else
            teleport_failure(reason);
        return;
    }
    auto value = agent(connection.agent);
    if (!value) {
        if (p.spec->name == "TeleportLocationRequest")
            network::detail::circuit_trace("teleport-no-presence", connection.agent, " sequence=", p.sequence);
        return;
    }
    auto a = *value;
    Event event;
    PacketSink other;
    std::optional<lludp::Packet> normalized_teleport;
    std::optional<ViewerAgent> before_seat_exit;
    bool move = false, arrival_handshake = false, logout = false, local_teleport_rejected = false;
    std::optional<Vector3> local_teleport_reply;
    const char *teleport_error = nullptr;
    {
        std::lock_guard lock(mutex_);
        auto i = agents_.find(connection.agent);
        if (i == agents_.end() || i->second.circuit->session_id != connection.session)
            return;
        auto &current = i->second;
        if (p.spec->name == "RegionHandshakeReply") {
            current.handshake_reply = true;
            current.viewer_flags = number(p.block("RegionInfo"), "Flags");
            event = handshake_;
        } else if (p.spec->name == "CompleteAgentMovement") {
            if (number(p.block("AgentData"), "CircuitCode") != connection.circuit_code || current.root ||
                current.logout_requested)
                return;
            constexpr std::uint32_t via_login = 1u << 7, via_hg_login = 1u << 30;
            const bool real_login = (current.circuit->teleport_flags & (via_login | via_hg_login)) == via_login;
            const bool requires_transfer_update = current.crossed_child && !current.npc_owner && !real_login;
            if (requires_transfer_update && !current.crossing_arrival_ready) {
                const auto now = std::chrono::steady_clock::now();
                if (!current.early_crossing_completion || now >= current.early_crossing_completion->deadline)
                    current.early_crossing_completion =
                        ViewerAgent::EarlyCrossingCompletion{now + std::chrono::seconds(10), connection};
                return;
            }
            if (requires_transfer_update && current.origin_region_id.is_zero()) {
                // WaitForUpdateAgent consumes the update notification even
                // when CopyFrom supplied an invalid (zero) origin region.
                current.crossing_arrival_ready = false;
                current.transfer_group_information = false;
                current.early_crossing_completion.reset();
                return;
            }
            if (current.crossing_arrival_ready) {
                current.appearance_override.reset();
                if (!current.transfer_group_information)
                    current.active_group_override.reset();
                current.mouselook_rotation.reset();
                apply_arrival_state(current, region_flags(), options_);
            }
            restore_transferred_seat(current);
            if (options_.prepare_root)
                options_.prepare_root(current, false);
            // Teleports reset the camera center after landing. Crossings retain the transferred center.
            if (current.circuit->teleport_flags != 0) {
                current.camera_position = current.position;
                current.velocity = {};
            }
            // ScenePresence.CompleteMovement sends a fresh region handshake
            // for login/teleport, but not for an AgentData crossing update.
            arrival_handshake = current.circuit->teleport_flags != 0 || current.transfer_crossing_flags == 0;
            if (arrival_handshake) {
                current.handshake_sent = true;
                current.handshake_reply = false;
            }
            current.root = true;
            if (network::detail::circuit_trace_enabled())
                network::detail::circuit_trace("root-arrival", connection.agent, " region=", world_.region_id().str(),
                                               " circuit=", current.circuit.get());
            current.transfer_child = false;
            current.root_arrival_signal->store(true);
            current.crossed_child = false;
            current.crossing_arrival_ready = false;
            current.retained_crossing_circuit.reset();
            event = entered_;
            move = true;
        } else if (p.spec->name == "TeleportLocationRequest") {
            const auto &info = p.block("Info");
            if (network::detail::circuit_trace_enabled())
                network::detail::circuit_trace("teleport-request", connection.agent,
                                               " region=", world_.region_id().str(), " root=", current.root,
                                               " circuit=", current.circuit.get(), " sequence=", p.sequence);
            if (!current.root)
                return;
            const auto position = vector(info.at("Position"));
            const auto look_at = vector(info.at("LookAt"));
            const auto handle = std::get<std::uint64_t>(info.at("RegionHandle"));
            const double offset_x = double(static_cast<std::uint32_t>(handle >> 32)) - options_.x;
            const double offset_y = double(static_cast<std::uint32_t>(handle)) - options_.y;
            const auto inside = [&](double x, double y) {
                return x >= 0 && x < world_.terrain().width() && y >= 0 && y < world_.terrain().height();
            };
            double target_x = offset_x + position.x;
            double target_y = offset_y + position.y;
            const auto base_handle = (std::uint64_t(options_.x) << 32) | options_.y;
            const bool viewer_subregion =
                !info.contains("RequestedFlags") && handle != base_handle && inside(offset_x, offset_y);
            if (viewer_subregion) {
                target_x = static_cast<float>(target_x);
                target_y = static_cast<float>(target_y);
            }
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
                !std::isfinite(look_at.x) || !std::isfinite(look_at.y) || !std::isfinite(look_at.z)) {
                teleport_error = "Invalid teleport coordinates";
            } else if (!inside(target_x, target_y)) {
                if (viewer_subregion) {
                    normalized_teleport = p;
                    auto &request = normalized_teleport->block("Info");
                    request["RegionHandle"] = base_handle;
                    request["Position"] = std::vector<double>{target_x, target_y, position.z};
                }
                other = other_;
            } else if (options_.movement_frozen && options_.movement_frozen(current)) {
                teleport_error = "You are frozen";
            } else {
                ViewerAgent landing = current;
                landing.position = {static_cast<float>(target_x), static_cast<float>(target_y), position.z};
                landing.camera_position = landing.position;
                landing.seat.reset();
                landing.ground_sitting = false;
                if (options_.prepare_root)
                    options_.prepare_root(landing, true);
                local_teleport_reply = landing.position;
                const auto flags =
                    info.contains("RequestedFlags") ? std::get<std::uint64_t>(info.at("RequestedFlags")) : 16u;
                const auto routed = options_.route_local_teleport
                                        ? options_.route_local_teleport(current, landing.position, flags)
                                        : local_teleport_reply;
                current.look_at = look_at;
                current.look_at.z = 0;
                if (std::abs(look_at.x) < 0.01f && std::abs(look_at.y) < 0.01f)
                    current.look_at = {1, 0, 0};
                if (!current.seat)
                    current.rotation = rotation_from_horizontal_look(current.look_at);
                current.velocity = {};
                current.acceleration = {};
                if (routed) {
                    if (current.seat || current.ground_sitting)
                        before_seat_exit = current;
                    if (current.seat) {
                        if (const auto root = world_.object(current.seat->parent_local))
                            current.rotation = sdk_multiply_quaternion(root->rotation, current.seat->rotation);
                    }
                    current.position = *routed;
                    current.camera_position = *routed;
                    current.appearance_override = std::move(landing.appearance_override);
                    current.seat.reset();
                    current.ground_sitting = false;
                } else {
                    local_teleport_rejected = true;
                }
            }
        } else if (p.spec->name == "AgentUpdate") {
            if (!current.root || p.sequence <= current.last_agent_update_sequence)
                return;
            current.last_agent_update_sequence = p.sequence;
            auto &b = p.block("AgentData");
            current.draw_distance =
                clamp_draw_distance(static_cast<float>(std::get<double>(b.at("Far"))), options_.max_draw_distance);
            current.controls = number(b, "ControlFlags");
            current.mute_collisions = (number(b, "Flags") & 0x80u) != 0;
            current.agent_state = static_cast<std::uint8_t>(number(b, "State"));
            current.flying = !current.seat && !current.ground_sitting && (region_flags() & block_flight) == 0 &&
                             (current.controls & fly_control) != 0;
            current.look_at = vector(b.at("CameraAtAxis"));
            current.camera_position = vector(b.at("CameraCenter"));
            const auto left = vector(b.at("CameraLeftAxis"));
            const auto up = vector(b.at("CameraUpAxis"));
            current.camera_at_axis = camera_axis_normalized(current.look_at);
            current.camera_left_axis = camera_axis_normalized(left);
            current.camera_up_axis = camera_axis_normalized(up);
            current.camera_rotation =
                rotation_from_axes(current.camera_at_axis, current.camera_left_axis, current.camera_up_axis);
            constexpr std::uint32_t mouselook = 0x40000u;
            if (current.controls & mouselook)
                current.mouselook_rotation = rotation_from_axes(current.look_at, left, up);
            else
                current.mouselook_rotation.reset();
            auto &q = std::get<std::vector<double>>(b.at("BodyRotation"));
            current.rotation = {static_cast<float>(q.at(0)), static_cast<float>(q.at(1)), static_cast<float>(q.at(2)),
                                static_cast<float>(q.at(3))};
            const auto &head = std::get<std::vector<double>>(b.at("HeadRotation"));
            current.head_rotation = {static_cast<float>(head.at(0)), static_cast<float>(head.at(1)),
                                     static_cast<float>(head.at(2)), static_cast<float>(head.at(3))};
            other = other_;
        } else if (p.spec->name == "AgentFOV") {
            const auto &fov = p.block("FOVBlock");
            const auto generation = number(fov, "GenCounter");
            if (generation == 0 || generation > current.camera_fov_generation) {
                current.camera_fov_generation = generation;
                current.camera_fov = static_cast<float>(std::get<double>(fov.at("VerticalAngle")));
            }
        } else if (p.spec->name == "AgentHeightWidth") {
            const auto &viewport = p.block("HeightWidthBlock");
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
    if (move && a.circuit->transfer_throttles.size() >= 28)
        transport_.set_throttles_if_current(connection, a.circuit->transfer_throttles);
    if (arrival_handshake)
        transport_.send_if_current(connection, region_handshake(connection.agent));
    if (move)
        transport_.send_if_current(connection, movement_complete(a));
    if (teleport_error) {
        auto failure = lludp::make_packet("TeleportFailed");
        auto &fields = failure.block("Info");
        fields["AgentID"] = connection.agent;
        fields["Reason"] = text(teleport_error);
        transport_.send_if_current(connection, std::move(failure));
    }
    if (local_teleport_reply) {
        if (before_seat_exit && options_.local_seat_exit)
            options_.local_seat_exit(*before_seat_exit, a);
        const auto &request = p.block("Info");
        const auto requested =
            request.contains("RequestedFlags") ? std::get<std::uint64_t>(request.at("RequestedFlags")) : 16u;
        const std::uint64_t flags = requested | (a.flying ? 8192u : 0u);
        auto start = lludp::make_packet("TeleportStart");
        start.block("Info")["TeleportFlags"] = flags;
        transport_.send_if_current(connection, std::move(start));
        auto reply = lludp::make_packet("TeleportLocal");
        auto &info = reply.block("Info");
        info["AgentID"] = connection.agent;
        info["LocationID"] = std::uint64_t{2};
        info["Position"] = vector(*local_teleport_reply);
        info["LookAt"] = vector(a.look_at);
        info["TeleportFlags"] = flags;
        transport_.send_if_current(connection, std::move(reply));
        if (options_.local_teleport_complete)
            options_.local_teleport_complete(a, !local_teleport_rejected);
    }
    if (logout) {
        auto reply = lludp::make_packet("LogoutReply");
        reply.block("AgentData")["AgentID"] = connection.agent;
        reply.block("AgentData")["SessionID"] = connection.session;
        auto item = lludp::make_block(reply.spec->blocks.at(1));
        item["ItemID"] = Uuid{};
        reply.blocks["InventoryData"].push_back(std::move(item));
        transport_.send_if_current(connection, std::move(reply));
    }
    const auto complete_group_information = [&] {
        if (!move)
            return;
        std::lock_guard lock(mutex_);
        const auto current = agents_.find(connection.agent);
        if (current != agents_.end() && current->second.circuit == a.circuit) {
            current->second.transfer_group_information = false;
            current->second.transfer_crossing_flags = 0;
        }
    };
    try {
        if (event)
            event(a);
        if (other)
            other(a, normalized_teleport ? *normalized_teleport : p);
    } catch (...) {
        complete_group_information();
        throw;
    }
    complete_group_information();
}

void ViewerRegion::closed(const CircuitConnection &connection) {
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
    closing_ = true;
    if (admission_queue_)
        admission_queue_->close();
    transport_.close();
    std::lock_guard lock(mutex_);
    for (std::size_t index = 0; index < presence_slots_.size(); ++index) {
        auto &id = presence_slots_[index];
        if (id && agents_.at(*id).npc_owner) {
            free_presence_slots_.push_back(index);
            agents_.erase(*id);
            id.reset();
        }
    }
}
} // namespace opensim::scene
