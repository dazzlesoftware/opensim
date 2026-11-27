// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/object_teleport_motion.hpp"
#include "opensim/framework/uuid.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace opensim::scene {
struct ObjectTeleportState {
    framework::Uuid id;
    bool in_transit = false, deleted = false, attached = false, selected = false;
    bool scene_available = true, keyframe_motion = false;
    framework::Vector3 position, scale;
    ObjectTeleportMotion motion;
    std::vector<framework::Uuid> seated_avatars;
};

struct ObjectTeleportParcel {
    std::function<bool(framework::Uuid)> restricted, banned;
};

struct ObjectTeleportCrossing {
    framework::Uuid source;
    framework::Vector3 target;
    std::int32_t flags;
    ObjectTeleportMotion motion;
};

struct ObjectTeleportHost {
    std::function<bool(framework::Vector3)> inside_region;
    std::function<bool(framework::Uuid, framework::Uuid, int)> cooldown;
    std::function<std::shared_ptr<ObjectTeleportParcel>(float, float)> land_at;
    std::function<bool(const ObjectTeleportParcel &)> can_enter_with_scripts;
    std::function<float(float, float)> ground_height;
    std::function<ObjectTeleportMotion(ObjectTeleportMotion)> store_motion;
    std::function<void()> terse_update;
    std::function<void(const ObjectTeleportCrossing &)> schedule_crossing;
};

// Source SceneObjectGroup.TeleportObject orchestration. The host must bind real
// parcel/physics/scene state and asynchronous crossing before script exposure.
int teleport_object(ObjectTeleportState &, framework::Uuid source, framework::Vector3 target,
                    framework::Quaternion rotation, std::int32_t flags, const ObjectTeleportHost &);
} // namespace opensim::scene
