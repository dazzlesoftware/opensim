// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/rez_location.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include <array>
#include <limits>
#include <cmath>
#include <stdexcept>
namespace opensim::scene {
namespace {
using framework::Vector3;
Vector3 add(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vector3 multiply(Vector3 a, float b) {
    return {a.x * b, a.y * b, a.z * b};
}
}
RezIntersection intersect_rez_box(Vector3 position, framework::Quaternion rotation, Vector3 scale,
                                  Vector3 origin, Vector3 direction, bool front_faces_only,
                                  bool face_center) {
    const auto length = std::sqrt(framework::sdk_quaternion_length_squared(rotation));
    if (!std::isfinite(length) || length == 0)
        return {};
    rotation = {rotation.x / length, rotation.y / length, rotation.z / length, rotation.w / length};
    const framework::Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
    const auto local_origin = framework::sdk_rotate_vector(
        {origin.x - position.x, origin.y - position.y, origin.z - position.z}, inverse);
    const auto local_direction = framework::sdk_rotate_vector(direction, inverse);
    const std::array offsets{local_origin.x, local_origin.y, local_origin.z};
    const std::array directions{local_direction.x, local_direction.y, local_direction.z};
    const std::array half{scale.x * .5f, scale.y * .5f, scale.z * .5f};
    float closest = std::numeric_limits<float>::infinity();
    RezIntersection result;
    // The face order matches the original prim OBB for edge and corner ties.
    static constexpr std::array axes{0, 1, 0, 1, 2, 2};
    static constexpr std::array signs{1.f, 1.f, -1.f, -1.f, 1.f, -1.f};
    for (std::size_t face = 0; face < axes.size(); ++face) {
        const auto normal_axis = axes[face];
        const auto sign = signs[face], delta = directions[normal_axis];
        if (delta == 0 || (front_faces_only && delta * sign >= 0))
            continue;
        const auto distance = (sign * half[normal_axis] - offsets[normal_axis]) / delta;
        if (distance < 0 || distance >= closest)
            continue;
        bool inside = true;
        for (int i = 0; i < 3; ++i)
            if (i != normal_axis && std::abs(offsets[i] + distance * directions[i]) > half[i] + 1e-5f)
                inside = false;
        if (!inside)
            continue;
        std::array<float, 3> normal{};
        normal[normal_axis] = sign;
        const Vector3 local_normal{normal[0], normal[1], normal[2]};
        const auto world_normal = framework::sdk_rotate_vector(local_normal, rotation);
        const auto point = face_center ? add(position, multiply(world_normal, half[normal_axis]))
                                       : add(origin, multiply(direction, distance));
        result = {true, point, world_normal, local_normal};
        closest = distance;
    }
    return result;
}
framework::Vector3 get_new_rez_location(const RezLocationRequest& r, const RezLocationBackend& backend) {
    Vector3 dir{r.ray_end.x - r.ray_start.x, r.ray_end.y - r.ray_start.y, r.ray_end.z - r.ray_start.z};
    Vector3 water{0, 0, -100.f};
    if (r.ray_start.z > backend.water_height && r.ray_end.z < backend.water_height) {
        float ratio = (backend.water_height - r.ray_start.z) / dir.z;
        water = {r.ray_start.x + ratio * dir.x, r.ray_start.y + ratio * dir.y, backend.water_height};
    }
    auto above_water = [&](Vector3 p) { return water.z > p.z ? water : p; };
    if (r.ray_end_is_intersection != 1) {
        float dist = std::sqrt((dir.x * dir.x + dir.y * dir.y) + dir.z * dir.z);
        if (dist != 0) {
            Vector3 direction = multiply(dir, 1.f / dist);
            dist += 1.f;
            if (backend.filtered_raycast) {
                auto contacts = backend.filtered_raycast(r.ray_start, direction, dist, 4, 0x407a);
                if (!contacts.empty()) {
                    auto position = [&](const RezContact& c) {
                        Vector3 offset{c.normal.x * r.scale.x, c.normal.y * r.scale.y,
                                       c.normal.z * r.scale.z};
                        offset = multiply(offset, .5f);
                        return above_water(add(c.position, offset));
                    };
                    if (r.target.is_zero()) {
                        for (const auto& c : contacts)
                            if (c.consumer_id == 0)
                                return position(c);
                    } else {
                        if (!backend.part_uuid)
                            throw std::logic_error("rez backend requires local-ID part lookup");
                        for (const auto& c : contacts) {
                            auto id = backend.part_uuid(c.consumer_id);
                            if (id && *id == r.target)
                                return position(c);
                        }
                    }
                    return position(contacts.front());
                }
            }
            if (!r.target.is_zero()) {
                if (!backend.target_intersection)
                    throw std::logic_error("rez backend requires target OBB intersection");
                auto intersection = backend.target_intersection(
                    r.target, r.ray_start, direction, {0, 0, 0, 1}, r.front_faces_only, r.face_center);
                if (intersection) {
                    const auto& e = *intersection;
                    if (e.hit) {
                        float offset = .5f;
                        if (e.axis_aligned_face_normal.x != 0)
                            offset = r.scale.x;
                        if (e.axis_aligned_face_normal.y != 0)
                            offset = r.scale.y;
                        if (e.axis_aligned_face_normal.z != 0)
                            offset = r.scale.z;
                        offset = std::abs(offset);
                        return above_water(add(e.point, multiply(e.normal, offset / 2.f)));
                    }
                } else {
                    if (!backend.closest_intersection)
                        throw std::logic_error("rez backend requires closest prim intersection");
                    auto e = backend.closest_intersection(r.ray_start, direction, true, false);
                    return above_water(e.hit ? e.point : r.ray_end);
                }
            }
        }
    }
    auto pos = r.ray_end;
    pos.z += r.scale.z / 2.f;
    return above_water(pos);
}
}
