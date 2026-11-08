// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/viewer_physics.hpp"
#include "opensim/scene/movement.hpp"
#include "opensim/scene/movement_animation.hpp"
#include "opensim/scene/avatar_packets.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/physics/ubode.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <mutex>
#include <map>
#include <set>
#include <utility>
namespace opensim::scene {
using namespace framework;
namespace {
double milliseconds() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
std::int32_t tick_count() {
    return std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(milliseconds())));
}
}
struct ViewerPhysics::Impl {
    enum class Kind { Enter, Input, Running, Leave, Teleport, Appearance, Buoyancy, Push, Hover, MoveToTarget };
    struct Command {
        Kind kind;
        ViewerAgent agent;
        std::vector<AvatarAnimationEntry> animations;
        std::map<Uuid, Uuid> overrides;
        std::optional<MovementAnimation> movement;
        float buoyancy = 0.f;
        Vector3 impulse{};
        framework::PrimHover hover{};
        framework::PrimMoveTarget move_target{};
    };
    ViewerRegion& viewers;
    std::shared_ptr<const DefaultAnimations> catalog;
    Publish publish;
    Log log;
    AvatarVisibility visible;
    ObjectCrossing cross_object;
    MeshLoader load_mesh;
    World& scene_objects;
    physics::UbodeWorld world;
    const Terrain& terrain;
    std::uint64_t terrain_revision = 0;
    std::map<Uuid, PhysicalPrim> boxes;
    struct MeshRequest {
        std::mutex mutex;
        std::shared_ptr<const physics::AssetPhysicsMesh> mesh;
    };
    using MeshKey = std::pair<Uuid, bool>;
    std::map<MeshKey, std::shared_ptr<MeshRequest>> mesh_requests;
    std::mutex queue_mutex;
    std::vector<Command> queue;
    struct PrimImpulse {
        Uuid id;
        Vector3 impulse;
        bool angular;
    };
    std::vector<PrimImpulse> prim_impulses;
    mutable std::mutex animation_mutex;
    std::map<std::pair<Uuid, Uuid>, MovementAnimation> animation_snapshots;
    std::optional<float> pending_water_height;
    double last_clock = milliseconds();
    float accumulated = 0;
    const ScenePhysicsTiming timing;
    Impl(World& scene, ViewerRegion& v, const std::filesystem::path& library,
         std::shared_ptr<const DefaultAnimations> c, Publish p, Log l, float water_height,
         AvatarVisibility visibility, ObjectCrossing crossing, MeshLoader loader, ScenePhysicsTiming timing)
        : viewers(v), catalog(std::move(c)), publish(std::move(p)), log(std::move(l)),
          visible(std::move(visibility)), cross_object(std::move(crossing)), load_mesh(std::move(loader)),
          scene_objects(scene),
          world(library, scene.terrain(), {timing.frame_time, water_height, true}), terrain(scene.terrain()),
          timing(timing) {
        if (!catalog)
            throw std::invalid_argument("Physics animation catalog required");
    }
    void post(Kind kind, const ViewerAgent& a, std::vector<AvatarAnimationEntry> animations = {},
              std::map<Uuid, Uuid> overrides = {}, std::optional<MovementAnimation> movement = {}) {
        std::lock_guard lock(queue_mutex);
        queue.push_back({kind, a, std::move(animations), std::move(overrides), std::move(movement)});
    }
    struct Avatar {
        Impl& scene;
        ViewerRegion& viewers;
        const DefaultAnimations& catalog;
        const AvatarVisibility& visible;
        physics::UbodeWorld world;
        Uuid owner, owner_session;
        std::uint32_t local_id;
        MovementState movement;
        Vector3 move_destination;
        std::optional<Quaternion> pending_rotation;
        MovementAnimator animator{tick_count};
        std::uint32_t movement_flags = 0,
                      sequence_counter = static_cast<std::uint32_t>(tick_count()) & 0x5fffafu;
        std::int32_t animation_sequence = 1;
        Uuid animation;
        bool always_run = false, stop_active = false;
        double last_publish, last_animation;
        Avatar(Impl& scene, const ViewerAgent& a, double now)
            : scene(scene), viewers(scene.viewers), catalog(*scene.catalog), visible(scene.visible), world(scene.world),
              owner(a.circuit->agent_id), owner_session(a.circuit->session_id), local_id(a.local_id),
              animation(catalog.resolve("STAND")), always_run(a.always_run), last_publish(now),
              last_animation(now) {
            reset_body(a);
            const bool crossing = a.circuit->teleport_flags == 0 && a.circuit->transfer_movement.has_value();
            if (crossing && a.velocity != Vector3{})
                world.set_momentum(a.velocity);
            movement.rotation = a.rotation;
            movement.controls = crossing ? a.controls : a.controls & 0x2000u;
        }
        void reset_body(const ViewerAgent& a) {
            movement.moving_to_target = false;
            movement.move_to_speed = -1.f;
            pending_rotation.reset();
            const auto appearance = a.appearance();
            auto size = appearance ? appearance->avatar_box_size() : Vector3{.45f, .6f, 2.1f};
            world.add_avatar(a.position, size, a.flying, {owner, local_id});
            world.set_orientation(a.rotation);
            world.set_running(always_run);
        }
        void send_animation(Uuid recipient) {
            const auto viewer = viewers.agent(recipient);
            if (viewer && viewer->root)
                send_animation(*viewer);
        }
        void send_animation(const ViewerAgent& viewer) {
            const auto active = viewers.agent(owner);
            if (!active || active->circuit->session_id != owner_session)
                return;
            if (visible && !visible(*active, viewer))
                return;
            const auto recipient = viewer.circuit->agent_id;
            const auto current = effective_animation();
            std::vector<AvatarAnimationEntry> entries;
            if (!current.is_zero())
                entries.push_back({current, {}, animation_sequence});
            for (const auto& entry : script_animations)
                if (entry.animation != current)
                    entries.push_back(entry);
            viewers.transport().enqueue_if_session(recipient, viewer.circuit->session_id,
                                                   avatar_animations(owner, recipient, entries),
                                                   network::ThrottleOutPacketType::Task);
        }
        std::vector<AvatarAnimationEntry> script_animations;
        std::optional<Uuid> default_override;
        std::optional<AvatarAnimationEntry> implicit_animation;
        bool restored_movement = false;
        std::map<Uuid, Uuid> animation_overrides;
        Uuid effective_animation() const {
            if (default_override)
                return *default_override;
            const auto found = animation_overrides.find(animation);
            return found == animation_overrides.end() ? animation : found->second;
        }
        void snapshot_animation() const {
            const AvatarAnimationEntry current{effective_animation(), {}, animation_sequence};
            const auto implicit = implicit_animation.value_or(current);
            std::lock_guard lock(scene.animation_mutex);
            scene.animation_snapshots.insert_or_assign(std::pair{owner, owner_session},
                MovementAnimation{current, implicit, animator.state()});
        }
        bool change_animation(const std::string& name) {
            auto id = catalog.resolve(name);
            if (id.is_zero() || (id == animation && !restored_movement))
                return false;
            const auto previous = effective_animation();
            animation = id;
            default_override.reset();
            implicit_animation.reset();
            if (std::exchange(restored_movement, false) && previous == effective_animation())
                return false;
            advance_animation_sequence();
            for (auto& a : viewers.agents())
                if (a.root)
                    send_animation(a.circuit->agent_id);
            return true;
        }
        void advance_animation_sequence() {
            sequence_counter = (sequence_counter + 1) & 0x7fffffffu;
            if (!sequence_counter)
                sequence_counter = 1;
            animation_sequence = static_cast<std::int32_t>(sequence_counter);
        }
        void animate() {
            if (owner.is_zero())
                return;
            auto state = world.snapshot();
            MovementAnimationContext context;
            context.controls = movement.controls;
            context.stop_active = stop_active;
            context.always_run = always_run;
            context.flying = state.flying;
            context.colliding = state.colliding;
            context.velocity = state.velocity;
            MovementAnimationEffects effects;
            effects.jump = [this](float impulse) { world.avatar_jump(impulse); };
            effects.set_flying = [this](bool value) { world.set_flying(value); };
            effects.set_selected = [](bool) {}; // Source OdeCharacter.Selected setter is a no-op.
            effects.try_set_animation = [this](const auto& name) { return change_animation(name); };
            if (restored_movement)
                animator.force_update(context, effects);
            else
                animator.update(context, effects);
            snapshot_animation();
        }
        void apply_target(Vector3 relative, float speed, bool breaking = false) {
            auto state = world.snapshot();
            movement.flying = state.flying;
            movement.colliding = state.colliding;
            movement.falling = animator.state() == MotionControlState::falling;
            add_new_movement(movement, relative, speed, breaking);
            world.set_target_velocity(movement.target_velocity);
            animate();
        }
        void stop_move_to_target() {
            if (!movement.moving_to_target)
                return;
            movement.moving_to_target = false;
            movement.move_to_speed = -1.f;
            movement.controls = 0;
            movement.delayed_stop = -1;
        }
        void start_move_to_target(const ViewerAgent& agent, PrimMoveTarget target) {
            if (!target.active()) {
                stop_move_to_target();
                return;
            }
            move_destination = target.position;
            move_destination.x = std::clamp(move_destination.x, .5f, scene.terrain.width() - .5f);
            move_destination.y = std::clamp(move_destination.y, .5f, scene.terrain.height() - .5f);
            const auto appearance = agent.appearance();
            const float height = appearance && appearance->avatar_height > 0.f ? appearance->avatar_height : 2.1f;
            const float ground = scene.terrain.height_at(move_destination.x, move_destination.y) + height * .5f + .2f;
            move_destination.z = std::max(move_destination.z, ground);
            const auto state = world.snapshot();
            const bool flying = state.flying || move_destination.z > ground + height;
            world.set_flying(flying);
            always_run = false;
            world.set_running(false);
            const auto p = state.body_position;
            const float dz = flying ? move_destination.z - p.z : 0.f;
            const float distance = std::hypot(move_destination.x - p.x, move_destination.y - p.y, dz);
            movement.move_to_speed = std::clamp(distance / std::max(target.tau, scene.timing.frame_time), .5f, 50.f);
            movement.moving_to_target = true;
            movement.delayed_stop = -1;
        }
        void advance_move_to_target() {
            if (!movement.moving_to_target)
                return;
            const auto state = world.snapshot();
            const auto p = state.body_position;
            const float dx = move_destination.x - p.x;
            const float dy = move_destination.y - p.y;
            const float horizontal = std::hypot(dx, dy);
            if (horizontal <= .5f) {
                auto destination = move_destination;
                if (!state.flying)
                    destination.z = p.z;
                world.snap_avatar_position(destination);
                stop_move_to_target();
                movement_flags = 0;
                apply_target({}, 0.f);
                return;
            }
            const float dz = state.flying ? move_destination.z - p.z : 0.f;
            const float distance = std::hypot(horizontal, dz);
            movement.move_to_speed = std::min(movement.move_to_speed, distance / scene.timing.frame_time);
            const float yaw = .5f * std::atan2(dy, dx);
            movement.rotation = {0, 0, std::sin(yaw), std::cos(yaw)};
            world.set_orientation(movement.rotation);
            pending_rotation = movement.rotation;
            movement_flags = 1;
            movement.controls = 1 | (state.flying ? 0x2000u : 0u);
            apply_target({horizontal, 0, dz}, 4.096f);
        }
        void update_input(const ViewerAgent& agent, double now) {
            auto q = agent.rotation;
            if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w))
                return;
            auto projected = project_controls(agent.controls, 0, always_run, movement_flags);
            if (movement.moving_to_target) {
                if (!projected.key_pressed && !(agent.controls & 0x4000u))
                    return;
                stop_move_to_target();
                pending_rotation.reset();
            }
            movement.rotation = q;
            world.set_orientation(q);
            movement_flags = projected.movement_flags;
            movement.controls = projected.controls;
            bool changed = projected.changed;
            auto state = world.snapshot();
            bool flying = (movement.controls & 0x2000u) != 0;
            if (flying != state.flying) {
                world.set_flying(flying);
                changed = true;
            }
            bool stop = (movement.controls & 0x4000u) != 0;
            if (stop != stop_active) {
                stop_active = stop;
                changed = true;
            }
            auto relative = projected.relative;
            if (!flying && state.colliding && relative.z < 0)
                relative.z = 0;
            if (changed) {
                auto phase = animator.state();
                if (stop_active)
                    apply_target(relative, .2f * 4.096f,
                                 movement_flags == 0 && phase == MotionControlState::falling);
                else if (phase != MotionControlState::flying && phase != MotionControlState::onsurface)
                    apply_target(relative, projected.speed);
                else if (movement_flags != 0)
                    apply_target(relative, projected.speed);
                else
                    movement.delayed_stop = now + 250.;
            } else if (movement.controls & 0x8000u)
                animate();
        }
    };
    std::map<Uuid, std::unique_ptr<Avatar>> avatars;
    bool has_dynamic_boxes() const {
        return std::ranges::any_of(boxes, [](const auto& entry) { return entry.second.dynamic; });
    }
    void command(const Command& command, double now) {
        const auto& a = command.agent;
        const auto id = a.circuit->agent_id;
        const auto existing = avatars.find(id);
        if (command.kind == Kind::Enter) {
            const auto current = viewers.agent(id);
            if (!current || !current->root || current->circuit->session_id != a.circuit->session_id)
                return;
            if (existing != avatars.end() && existing->second->owner_session == a.circuit->session_id)
                return;
            if (avatars.empty() && !has_dynamic_boxes()) {
                accumulated = 0;
                last_clock = now;
            }
            auto avatar = std::make_unique<Avatar>(*this, a, now);
            avatar->script_animations = command.animations;
            avatar->animation_overrides = command.overrides;
            if (command.movement) {
                const auto& movement = *command.movement;
                // A falling animator can return its current name before its next transition.
                // Restore that name along with the phase so it cannot fall back to STAND.
                std::string movement_name;
                const auto implicit = movement.implicit ? movement.implicit : movement.current;
                if (implicit) {
                    auto movement_id = implicit->animation;
                    for (const auto& [original, replacement] : command.overrides) {
                        if (replacement == movement_id) {
                            movement_id = original;
                            break;
                        }
                    }
                    if (const auto name = catalog->by_id.find(movement_id); name != catalog->by_id.end())
                        movement_name = name->second;
                }
                avatar->animator.restore_state(movement.phase, std::move(movement_name));
                avatar->implicit_animation = movement.implicit;
                if (movement.current) {
                    avatar->default_override = movement.current->animation;
                    avatar->restored_movement = true;
                    avatar->animation_sequence = movement.current->sequence;
                    if (movement.current->sequence > 0)
                        avatar->sequence_counter = std::max(avatar->sequence_counter,
                            static_cast<std::uint32_t>(movement.current->sequence));
                }
            }
            avatar->snapshot_animation();
            for (auto& other : viewers.agents())
                if (other.root)
                    avatar->send_animation(other.circuit->agent_id);
            for (const auto& [owner, other] : avatars)
                if (owner != id)
                    other->send_animation(id);
            if (existing != avatars.end()) {
                std::lock_guard lock(animation_mutex);
                animation_snapshots.erase({id, existing->second->owner_session});
            }
            avatars.insert_or_assign(id, std::move(avatar));
            return;
        }
        if (existing == avatars.end() || a.circuit->session_id != existing->second->owner_session)
            return;
        auto& avatar = *existing->second;
        if (command.kind == Kind::Teleport) {
            avatar.world.remove_avatar();
            try {
                avatar.reset_body(a);
            } catch (...) {
                {
                    std::lock_guard lock(animation_mutex);
                    animation_snapshots.erase({id, a.circuit->session_id});
                }
                avatars.erase(existing);
                throw;
            }
            if (avatars.size() == 1) {
                accumulated = 0;
                last_clock = now;
            }
            return;
        }
        if (command.kind == Kind::Leave) {
            {
                std::lock_guard lock(animation_mutex);
                animation_snapshots.erase({id, a.circuit->session_id});
            }
            avatars.erase(existing);
            return;
        }
        if (command.kind == Kind::Appearance) {
            if (const auto appearance = a.appearance())
                avatar.world.resize_avatar(appearance->avatar_box_size());
            return;
        }
        if (command.kind == Kind::Running) {
            avatar.always_run = a.always_run;
            avatar.world.set_running(a.always_run);
            return;
        }
        if (command.kind == Kind::Buoyancy) {
            avatar.world.set_buoyancy(command.buoyancy);
            return;
        }
        if (command.kind == Kind::Push) {
            avatar.world.push_avatar(command.impulse);
            return;
        }
        if (command.kind == Kind::Hover) {
            const auto hover = command.hover;
            const auto reference = hover.above_water ? physics::HoverReference::GroundAndWater
                                                    : physics::HoverReference::Ground;
            avatar.world.set_hover(hover.height, hover.tau, reference, hover.active());
            return;
        }
        if (command.kind == Kind::MoveToTarget) {
            avatar.start_move_to_target(a, command.move_target);
            return;
        }
        avatar.update_input(a, now);
    }
    void remove_box(std::map<Uuid, PhysicalPrim>::iterator entry) {
        world.remove_box(entry->first);
        boxes.erase(entry);
    }
    std::shared_ptr<const physics::AssetPhysicsMesh> resolve_mesh(Uuid asset, bool convex) {
        if (asset.is_zero() || !load_mesh)
            return {};
        const MeshKey key{asset, convex};
        auto entry = mesh_requests.find(key);
        if (entry == mesh_requests.end()) {
            auto request = std::make_shared<MeshRequest>();
            entry = mesh_requests.emplace(key, request).first;
            try {
                load_mesh(asset, convex, [weak = std::weak_ptr(request)](auto mesh) {
                    if (const auto pending = weak.lock()) {
                        std::lock_guard lock(pending->mutex);
                        pending->mesh = std::move(mesh);
                    }
                });
            } catch (const std::exception& failure) {
                mesh_requests.erase(entry);
                if (log)
                    log(failure.what());
                return {};
            }
        }
        std::lock_guard lock(entry->second->mutex);
        return entry->second->mesh;
    }
    static float part_mass(Vector3 scale, float density, PrimType type, float hollow,
                           const LinearPrimPath& path) {
        if (!std::isfinite(density))
            throw std::invalid_argument("Invalid box density");
        double shape_factor = 1.;
        if (type == PrimType::Sphere)
            shape_factor = .5236;
        else if (type == PrimType::Cylinder)
            shape_factor = .78539816339;
        else if (type == PrimType::Prism)
            shape_factor = .32475953;
        else if (type == PrimType::Torus || type == PrimType::Tube || type == PrimType::Ring) {
            const double profile_factor = type == PrimType::Torus ? .6168502750680849 :
                                          type == PrimType::Tube ? .78539816339 : .32475953;
            const double inner = 1. - 2. * path.hole_y;
            shape_factor = profile_factor * path.hole_x * (1. - inner * inner);
        }
        double hollow_factor = 1.;
        if (type == PrimType::Prism || type == PrimType::Ring) {
            hollow_factor = .25;
            if (path.hollow_segments == 4)
                hollow_factor = .499849 * 3.07920140172638;
            else if (path.hollow_segments == 24)
                hollow_factor = .1963495 * 3.07920140172638;
        } else if (type == PrimType::Cylinder || type == PrimType::Torus) {
            if (path.hollow_segments == 4)
                hollow_factor = .5 * 2.5984480504799;
            else if (path.hollow_segments == 3)
                hollow_factor = .5 * 1.27323954473516;
        } else if (path.hollow_segments == 24) {
            hollow_factor = .78539816339;
        } else if (path.hollow_segments == 3) {
            hollow_factor = .25;
        }
        const double hollow_area = static_cast<double>(hollow) * hollow;
        const bool swept = type == PrimType::Torus || type == PrimType::Tube || type == PrimType::Ring;
        shape_factor *= 1. - hollow_area * (swept ? hollow : 1.f) * hollow_factor;
        const double taper_x = std::abs(path.taper_x);
        const double taper_y = std::abs(path.taper_y);
        const double base_x = 1. - taper_x;
        const double base_y = 1. - taper_y;
        const double taper_factor = base_x * base_y + .5 * (base_x * taper_y + taper_x * base_y) +
                                    taper_x * taper_y / 3.;
        const double volume = static_cast<double>(scale.x) * scale.y * scale.z * shape_factor *
                              taper_factor * (path.end - path.begin) * (path.profile_end - path.profile_begin);
        return static_cast<float>(std::clamp(volume * density / 100., .0001, 10000.01));
    }

    static physics::UbodePrimForces prim_forces(const PhysicalPrim& box) {
        return {box.gravity_modifier, box.buoyancy, box.force, box.torque, box.hover, box.move_target,
                box.look_at, box.inertia_override, box.rotation_axis_locks};
    }

    bool add_dynamic_prim(const PhysicalPrim& box) {
        const physics::UbodeBodyState state{box.position, box.rotation, box.velocity, box.angular_velocity};
        std::vector<physics::UbodePrimPart> parts;
        parts.reserve(box.children.size() + 1);
        auto mesh = resolve_mesh(box.mesh_asset, box.convex_mesh);
        bool ready = box.mesh_asset.is_zero() || mesh != nullptr;
        parts.push_back({box.scale, {}, {0, 0, 0, 1},
                         part_mass(box.scale, box.density, box.type, box.hollow, box.path),
                         box.type, box.mesh_segments, std::min(box.hollow, .95f), box.path,
                         std::move(mesh), box.contact_material, box.id, box.volume_detect, box.phantom});
        for (const auto& child : box.children) {
            auto child_mesh = resolve_mesh(child.mesh_asset, child.convex_mesh);
            ready = (child.mesh_asset.is_zero() || child_mesh != nullptr) && ready;
            parts.push_back({child.scale, child.offset, child.rotation,
                             part_mass(child.scale, child.density, child.type, child.hollow, child.path),
                             child.type, child.mesh_segments, std::min(child.hollow, .95f), child.path,
                             std::move(child_mesh), child.contact_material, child.id, child.volume_detect, child.phantom});
        }
        if (!ready)
            return false;
        world.add_dynamic_linkset(box.id, parts, state, prim_forces(box));
        return true;
    }
    bool update_box(const PhysicalPrim& previous, const PhysicalPrim& current) {
        if (previous.children.size() != current.children.size())
            return false;

        auto without_properties = current;
        const auto restore_properties = [](auto& updated, const auto& original) {
            const bool changed = updated.contact_material != original.contact_material ||
                                 updated.volume_detect != original.volume_detect || updated.phantom != original.phantom;
            // ubODE reads the new density when rebuilding the prim's geometry.
            updated.density = original.density;
            updated.contact_material = original.contact_material;
            updated.volume_detect = original.volume_detect;
            updated.phantom = original.phantom;
            return changed;
        };
        bool colliders_changed = restore_properties(without_properties, previous);
        for (std::size_t i = 0; i < current.children.size(); ++i)
            colliders_changed |= restore_properties(without_properties.children[i], previous.children[i]);

        if (without_properties != previous && !update_box_motion(previous, current, without_properties))
            return false;
        if (!colliders_changed)
            return true;

        // The colliders are created in root-first order, followed by the linkset children.
        std::vector<physics::UbodeColliderProperties> properties;
        properties.reserve(1 + current.children.size());
        properties.push_back({current.contact_material, current.volume_detect, current.phantom});
        for (const auto& child : current.children)
            properties.push_back({child.contact_material, child.volume_detect, child.phantom});
        return world.update_collider_properties(current.id, properties);
    }

    bool update_box_motion(const PhysicalPrim& previous, const PhysicalPrim& current, PhysicalPrim& without_motion) {
        if (!previous.dynamic || !current.dynamic)
            return false;
        without_motion.position = previous.position;
        without_motion.rotation = previous.rotation;
        without_motion.velocity = previous.velocity;
        without_motion.angular_velocity = previous.angular_velocity;
        without_motion.acceleration = previous.acceleration;
        without_motion.gravity_modifier = previous.gravity_modifier;
        without_motion.buoyancy = previous.buoyancy;
        without_motion.force = previous.force;
        without_motion.torque = previous.torque;
        without_motion.hover = previous.hover;
        without_motion.move_target = previous.move_target;
        without_motion.look_at = previous.look_at;
        without_motion.rotation_axis_locks = previous.rotation_axis_locks;
        if (without_motion != previous)
            return false;

        // Keep the body in place when updating motion, forces, or rotation locks.
        const auto position = current.position != previous.position ? std::optional{current.position} : std::nullopt;
        const auto linear = current.velocity != previous.velocity ? std::optional{current.velocity} : std::nullopt;
        const auto angular = current.angular_velocity != previous.angular_velocity
            ? std::optional{current.angular_velocity} : std::nullopt;
        const auto next_forces = prim_forces(current);
        auto previous_forces = prim_forces(previous);
        previous_forces.rotation_axis_locks = next_forces.rotation_axis_locks;
        const auto forces = next_forces != previous_forces ? std::optional{next_forces} : std::nullopt;
        const auto rotation = current.rotation != previous.rotation ? std::optional{current.rotation} : std::nullopt;
        const auto rotation_locks = current.rotation_axis_locks != previous.rotation_axis_locks
            ? std::optional{current.rotation_axis_locks} : std::nullopt;
        return world.update_body_motion(current.id, position, linear, angular, forces, rotation_locks, rotation);
    }

    void sync_boxes() {
        auto candidates = scene_objects.collision_prims();
        if (!mesh_requests.empty()) {
            std::set<MeshKey> assets;
            for (const auto& box : candidates) {
                if (!box.mesh_asset.is_zero())
                    assets.emplace(box.mesh_asset, box.convex_mesh);
                for (const auto& child : box.children)
                    if (!child.mesh_asset.is_zero())
                        assets.emplace(child.mesh_asset, child.convex_mesh);
            }
            std::erase_if(mesh_requests, [&](const auto& entry) { return !assets.contains(entry.first); });
        }
        for (auto& box : candidates) {
            std::erase_if(box.children, [&](const auto& part) {
                return !part.mesh_asset.is_zero() && !resolve_mesh(part.mesh_asset, part.convex_mesh);
            });
        }
        for (auto entry = boxes.begin(); entry != boxes.end();) {
            const auto current = std::lower_bound(candidates.begin(), candidates.end(), entry->first,
                [](const PhysicalPrim& box, Uuid id) { return box.id < id; });
            if (current == candidates.end() || current->id != entry->first) {
                remove_box(entry++);
                continue;
            }
            if (*current != entry->second) {
                if (!update_box(entry->second, *current)) {
                    remove_box(entry++);
                    continue;
                }
                entry->second = *current;
            }
            ++entry;
        }
        for (const auto& box : candidates) {
            if (boxes.contains(box.id))
                continue;
            try {
                const auto entry = boxes.emplace(box.id, box).first;
                try {
                    if (box.dynamic) {
                        if (!add_dynamic_prim(box))
                            boxes.erase(entry);
                    } else {
                        auto mesh = resolve_mesh(box.mesh_asset, box.convex_mesh);
                        if (!box.mesh_asset.is_zero() && !mesh) {
                            boxes.erase(entry);
                            continue;
                        }
                        world.add_static_prim(box.id, box.type, box.scale, box.position, box.rotation,
                                              box.mesh_segments, std::min(box.hollow, .95f), box.path,
                                              std::move(mesh), box.contact_material, box.volume_detect,
                                              part_mass(box.scale, box.density, box.type, box.hollow, box.path));
                    }
                } catch (...) {
                    boxes.erase(entry);
                    throw;
                }
            } catch (const std::exception& failure) {
                if (log)
                    log(failure.what());
            }
        }
    }
    void publish_boxes(std::size_t steps) {
        if (!steps)
            return;
        const float elapsed = static_cast<float>(steps) * physics::UbodeWorld::time_step;
        for (auto entry = boxes.begin(); entry != boxes.end();) {
            const auto current = entry++;
            const auto& previous = current->second;
            if (!previous.dynamic)
                continue;
            const auto state = world.body_state(current->first);
            auto motion = previous;
            motion.position = state.position;
            motion.rotation = state.rotation;
            motion.velocity = state.velocity;
            motion.angular_velocity = state.angular_velocity;
            motion.acceleration = {(motion.velocity.x - previous.velocity.x) / elapsed,
                                   (motion.velocity.y - previous.velocity.y) / elapsed,
                                   (motion.velocity.z - previous.velocity.z) / elapsed};
            const bool outside = motion.position.x < 0 || motion.position.x >= terrain.width() ||
                                 motion.position.y < 0 || motion.position.y >= terrain.height();
            if (outside) {
                const bool crossing_started = cross_object && cross_object(motion.id, motion.position);
                if (crossing_started) {
                    // Keep the last local pose available for a failed transfer.
                    motion.position = previous.position;
                    motion.rotation = previous.rotation;
                } else {
                    motion.position.x = std::clamp(motion.position.x, .5f,
                                                   static_cast<float>(terrain.width()) - .5f);
                    motion.position.y = std::clamp(motion.position.y, .5f,
                                                   static_cast<float>(terrain.height()) - .5f);
                }
                motion.velocity = {};
                motion.angular_velocity = {};
                motion.acceleration = {};
                scene_objects.apply_physical_prim_motion(previous, motion);
                remove_box(current);
                continue;
            }
            if (motion == previous)
                continue;
            if (scene_objects.apply_physical_prim_motion(previous, motion))
                current->second = motion;
            else
                remove_box(current);
        }
    }
    PollResult poll(bool simulate, bool update_object_groups) {
        const auto revision = terrain.revision();
        if (revision != terrain_revision) {
            try {
                world.update_terrain(terrain);
            } catch (const std::exception& error) {
                if (log)
                    log(error.what());
            }
            terrain_revision = revision;
        }
        const double now = milliseconds();
        std::vector<Command> commands;
        std::optional<float> water_height;
        {
            std::lock_guard lock(queue_mutex);
            commands.swap(queue);
            water_height.swap(pending_water_height);
        }
        if (water_height)
            world.set_water_height(*water_height);
        PollResult result;
        const auto commands_started = milliseconds();
        for (auto& entry : commands)
            try {
                command(entry, now);
            } catch (const std::exception& e) {
                if (log)
                    log(e.what());
            }
        if (!commands.empty())
            result.agent_ms = milliseconds() - commands_started;
        if (simulate)
            sync_boxes();
        if (!simulate || (avatars.empty() && !has_dynamic_boxes())) {
            if (simulate)
                result.prim_collision_reports.emplace_back();
            if (simulate) {
                std::lock_guard lock(queue_mutex);
                prim_impulses.clear();
            }
            last_clock = now;
            accumulated = 0;
            return result;
        }
        accumulated += static_cast<float>((now - last_clock) * .001);
        last_clock = now;
        // Source solver uses a20ms quantum,10ms half-step gate,1.2-frame work
        // budget and400ms lag drop. Other scene scheduling/mesh work is not modeled.
        const double deadline = now + timing.frame_time * 1200.;
        std::vector<Avatar*> ordered;
        for (auto& [owner, avatar] : avatars)
            ordered.push_back(avatar.get());
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto* a, const auto* b) { return a->local_id < b->local_id; });
        auto& first = ordered.empty() ? world : ordered.front()->world;
        std::vector<physics::UbodeWorld*> peers;
        for (std::size_t i = 1; i < ordered.size(); ++i)
            peers.push_back(&ordered[i]->world);
        while (accumulated > .010f) {
            const auto started = milliseconds();
            if (!result.steps) {
                for (auto* avatar : ordered)
                    avatar->advance_move_to_target();
                std::vector<PrimImpulse> impulses;
                {
                    std::lock_guard lock(queue_mutex);
                    impulses.swap(prim_impulses);
                }
                for (const auto& impulse : impulses)
                    world.apply_prim_impulse(impulse.id, impulse.impulse, impulse.angular);
            }
            first.step_with(peers);
            const auto finished = milliseconds();
            result.solver_ms += finished - started;
            ++result.steps;
            accumulated -= physics::UbodeWorld::time_step;
            if (finished > deadline)
                break;
        }
        if (accumulated > .4f) {
            accumulated = 0;
            last_clock = milliseconds();
        }
        publish_boxes(result.steps);
        result.prim_collision_reports = world.take_prim_collision_reports();
        if (update_object_groups)
            world.update_prim_look_at();
        for (auto& [owner, avatar] : avatars) {
            if (avatar->movement.delayed_stop > 0 && now > avatar->movement.delayed_stop)
                avatar->apply_target({}, 4.096f);
            // Source RegionHeartbeatEnd reevaluates the animator every second heartbeat.
            if (now - avatar->last_animation >= timing.frame_time * 2000.f) {
                avatar->animate();
                avatar->last_animation = now;
            }
            if (now - avatar->last_publish >= timing.frame_time * 1000.f) {
                const auto state = avatar->world.snapshot();
                auto a = viewers.update_physics(owner, avatar->owner_session, state.position, state.velocity,
                                                state.acceleration, state.flying, avatar->pending_rotation,
                                                state.collision_plane, state.angular_velocity);
                avatar->pending_rotation.reset();
                if (a && publish)
                    publish(*a);
                avatar->last_publish = now;
            }
        }
        return result;
    }
};
ViewerPhysics::ViewerPhysics(World& world, ViewerRegion& viewers, const std::filesystem::path& library,
                             std::shared_ptr<const DefaultAnimations> catalog, Publish publish, Log log,
                             float water_height, AvatarVisibility visibility, ObjectCrossing crossing,
                             MeshLoader loader, ScenePhysicsTiming timing)
    : impl_(std::make_unique<Impl>(world, viewers, library, std::move(catalog), std::move(publish),
                                   std::move(log), water_height, std::move(visibility),
                                   std::move(crossing), std::move(loader), timing)) {}
