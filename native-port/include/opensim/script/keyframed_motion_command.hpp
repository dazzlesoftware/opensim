// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/lsl_runtime.hpp"
#include <stdexcept>

namespace opensim::script {
enum class KeyframedMotionMode { Forward, Reverse, Loop, PingPong };

struct KeyframedMotionFrame {
    std::optional<framework::Vector3> position;
    std::optional<framework::Quaternion> rotation;
    std::int32_t time_ms = 0;
};

class KeyframedMotionCommandError : public std::runtime_error {
  public:
    enum class Kind { Index, InvalidCast };

    KeyframedMotionCommandError(Kind kind, std::string message) : std::runtime_error(std::move(message)), kind(kind) {}

    Kind kind;
};

struct KeyframedMotionCommandHost {
    // Effective RootPart.PhysActor.IsPhysical, not the persisted physical flag.
    bool physical = false, attachment = false;
    std::function<bool()> has_motion;
    std::function<void()> destroy, clear;
    std::function<void(KeyframedMotionMode, std::int32_t)> create;
    std::function<void(std::vector<KeyframedMotionFrame>)> set_keyframes;
    std::function<void()> start, stop, pause;
};

// Original llSetKeyframedMotion command and list-processing order. The host
// still has to bind the motion scheduler, scene lifecycle and persistence.
void set_keyframed_motion(const LslList &frames, const LslList &options, const KeyframedMotionCommandHost &host);
} // namespace opensim::script
