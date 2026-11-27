// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/physics/mesh_asset.hpp"
#include "opensim/physics/ubode.hpp"
#include "opensim/scene/viewer_region.hpp"
#include "opensim/scene/default_animations.hpp"
#include "opensim/scene/avatar_packets.hpp"
#include "opensim/scene/movement_animation.hpp"
#include <span>
#include <set>

namespace opensim::scene {
struct ScenePhysicsTiming {
    float frame_time = .0909f;
};

// Host-thread composition of source controls, animator and actual ubODE.
// Packet callbacks enqueue commands; no native engine calls run on UDP threads.
class ViewerPhysics {
  public:
    struct MovementAnimation {
        std::optional<AvatarAnimationEntry> current;
        std::optional<AvatarAnimationEntry> implicit;
        MotionControlState phase = MotionControlState::onsurface;
    };

    using Publish = std::function<void(const ViewerAgent &)>;
    using Log = std::function<void(const std::string &)>;
    using AvatarVisibility = std::function<bool(const ViewerAgent &, const ViewerAgent &)>;
    using ObjectCrossing = std::function<bool(framework::Uuid, framework::Vector3)>;
    using MeshReady = std::function<void(std::shared_ptr<const physics::AssetPhysicsMesh>)>;
    using MeshLoader = std::function<void(framework::Uuid, bool convex, MeshReady)>;
    ViewerPhysics(World &, ViewerRegion &, const std::filesystem::path &library,
                  std::shared_ptr<const DefaultAnimations>, Publish, Log = {}, float water_height = 20.f,
                  AvatarVisibility = {}, ObjectCrossing = {}, MeshLoader = {}, ScenePhysicsTiming = {},
                  bool collidable_prims = true);
    ~ViewerPhysics();
    std::string engine_name() const;
    void entered(const ViewerAgent &, std::vector<AvatarAnimationEntry> animations = {},
                 std::map<framework::Uuid, framework::Uuid> overrides = {},
                 std::optional<MovementAnimation> movement = {});
    std::optional<MovementAnimation> movement_animation(framework::Uuid agent, framework::Uuid session) const;
    void teleported(const ViewerAgent &);
    void teleport_refused(const ViewerAgent &);
    void script_teleported(const ViewerAgent &, bool apply_momentum);
    void input(const ViewerAgent &);
    void appearance_changed(const ViewerAgent &);
    void npc_rotation_changed(const ViewerAgent &);
    std::optional<std::pair<std::uint32_t, bool>> npc_movement_flags(const ViewerAgent &) const;
    void running(const ViewerAgent &);
    void left(const ViewerAgent &);
    void set_water_height(float height);
    void set_buoyancy(const ViewerAgent &, float buoyancy);
    void set_hover(const ViewerAgent &, framework::PrimHover hover);
    void move_to_target(const ViewerAgent &, framework::PrimMoveTarget target, bool use_default_speed = false);
    void move_npc_to_target(const ViewerAgent &, std::optional<framework::Vector3> target, std::int32_t options);
    void push_avatar(const ViewerAgent &, framework::Vector3 impulse);
    void apply_prim_impulse(framework::Uuid id, framework::Vector3 impulse, bool angular);
    void apply_viewer_spin(framework::Uuid id, framework::Vector3 axis);

    struct PollResult {
        std::size_t steps = 0;
        std::size_t physical_parts = 0;
        double solver_ms = 0.;
        double agent_ms = 0.;
        std::vector<physics::PrimCollisionReport> prim_collision_reports;
    };

    PollResult poll(bool simulate = true, bool update_object_groups = true);
    std::vector<std::pair<framework::Uuid, float>> top_colliders() const;
    std::optional<framework::PrimInertiaData>
    inertia_data(framework::Uuid root, const std::optional<framework::PrimInertiaOverride> &requested = {}) const;
    std::optional<physics::UbodeMassProperties> mass_properties(framework::Uuid root, framework::Uuid part = {}) const;
    std::optional<float> prim_physics_cost(framework::Uuid root, framework::Uuid part) const;
    // Scene-owner-thread query of an existing root actor, including creation failure.
    bool prim_is_physical(const SceneObject &expected) const;
    // Owner-thread notification after dynamic body creation, before stepping.
    // The callback may stop scene motion; those changes are synchronized before stepping.
    void set_physical_root_created(std::function<void(const SceneObject &)> callback);
    bool set_script_animations(framework::Uuid, std::vector<AvatarAnimationEntry>);
    bool broadcast_animations(framework::Uuid, std::span<const ViewerAgent>);
    framework::Uuid movement_animation(framework::Uuid) const;
    float avatar_mass(framework::Uuid agent, framework::Uuid session) const;
    bool avatar_colliding(framework::Uuid agent, framework::Uuid session) const;
    framework::Uuid default_animation(framework::Uuid) const;
    void stop_default_animation(framework::Uuid, bool allow_empty);
    void set_animation_overrides(framework::Uuid, const std::map<framework::Uuid, framework::Uuid> &);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace opensim::scene
