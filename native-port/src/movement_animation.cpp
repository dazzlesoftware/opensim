// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/movement_animation.hpp"
#include <bit>
#include <chrono>
namespace opensim::scene {
namespace {
std::int32_t sub(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
struct Flags {
    static constexpr std::uint32_t AGENT_CONTROL_AT_POS = 1, AGENT_CONTROL_AT_NEG = 2,
                                   AGENT_CONTROL_LEFT_POS = 4, AGENT_CONTROL_LEFT_NEG = 8,
                                   AGENT_CONTROL_UP_POS = 16, AGENT_CONTROL_UP_NEG = 32;
    static constexpr std::uint32_t AGENT_CONTROL_NUDGE_AT_POS = 1u << 19,
                                   AGENT_CONTROL_NUDGE_AT_NEG = 1u << 20,
                                   AGENT_CONTROL_NUDGE_LEFT_POS = 1u << 21,
                                   AGENT_CONTROL_NUDGE_LEFT_NEG = 1u << 22,
                                   AGENT_CONTROL_NUDGE_UP_POS = 1u << 23,
                                   AGENT_CONTROL_NUDGE_UP_NEG = 1u << 24;
    static constexpr std::uint32_t AGENT_CONTROL_TURN_LEFT = 0x2000000, AGENT_CONTROL_TURN_RIGHT = 0x4000000,
                                   AGENT_CONTROL_FINISH_ANIM = 0x8000;
};
std::int32_t ticks() {
    return std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count()));
}
}
struct MovementAnimator::Impl {
    Clock clock;
    std::string CurrentMovementAnimation = "STAND";
    MotionControlState currentControlState = MotionControlState::onsurface;
    std::int32_t m_animTickFall = 0, m_animTickLand = 0, m_animTickJump = 0;
    bool isJumping = false, Falling = false;
    float m_lastFallVelocity = 0;
    explicit Impl(Clock value) : clock(std::move(value)) {
    }
    std::int32_t now() const {
        return clock ? clock() : ticks();
    }
    std::string determine(MovementAnimationContext& context, const MovementAnimationEffects& effects,
                          MovementAnimationResult& result) {
        const int FALL_DELAY = 800;
        const int PREJUMP_DELAY = 450;
        const int JUMP_PERIOD = 1050;

        if (context.in_transit)
            return CurrentMovementAnimation;

        if (context.sit_ground) {
            currentControlState = MotionControlState::sitted;
            isJumping = false;
            Falling = false;
            return "SITGROUND";
        }
        if (context.parent_id != 0 || !context.parent_uuid.is_zero()) {
            currentControlState = MotionControlState::sitted;
            isJumping = false;
            Falling = false;
            return "SIT";
        }

        std::uint32_t controlFlags = context.controls;

        constexpr std::uint32_t ANYXYMASK =
            (Flags::AGENT_CONTROL_AT_POS | Flags::AGENT_CONTROL_NUDGE_AT_POS | Flags::AGENT_CONTROL_AT_NEG |
             Flags::AGENT_CONTROL_NUDGE_AT_NEG | Flags::AGENT_CONTROL_LEFT_POS |
             Flags::AGENT_CONTROL_NUDGE_LEFT_POS | Flags::AGENT_CONTROL_LEFT_NEG |
             Flags::AGENT_CONTROL_NUDGE_LEFT_NEG);
        constexpr std::uint32_t ANYXYZMASK =
            (ANYXYMASK | Flags::AGENT_CONTROL_UP_POS | Flags::AGENT_CONTROL_NUDGE_UP_POS |
             Flags::AGENT_CONTROL_UP_NEG | Flags::AGENT_CONTROL_NUDGE_UP_NEG);

        // excluded nudge up so it doesn't trigger jump state
        bool heldUp = ((controlFlags & (Flags::AGENT_CONTROL_UP_POS)) != 0);
        bool heldDown =
            ((controlFlags & (Flags::AGENT_CONTROL_UP_NEG | Flags::AGENT_CONTROL_NUDGE_UP_NEG)) != 0);
        //bool flying = (controlFlags & Flags::AGENT_CONTROL_FLY) == Flags::AGENT_CONTROL_FLY;
        //bool mouselook = (controlFlags & Flags::AGENT_CONTROL_MOUSELOOK) == Flags::AGENT_CONTROL_MOUSELOOK;

        bool heldOnXY = ((controlFlags & ANYXYMASK) != 0);
        bool heldTurnLeft;
        bool heldTurnRight;
        if ((controlFlags & ANYXYZMASK) != 0) {
            heldTurnLeft = false;
            heldTurnRight = false;
        } else {
            heldTurnLeft = (controlFlags & Flags::AGENT_CONTROL_TURN_LEFT) != 0;
            heldTurnRight = (controlFlags & Flags::AGENT_CONTROL_TURN_RIGHT) != 0;
            //heldTurnLeft = (controlFlags & Flags::AGENT_CONTROL_YAW_POS) != 0;
            //heldTurnRight = (controlFlags & Flags::AGENT_CONTROL_YAW_NEG) != 0;
        }

        auto* actor = context.actor_present ? &context : nullptr;
        // no physics actor case
        if (actor == nullptr) {
            isJumping = false;
            Falling = false;

            // well what to do?
            currentControlState = MotionControlState::onsurface;
            if (heldOnXY)
                return "WALK";

            return "STAND";
        }

        bool isColliding = actor->colliding;

        if (actor->flying) {
            m_animTickFall = 0;
            m_animTickJump = 0;
            isJumping = false;
            Falling = false;

            currentControlState = MotionControlState::flying;

            if (heldOnXY) {
                return (context.use_fly_slow ? "FLYSLOW" : "FLY");
            } else if (heldUp) {
                return "HOVER_UP";
            } else if (heldDown) {
                if (isColliding) {
                    context.flying = false;
                    result.actions.push_back({MovementAnimationActionKind::SetFlying, 0});
                    if (effects.set_flying)
                        effects.set_flying(false);
                    currentControlState = MotionControlState::landing;
                    m_animTickLand = now();
                    return "LAND";
                } else
                    return "HOVER_DOWN";
            } else {
                return "HOVER";
            }
        } else {
            if (isColliding && currentControlState == MotionControlState::flying) {
                currentControlState = MotionControlState::landing;
                m_animTickLand = now();
                return "LAND";
            }
        }

        if (!isColliding && currentControlState != MotionControlState::jumping) {

            if (actor->pid_hover_active) {
                m_animTickFall = 0;
                m_animTickJump = 0;
                Falling = false;
                currentControlState = MotionControlState::flying;
                m_lastFallVelocity = 0.0f;
                return "HOVER";
            }

            float fallVelocity = actor->velocity.z;
            if (fallVelocity < -2.5f)
                Falling = true;

            if (m_animTickFall == 0 || (fallVelocity >= -0.5f)) {
                m_animTickFall = now();
            } else {
                int fallElapsed = sub(now(), m_animTickFall);
                if ((fallElapsed > FALL_DELAY) && (fallVelocity < -3.0f)) {
                    currentControlState = MotionControlState::falling;
                    m_lastFallVelocity = fallVelocity;
                    // Falling long enough to trigger the animation
                    return "FALLDOWN";
                }
            }

            // Check if the user has stopped walking just now
            if (CurrentMovementAnimation == "WALK" && !heldOnXY && !heldDown && !heldUp)
                return "STAND";

            return CurrentMovementAnimation;
        }

        m_animTickFall = 0;

        if (isColliding && heldUp && !isJumping && !actor->pid_hover_active) {
            isJumping = true;
            Falling = false;
            if ((controlFlags & (Flags::AGENT_CONTROL_FINISH_ANIM)) == 0) {
                currentControlState = MotionControlState::prejumping;
                m_animTickJump = now();
                // Start jumping, prejump
                return "PREJUMP";
            }

            m_animTickJump = sub(sub(now(), PREJUMP_DELAY), 1);
            currentControlState = MotionControlState::jumping;
            result.actions.push_back({MovementAnimationActionKind::Jump, 9.4f});
            if (effects.jump)
                effects.jump(9.4f);
            return "JUMP";
        }

        if (currentControlState == MotionControlState::prejumping) {
            if ((controlFlags & (Flags::AGENT_CONTROL_FINISH_ANIM)) == 0) {
                int jumptime = sub(now(), m_animTickJump);
                if (jumptime < PREJUMP_DELAY)
                    return CurrentMovementAnimation;
            }

            // jump up
            isJumping = true;
            currentControlState = MotionControlState::jumping;
            result.actions.push_back({MovementAnimationActionKind::Jump, 9.4f});
            if (effects.jump)
                effects.jump(9.4f);
            return "JUMP";
        }

        if (currentControlState == MotionControlState::jumping) {
            int jumptime = sub(now(), m_animTickJump);
            if ((jumptime > (JUMP_PERIOD * 1.5f)) && actor->colliding) {
                // end jumping
                isJumping = false;
                Falling = false;
                result.actions.push_back({MovementAnimationActionKind::SetSelected, 0});
                if (effects.set_selected)
                    effects.set_selected(false); // borrowed for jumping flag
                m_animTickLand = now();
                currentControlState = MotionControlState::landing;
                return "LAND";
            } else if (jumptime > JUMP_PERIOD) {
                // jump down
                return "JUMP";
            }
            return CurrentMovementAnimation;
        }

        if (currentControlState == MotionControlState::falling) {
            Falling = false;
            currentControlState = MotionControlState::landing;

            if ((controlFlags & (Flags::AGENT_CONTROL_FINISH_ANIM)) == 0) {
                float fallVsq = m_lastFallVelocity * m_lastFallVelocity;
                if (fallVsq > 300.0f) // aprox 20*h
                {
                    m_animTickLand = add(now(), 3000);
                    return "STANDUP";
                }
                if (fallVsq > 160.0f) {
                    m_animTickLand = add(now(), 1500);
                    return "SOFT_LAND";
                }
            }
            m_animTickLand = add(now(), 600);
            return "LAND";
        }

        if (currentControlState == MotionControlState::landing) {
            Falling = false;

            if ((controlFlags & (Flags::AGENT_CONTROL_FINISH_ANIM)) == 0) {
                if ((m_animTickLand != 0) && (m_animTickLand > now()))
                    return CurrentMovementAnimation;
            }

            currentControlState = MotionControlState::onsurface;
            m_animTickLand = 0;
            return "STAND";
        }

        // next section moved outside paren. and realigned for jumping

        if (heldOnXY) {
            currentControlState = MotionControlState::onsurface;
            Falling = false;
            // Walking / crouchwalking / running
            if (heldDown) {
                return "CROUCHWALK";
            }
            // We need to prevent these animations if the user tries to make their avatar walk or run whilst
            // specifying AGENT_CONTROL_STOP (pressing down space on viewers).
            else if (!context.stop_active) {
                if (context.always_run)
                    return "RUN";
                else
                    return "WALK";
            }
        } else {
            currentControlState = MotionControlState::onsurface;
            Falling = false;
            // Not walking
            if (heldDown)
                return "CROUCH";
            else if (heldTurnLeft)
                return "TURNLEFT";
            else if (heldTurnRight)
                return "TURNRIGHT";
            else
                return "STAND";
        }

        return CurrentMovementAnimation;
    }

