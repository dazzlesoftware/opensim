// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_teleport.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include <cmath>

namespace opensim::scene {
int teleport_object(ObjectTeleportState &state, framework::Uuid source, framework::Vector3 target,
                    framework::Quaternion rotation, std::int32_t flags, const ObjectTeleportHost &host) {
    if (state.in_transit || state.deleted || state.attached || state.selected || !state.scene_available)
        return -1;
    state.in_transit = true;
    if (state.keyframe_motion) {
        state.in_transit = false;
        return -1;
    }
    const auto motion = prepare_object_teleport_motion(state.motion, rotation, flags);
    if (host.inside_region(target)) {
        if (host.cooldown(state.id, source, 1000)) {
            state.in_transit = false;
            return -2;
        }
        const auto current_land = host.land_at(state.position.x, state.position.y);
        const auto land = host.land_at(target.x, target.y);
        if (land && land != current_land) {
            if (!host.can_enter_with_scripts(*land)) {
                state.in_transit = false;
                return -3;
            }
            for (const auto &avatar : state.seated_avatars)
                if (land->restricted(avatar) || land->banned(avatar)) {
                    state.in_transit = false;
                    return -4;
                }
        }
        state.motion = motion;
        if (host.store_motion)
            state.motion = host.store_motion(motion);
        const auto rotated_scale = framework::sdk_rotate_vector(state.scale, state.motion.orientation);
        const float height = host.ground_height(target.x, target.y) + .5f * std::abs(rotated_scale.z) + .01f;
        if (target.z < height)
            target.z = height;
        state.in_transit = false;
        state.position = target;
        host.terse_update();
        return 1;
    }
    if (host.cooldown(state.id, source, 20000)) {
        state.in_transit = false;
        return -1;
    }
    host.schedule_crossing({source, target, flags, motion});
    return 0;
}
} // namespace opensim::scene
