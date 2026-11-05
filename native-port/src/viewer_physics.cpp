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
    enum class Kind { Enter, Input, Running, Leave, Teleport, Appearance };
    struct Command {
        Kind kind;
        ViewerAgent agent;
    };
    ViewerRegion& viewers;
    std::shared_ptr<const DefaultAnimations> catalog;
    Publish publish;
    Log log;
    AvatarVisibility visible;
    physics::UbodeWorld world;
    const Terrain& terrain;
    std::uint64_t terrain_revision = 0;
    std::mutex queue_mutex;
    std::vector<Command> queue;
    std::optional<float> pending_water_height;
    double last_clock = milliseconds();
    float accumulated = 0;
    static constexpr float scene_frame_time = .0909f;
    Impl(World& scene, ViewerRegion& v, const std::filesystem::path& library,
         std::shared_ptr<const DefaultAnimations> c, Publish p, Log l, float water_height,
         AvatarVisibility visibility)
        : viewers(v), catalog(std::move(c)), publish(std::move(p)), log(std::move(l)),
          visible(std::move(visibility)), world(library, scene.terrain(), {scene_frame_time, water_height}),
          terrain(scene.terrain()) {
        if (!catalog)
            throw std::invalid_argument("Physics animation catalog required");
    }
    void post(Kind kind, const ViewerAgent& a) {
        std::lock_guard lock(queue_mutex);
        queue.push_back({kind, a});
    }
    struct Avatar {
        ViewerRegion& viewers;
        const DefaultAnimations& catalog;
        const AvatarVisibility& visible;
        physics::UbodeWorld world;
        Uuid owner, owner_session;
        std::uint32_t local_id;
        MovementState movement;
        MovementAnimator animator{tick_count};
        std::uint32_t movement_flags = 0,
                      sequence_counter = static_cast<std::uint32_t>(tick_count()) & 0x5fffafu;
        std::int32_t animation_sequence = 1;
        Uuid animation;
        bool always_run = false, stop_active = false;
        double last_publish, last_animation;
        Avatar(Impl& scene, const ViewerAgent& a, double now)
            : viewers(scene.viewers), catalog(*scene.catalog), visible(scene.visible), world(scene.world),
              owner(a.circuit->agent_id), owner_session(a.circuit->session_id), local_id(a.local_id),
              animation(catalog.resolve("STAND")), always_run(a.always_run), last_publish(now),
              last_animation(now) {
            reset_body(a);
            movement.rotation = a.rotation;
            movement.controls = a.controls & 0x2000u;
        }
        void reset_body(const ViewerAgent& a) {
            const auto appearance = a.appearance();
            auto size = appearance ? appearance->avatar_box_size() : Vector3{.45f, .6f, 2.1f};
            world.add_avatar(a.position, size, a.flying);
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
                entries.push_back({current, {}, default_override ? 1 : animation_sequence});
            for (const auto& entry : script_animations)
                if (entry.animation != current)
                    entries.push_back(entry);
            viewers.transport().enqueue_if_session(recipient, viewer.circuit->session_id,
                                                   avatar_animations(owner, recipient, entries),
                                                   network::ThrottleOutPacketType::Task);
        }
        std::vector<AvatarAnimationEntry> script_animations;
        std::optional<Uuid> default_override;
        std::map<Uuid, Uuid> animation_overrides;
        Uuid effective_animation() const {
            if (default_override)
                return *default_override;
            const auto found = animation_overrides.find(animation);
            return found == animation_overrides.end() ? animation : found->second;
        }
        bool change_animation(const std::string& name) {
            auto id = catalog.resolve(name);
            if (id.is_zero() || id == animation)
                return false;
            animation = id;
            advance_animation_sequence();
            default_override.reset();
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
            animator.update(context, effects);
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
        void update_input(const ViewerAgent& agent, double now) {
            auto q = agent.rotation;
            if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w))
                return;
            movement.rotation = q;
            world.set_orientation(q);
            auto projected = project_controls(agent.controls, 0, always_run, movement_flags);
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
            if (avatars.empty()) {
                accumulated = 0;
                last_clock = now;
            }
            auto avatar = std::make_unique<Avatar>(*this, a, now);
            for (auto& other : viewers.agents())
                if (other.root)
                    avatar->send_animation(other.circuit->agent_id);
            for (const auto& [owner, other] : avatars)
                if (owner != id)
                    other->send_animation(id);
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
        avatar.update_input(a, now);
    }
    PollResult poll(bool simulate) {
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
        if (avatars.empty() || !simulate) {
            last_clock = now;
            accumulated = 0;
            return result;
        }
        accumulated += static_cast<float>((now - last_clock) * .001);
        last_clock = now;
        // Source solver uses a20ms quantum,10ms half-step gate,1.2-frame work
        // budget and400ms lag drop. Other scene scheduling/mesh work is not modeled.
        const double deadline = now + static_cast<int>(scene_frame_time * 1200.f);
        std::vector<Avatar*> ordered;
        for (auto& [owner, avatar] : avatars)
            ordered.push_back(avatar.get());
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto* a, const auto* b) { return a->local_id < b->local_id; });
        auto& first = ordered.front()->world;
        std::vector<physics::UbodeWorld*> peers;
        for (std::size_t i = 1; i < ordered.size(); ++i)
            peers.push_back(&ordered[i]->world);
        while (accumulated > .010f) {
            const auto started = milliseconds();
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
        for (auto& [owner, avatar] : avatars) {
            if (avatar->movement.delayed_stop > 0 && now > avatar->movement.delayed_stop)
                avatar->apply_target({}, 4.096f);
            // Source RegionHeartbeatEnd reevaluates the animator every second heartbeat.
            if (now - avatar->last_animation >= scene_frame_time * 2000.f) {
                avatar->animate();
                avatar->last_animation = now;
            }
            if (now - avatar->last_publish >= scene_frame_time * 1000.f) {
                const auto state = avatar->world.snapshot();
                auto a = viewers.update_physics(owner, avatar->owner_session, state.position, state.velocity,
                                                state.acceleration, state.flying);
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
                             float water_height, AvatarVisibility visibility)
    : impl_(std::make_unique<Impl>(world, viewers, library, std::move(catalog), std::move(publish),
                                   std::move(log), water_height, std::move(visibility))) {}
ViewerPhysics::~ViewerPhysics() = default;
void ViewerPhysics::entered(const ViewerAgent& a) {
    impl_->post(Impl::Kind::Enter, a);
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
ViewerPhysics::PollResult ViewerPhysics::poll(bool simulate) {
    return impl_->poll(simulate);
}
void ViewerPhysics::set_water_height(float height) {
    std::lock_guard lock(impl_->queue_mutex);
    impl_->pending_water_height = height;
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
    if (before != avatar->second->effective_animation())
        avatar->second->advance_animation_sequence();
}
void ViewerPhysics::stop_default_animation(Uuid agent, bool allow_empty) {
    const auto avatar = impl_->avatars.find(agent);
    if (avatar != impl_->avatars.end())
        avatar->second->default_override = allow_empty ? Uuid{} : impl_->catalog->resolve("STAND");
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