    MovementAnimationResult apply(MovementAnimationContext& context, const MovementAnimationEffects& effects,
                                  bool force) {
        MovementAnimationResult result;
        auto name = determine(context, effects, result);
        result.name_changed = name != CurrentMovementAnimation;
        if (force || result.name_changed) {
            CurrentMovementAnimation = std::move(name);
            result.apply_attempted = true;
            if (effects.try_set_animation)
                result.applied = effects.try_set_animation(CurrentMovementAnimation);
        }
        result.animation = CurrentMovementAnimation;
        return result;
    }
};
MovementAnimator::MovementAnimator(Clock clock) : impl_(std::make_unique<Impl>(std::move(clock))) {
}
MovementAnimator::~MovementAnimator() = default;
MovementAnimationResult MovementAnimator::update(MovementAnimationContext& context,
                                                 const MovementAnimationEffects& effects) {
    return impl_->apply(context, effects, false);
}
MovementAnimationResult MovementAnimator::force_update(MovementAnimationContext& context,
                                                       const MovementAnimationEffects& effects) {
    return impl_->apply(context, effects, true);
}
MovementAnimationSnapshot MovementAnimator::snapshot() const {
    auto& i = *impl_;
    return {i.CurrentMovementAnimation, i.currentControlState, i.isJumping,      i.Falling,
            i.m_animTickFall,           i.m_animTickLand,      i.m_animTickJump, i.m_lastFallVelocity};
}
MotionControlState MovementAnimator::state() const {
    return impl_->currentControlState;
}
void MovementAnimator::restore_state(MotionControlState state, std::string movement_animation) {
    impl_->currentControlState = state;
    if (!movement_animation.empty())
        impl_->CurrentMovementAnimation = std::move(movement_animation);
}
}
