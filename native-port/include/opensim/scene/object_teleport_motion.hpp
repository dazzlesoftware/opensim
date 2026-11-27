// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/quaternion_text.hpp"
#include "opensim/framework/vector_text.hpp"
#include <cstdint>

namespace opensim::scene {
struct ObjectTeleportMotion {
    framework::Vector3 velocity, angular_velocity, acceleration;
    framework::Quaternion orientation;
};

// SceneObjectGroup.TeleportObject motion preparation. Land/seat checks and
// transfer completion are separate dependencies; this is not the script API.
ObjectTeleportMotion prepare_object_teleport_motion(ObjectTeleportMotion current, framework::Quaternion rotation,
                                                    std::int32_t flags);
// Backing-field result of SceneObjectPart's motion setters without a physics actor.
ObjectTeleportMotion object_teleport_root_storage(ObjectTeleportMotion motion);
} // namespace opensim::scene
