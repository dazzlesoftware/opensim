// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/physics/mesh_asset.hpp"
#include "opensim/scene/viewer_region.hpp"
#include "opensim/scene/default_animations.hpp"
#include "opensim/scene/avatar_packets.hpp"
#include "opensim/scene/movement_animation.hpp"
#include <span>
namespace opensim::scene {
// Host-thread composition of source controls, animator and actual ubODE.
// Packet callbacks enqueue commands; no native engine calls run on UDP threads.
class ViewerPhysics {
  public:
    struct MovementAnimation {
        std::optional<AvatarAnimationEntry> current;
        std::optional<AvatarAnimationEntry> implicit;
        MotionControlState phase = MotionControlState::onsurface;
    };
    using Publish = std::function<void(const ViewerAgent&)>;
    using Log = std::function<void(const std::string&)>;
    using AvatarVisibility = std::function<bool(const ViewerAgent&, const ViewerAgent&)>;
    using ObjectCrossing = std::function<bool(framework::Uuid, framework::Vector3)>;
    using MeshReady = std::function<void(std::shared_ptr<const physics::AssetPhysicsMesh>)>;
    using MeshLoader = std::function<void(framework::Uuid, bool convex, MeshReady)>;
    ViewerPhysics(World&, ViewerRegion&, const std::filesystem::path& library,
                  std::shared_ptr<const DefaultAnimations>, Publish, Log = {}, float water_height = 20.f,
                  AvatarVisibility = {}, ObjectCrossing = {}, MeshLoader = {});
    ~ViewerPhysics();
    void entered(const ViewerAgent&, std::vector<AvatarAnimationEntry> animations = {},
                 std::map<framework::Uuid, framework::Uuid> overrides = {},
                 std::optional<MovementAnimation> movement = {});
    std::optional<MovementAnimation> movement_animation(framework::Uuid agent, framework::Uuid session) const;
    void teleported(const ViewerAgent&);
    void input(const ViewerAgent&);
    void appearance_changed(const ViewerAgent&);
    void running(const ViewerAgent&);
    void left(const ViewerAgent&);
    void set_water_height(float height);
    void set_buoyancy(const ViewerAgent&, float buoyancy);
    struct PollResult {
        std::size_t steps = 0;
        double solver_ms = 0.;
        double agent_ms = 0.;
    };
    PollResult poll(bool simulate = true);
    bool set_script_animations(framework::Uuid, std::vector<AvatarAnimationEntry>);
    bool broadcast_animations(framework::Uuid, std::span<const ViewerAgent>);
    framework::Uuid movement_animation(framework::Uuid) const;
    float avatar_mass(framework::Uuid agent, framework::Uuid session) const;
    bool avatar_colliding(framework::Uuid agent, framework::Uuid session) const;
    framework::Uuid default_animation(framework::Uuid) const;
    void stop_default_animation(framework::Uuid, bool allow_empty);
    void set_animation_overrides(framework::Uuid, const std::map<framework::Uuid, framework::Uuid>&);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
