// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/group_position.hpp"
#include <cmath>
#include <limits>

namespace opensim::scene {
namespace {
float distance_squared(framework::Vector3 a, framework::Vector3 b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}

bool approximately_equal(float a, float b) {
    // Bundled SDK Utils.ApproxEqual(a,b,tolerance,float.Epsilon). The second
    // test matters for infinity, even with the tiny CLR epsilon multiplier.
    const float difference = std::abs(a - b);
    if (difference <= 1.e-3f)
        return true;
    a = std::abs(a);
    b = std::abs(b);
    if (b > a)
        a = b;
    return difference <= a * std::numeric_limits<float>::denorm_min();
}
} // namespace

void set_group_position(GroupPositionState &s, framework::Vector3 value, const GroupPositionHost &host) {
    if (s.scene && !host.inside(value) && !s.attached && !s.loading && !s.disable_transfer) {
        if (!s.in_transit) {
            s.in_transit = true;
            host.schedule_crossing(value);
        }
        return;
    }
    if (s.sandboxed && distance_squared(s.sandbox_origin, value) > 100.f) {
        host.disable_physics();
        if (s.scene)
            host.sandbox_debug();
        return;
    }
    const bool same = approximately_equal(s.position.x, value.x) && approximately_equal(s.position.y, value.y) &&
                      approximately_equal(s.position.z, value.z);
    const bool changed = !s.duplicating && !s.deleted && !same;
    s.position = value;
    host.write_positions(value);
    host.move_sitters();
    if (changed && s.changed_events)
        host.changed_position();
    if (s.scene)
        host.taint_parcel_counts();
}
} // namespace opensim::scene
