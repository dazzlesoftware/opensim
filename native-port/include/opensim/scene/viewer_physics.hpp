// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/viewer_region.hpp"
#include "opensim/scene/default_animations.hpp"
#include "opensim/scene/avatar_packets.hpp"
#include <span>
namespace opensim::scene {
// Host-thread composition of source controls, animator and actual ubODE.
// Packet callbacks enqueue commands; no native engine calls run on UDP threads.
class ViewerPhysics {
  public:
    using Publish = std::function<void(const ViewerAgent&)>;
    using Log = std::function<void(const std::string&)>;
    using AvatarVisibility = std::function<bool(const ViewerAgent&, const ViewerAgent&)>;
    ViewerPhysics(World&, ViewerRegion&, const std::filesystem::path& library,
                  std::shared_ptr<const DefaultAnimations>, Publish, Log = {}, float water_height = 20.f,
                  AvatarVisibility = {});
    ~ViewerPhysics();
    void entered(const ViewerAgent&);
    void teleported(const ViewerAgent&);
    void input(const ViewerAgent&);
    void appearance_changed(const ViewerAgent&);
    void running(const ViewerAgent&);
    void left(const ViewerAgent&);
    void set_water_height(float height);
    struct PollResult {
        std::size_t steps = 0;
        double solver_ms = 0.;
        double agent_ms = 0.;
    };
    PollResult poll(bool simulate = true);
    bool set_script_animations(framework::Uuid, std::vector<AvatarAnimationEntry>);
    bool broadcast_animations(framework::Uuid, std::span<const ViewerAgent>);
    framework::Uuid movement_animation(framework::Uuid) const;
    framework::Uuid default_animation(framework::Uuid) const;
    void stop_default_animation(framework::Uuid, bool allow_empty);
    void set_animation_overrides(framework::Uuid, const std::map<framework::Uuid, framework::Uuid>&);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
