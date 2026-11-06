// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace opensim::scene {
enum class MotionControlState : std::uint8_t {
    sitted,
    flying,
    falling,
    prejumping,
    jumping,
    landing,
    onsurface
};
struct MovementAnimationContext {
    bool in_transit = false, sit_ground = false;
    std::uint32_t parent_id = 0, controls = 0;
    framework::Uuid parent_uuid;
    bool stop_active = false, always_run = false, actor_present = true, flying = false, colliding = false,
         pid_hover_active = false, use_fly_slow = false;
    framework::Vector3 velocity;
};
struct MovementAnimationEffects {
    std::function<void(float)> jump;
    std::function<void(bool)> set_flying, set_selected;
    // Source TrySetMovementAnimation is a host adapter: animation UUIDs, sets,
    // overrides, child checks and viewer packets remain outside this pure core.
    std::function<bool(const std::string&)> try_set_animation;
};
enum class MovementAnimationActionKind { Jump, SetFlying, SetSelected };
struct MovementAnimationAction {
    MovementAnimationActionKind kind;
    float value;
};
struct MovementAnimationResult {
    std::string animation;
    bool name_changed = false, apply_attempted = false, applied = false;
    // Effects are recorded even when callbacks are absent; no physics is faked.
    std::vector<MovementAnimationAction> actions;
};
struct MovementAnimationSnapshot {
    std::string animation = "STAND";
    MotionControlState state = MotionControlState::onsurface;
    bool is_jumping = false, falling = false;
    std::int32_t tick_fall = 0, tick_land = 0, tick_jump = 0;
    float last_fall_velocity = 0;
};
class MovementAnimator {
  public:
    using Clock = std::function<std::int32_t()>;
    explicit MovementAnimator(Clock tick_count = {});
    ~MovementAnimator();
    MovementAnimator(const MovementAnimator&) = delete;
    MovementAnimationResult update(MovementAnimationContext&, const MovementAnimationEffects& effects = {});
    MovementAnimationResult force_update(MovementAnimationContext&,
                                         const MovementAnimationEffects& effects = {});
    MovementAnimationSnapshot snapshot() const;
    MotionControlState state() const;
    void restore_state(MotionControlState, std::string movement_animation = {});

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
