// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/keyframed_motion.hpp"
#include <span>

namespace opensim::scene {
struct KeyframedMotionSerializedState {
    KeyframedMotionState playback;
    // List<T> serializes its complete backing array, including unused entries.
    std::vector<KeyframedMotionFrame> backing_frames;
    std::int32_t list_version = 0;
    bool keyframes_share_backing = false;
};

// Canonical original BinaryFormatter graph import, without CLR activation.
std::optional<KeyframedMotionSerializedState> decode_keyframed_motion(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_keyframed_motion(const KeyframedMotionSerializedState &state);
} // namespace opensim::scene