ViewerPhysics::~ViewerPhysics() = default;
void ViewerPhysics::entered(const ViewerAgent& a, std::vector<AvatarAnimationEntry> animations,
                            std::map<Uuid, Uuid> overrides, std::optional<MovementAnimation> movement) {
    impl_->post(Impl::Kind::Enter, a, std::move(animations), std::move(overrides), std::move(movement));
}
std::optional<ViewerPhysics::MovementAnimation> ViewerPhysics::movement_animation(Uuid agent, Uuid session) const {
    std::lock_guard lock(impl_->animation_mutex);
    const auto found = impl_->animation_snapshots.find({agent, session});
    if (found == impl_->animation_snapshots.end())
        return std::nullopt;
    return found->second;
}
void ViewerPhysics::teleported(const ViewerAgent& a) {
    impl_->post(Impl::Kind::Teleport, a);
}
void ViewerPhysics::input(const ViewerAgent& a) {
    impl_->post(Impl::Kind::Input, a);
}
void ViewerPhysics::appearance_changed(const ViewerAgent& a) {
    impl_->post(Impl::Kind::Appearance, a);
}
void ViewerPhysics::running(const ViewerAgent& a) {
    impl_->post(Impl::Kind::Running, a);
}
void ViewerPhysics::left(const ViewerAgent& a) {
    impl_->post(Impl::Kind::Leave, a);
}
ViewerPhysics::PollResult ViewerPhysics::poll(bool simulate, bool update_object_groups) {
    return impl_->poll(simulate, update_object_groups);
}
std::vector<std::pair<Uuid, float>> ViewerPhysics::top_colliders() const {
    return impl_->world.top_colliders();
}
std::optional<framework::PrimInertiaData> ViewerPhysics::inertia_data(
    Uuid root, const std::optional<framework::PrimInertiaOverride>& requested) const {
    return impl_->world.inertia_data(root, requested);
}
std::optional<physics::UbodeMassProperties> ViewerPhysics::mass_properties(Uuid root, Uuid part) const {
    const auto entry = impl_->boxes.find(root);
    if (entry != impl_->boxes.end() && entry->second.dynamic)
        return impl_->world.mass_properties(root, part);
    if (!part.is_zero())
        return impl_->world.mass_properties(part);

    physics::UbodeMassProperties result;
    for (const auto& [id, box] : impl_->boxes) {
        if (box.root_id != root)
            continue;
        if (const auto properties = impl_->world.mass_properties(id)) {
            result.mass += properties->mass;
            result.center.x += properties->center.x * properties->mass;
            result.center.y += properties->center.y * properties->mass;
            result.center.z += properties->center.z * properties->mass;
        }
    }
    if (result.mass > 0.f) {
        const float inverse_mass = 1.f / result.mass;
        result.center.x *= inverse_mass;
        result.center.y *= inverse_mass;
        result.center.z *= inverse_mass;
    }
    return result;
}
void ViewerPhysics::set_buoyancy(const ViewerAgent& agent, float buoyancy) {
    std::lock_guard lock(impl_->queue_mutex);
    impl_->queue.push_back({Impl::Kind::Buoyancy, agent, {}, {}, {}, buoyancy});
}
void ViewerPhysics::set_hover(const ViewerAgent& agent, framework::PrimHover hover) {
    std::lock_guard lock(impl_->queue_mutex);
    impl_->queue.push_back({Impl::Kind::Hover, agent, {}, {}, {}, 0.f, {}, hover});
}
void ViewerPhysics::move_to_target(const ViewerAgent& agent, PrimMoveTarget target) {
    std::lock_guard lock(impl_->queue_mutex);
    impl_->queue.push_back({Impl::Kind::MoveToTarget, agent, {}, {}, {}, 0.f, {}, {}, target});
}
void ViewerPhysics::push_avatar(const ViewerAgent& agent, Vector3 impulse) {
    std::lock_guard lock(impl_->queue_mutex);
    impl_->queue.push_back({Impl::Kind::Push, agent, {}, {}, {}, 0.f, impulse});
}
void ViewerPhysics::apply_prim_impulse(Uuid id, Vector3 impulse, bool angular) {
    std::lock_guard lock(impl_->queue_mutex);
    impl_->prim_impulses.push_back({id, impulse, angular});
}
void ViewerPhysics::set_water_height(float height) {
    std::lock_guard lock(impl_->queue_mutex);
    impl_->pending_water_height = height;
}
bool ViewerPhysics::avatar_colliding(Uuid agent, Uuid session) const {
    const auto avatar = impl_->avatars.find(agent);
    return avatar != impl_->avatars.end() && avatar->second->owner_session == session &&
           avatar->second->world.snapshot().colliding;
}
float ViewerPhysics::avatar_mass(Uuid agent, Uuid session) const {
    const auto avatar = impl_->avatars.find(agent);
    if (avatar == impl_->avatars.end() || avatar->second->owner_session != session)
        return 0.f;
    return avatar->second->world.avatar_mass();
}
Uuid ViewerPhysics::movement_animation(Uuid agent) const {
    const auto avatar = impl_->avatars.find(agent);
    return avatar == impl_->avatars.end() ? Uuid{} : avatar->second->animation;
}
Uuid ViewerPhysics::default_animation(Uuid agent) const {
    const auto avatar = impl_->avatars.find(agent);
    return avatar == impl_->avatars.end() ? Uuid{} : avatar->second->effective_animation();
}
void ViewerPhysics::set_animation_overrides(Uuid agent, const std::map<Uuid, Uuid>& overrides) {
    const auto avatar = impl_->avatars.find(agent);
    if (avatar == impl_->avatars.end())
        return;
    const auto before = avatar->second->effective_animation();
    avatar->second->animation_overrides = overrides;
    avatar->second->default_override.reset();
    avatar->second->implicit_animation.reset();
    avatar->second->restored_movement = false;
    if (before != avatar->second->effective_animation())
        avatar->second->advance_animation_sequence();
    avatar->second->snapshot_animation();
}
void ViewerPhysics::stop_default_animation(Uuid agent, bool allow_empty) {
    const auto avatar = impl_->avatars.find(agent);
    if (avatar != impl_->avatars.end()) {
        const auto previous = AvatarAnimationEntry{avatar->second->effective_animation(), {},
                                                  avatar->second->animation_sequence};
        if (!avatar->second->implicit_animation)
            avatar->second->implicit_animation = previous;
        avatar->second->default_override = allow_empty ? Uuid{} : impl_->catalog->resolve("STAND");
        avatar->second->animation_sequence = 1;
        avatar->second->snapshot_animation();
    }
}
bool ViewerPhysics::set_script_animations(Uuid agent, std::vector<AvatarAnimationEntry> entries) {
    const auto avatar = impl_->avatars.find(agent);
    if (avatar == impl_->avatars.end())
        return false;
    avatar->second->script_animations = std::move(entries);
    for (const auto& viewer : impl_->viewers.agents())
        if (viewer.root)
            avatar->second->send_animation(viewer.circuit->agent_id);
    return true;
}
bool ViewerPhysics::broadcast_animations(Uuid agent, std::span<const ViewerAgent> recipients) {
    const auto avatar = impl_->avatars.find(agent);
    if (avatar == impl_->avatars.end())
        return false;
    for (const auto& viewer : recipients)
        avatar->second->send_animation(viewer);
    return true;
}
}
