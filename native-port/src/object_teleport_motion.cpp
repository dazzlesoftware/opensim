// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_teleport_motion.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include <cmath>

namespace opensim::scene {
namespace {
framework::Quaternion normalize(framework::Quaternion rotation) {
    const auto squared = framework::sdk_quaternion_length_squared(rotation);
    if (!(squared > 1.e-6f))
        return {0, 0, 0, 1};
    const auto magnitude = std::sqrt(squared);
    if (framework::sdk_sse41_supported())
        return {rotation.x / magnitude, rotation.y / magnitude, rotation.z / magnitude, rotation.w / magnitude};
    const auto inverse = 1.f / magnitude;
    return {rotation.x * inverse, rotation.y * inverse, rotation.z * inverse, rotation.w * inverse};
}

framework::Quaternion multiply(framework::Quaternion left, framework::Quaternion right) {
    // Bundled SDK operator* treats either near-identity W as an identity, even
    // for non-unit input. Keep this shortcut rather than algebraically reducing.
    if (std::abs(left.w) > .999999f)
        return right;
    if (std::abs(right.w) > .999999f)
        return left;
    return framework::sdk_multiply_quaternion(left, right);
}
} // namespace

ObjectTeleportMotion prepare_object_teleport_motion(ObjectTeleportMotion current, framework::Quaternion rotation,
                                                    std::int32_t flags) {
    const bool stop = (flags & 1) != 0;
    const bool set_rotation = (flags & 4) != 0;
    rotation = normalize(rotation);
    if (set_rotation) {
        const auto q = current.orientation;
        rotation = multiply({-q.x, -q.y, -q.z, q.w}, rotation);
    }
    ObjectTeleportMotion result{{}, {}, {}, current.orientation};
    if (!stop) {
        result.velocity = current.velocity;
        result.angular_velocity = current.angular_velocity;
        result.acceleration = current.acceleration;
    }
    if (set_rotation || std::abs(rotation.w) < .99999) {
        if (!stop) {
            result.velocity = framework::sdk_rotate_vector(result.velocity, rotation);
            result.angular_velocity = framework::sdk_rotate_vector(result.angular_velocity, rotation);
            result.acceleration = framework::sdk_rotate_vector(result.acceleration, rotation);
        }
        result.orientation = multiply(current.orientation, rotation);
    }
    return result;
}

ObjectTeleportMotion object_teleport_root_storage(ObjectTeleportMotion motion) {
    const auto store = [](framework::Vector3 value) {
        // Exact Util.IsNanOrInfinity(Vector3) repeats IsNaN(Z), retaining infinite Z.
        return std::isnan(value.x) || std::isnan(value.y) || std::isnan(value.z) || std::isinf(value.x) ||
                       std::isinf(value.y)
                   ? framework::Vector3{}
                   : value;
    };
    return {store(motion.velocity), store(motion.angular_velocity), store(motion.acceleration),
            normalize(motion.orientation)};
}
} // namespace opensim::scene
