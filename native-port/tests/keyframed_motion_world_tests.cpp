// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/world.hpp"
#include "opensim/scene/group_position.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace opensim;

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main() {
    try {
        scene::World world;
        auto root = scene::SceneObject::primitive(framework::Uuid::random(), {10, 20, 30});
        root.acceleration = {7, 8, 9};
        root = world.add(root, false, false).value();
        auto child = scene::SceneObject::primitive(root.owner, {1, 2, 3});
        child.parent_id = root.local_id;
        child = world.add(child, false, false).value();
        world.drain_events();
        scene::GroupPositionState position;
        position.position = root.position;
        position.changed_events = true;
        scene::GroupPositionHost move;
        int crossings = 0, sitters = 0, changed = 0, taints = 0, stopped = 0;
        move.inside = [](auto p) { return p.x >= 0 && p.x < 256 && p.y >= 0 && p.y < 256; };
        move.schedule_crossing = [&](auto) { ++crossings; };
        move.write_positions = [&](auto p) {
            check(world.store_keyframed_position(root, p), "World group move failed");
        };
        move.move_sitters = [&] {
            check(world.object(root.id)->position == position.position, "Sitter callback saw old position");
            ++sitters;
        };
        move.changed_position = [&] { ++changed; };
        move.taint_parcel_counts = [&] { ++taints; };
        move.disable_physics = [&] { ++stopped; };
        move.sandbox_debug = [] {};
        scene::set_group_position(position, {12, 21, -5}, move);
        check(world.object(root.id)->position == framework::Vector3{12, 21, -5}, "Group move was clamped");
        check(sitters == 1 && changed == 1 && taints == 1, "Group movement callbacks missing");
        check(!world.object(root.id)->changed && world.drain_events().empty(),
              "Position assignment broadcast or dirtied group");
        scene::set_group_position(position, {260, 21, -5}, move);
        scene::set_group_position(position, {261, 21, -5}, move);
        check(crossings == 1 && position.in_transit &&
                  world.object(root.id)->position == framework::Vector3{12, 21, -5},
              "Crossing applied local position or queued twice");
        position.in_transit = false;
        position.sandboxed = true;
        position.sandbox_origin = {12, 21, -5};
        scene::set_group_position(position, {23, 21, -5}, move);
        check(stopped == 1 && sitters == 1, "Sandbox rejection continued local movement");
        check(world.store_keyframed_properties(root, framework::Quaternion{0, 0, 6, 8},
                                               framework::Vector3{1000, -2000, 3000}, framework::Vector3{1, 2, 3}),
              "Root property write failed");
        auto stored = world.object(root.id).value();
        check(stored.velocity == framework::Vector3{1000, -2000, 3000}, "Nonphysical velocity was clamped");
        check(std::abs(stored.rotation.z - .6f) < 1.e-7f && std::abs(stored.rotation.w - .8f) < 1.e-7f,
              "RotationOffset did not normalize");
        check(stored.acceleration == root.acceleration, "Unassigned acceleration changed");
        check(!stored.changed && world.drain_events().empty(), "Property write broadcast or dirtied group");
        check(world.object(child.id)->position == child.position, "Root property write changed child offset");
        check(!world.store_keyframed_properties(child, {}, framework::Vector3{9, 9, 9}, {}),
              "Child accepted as group root");
        const auto infinity = std::numeric_limits<float>::infinity();
        check(world.store_keyframed_properties(root, {}, framework::Vector3{infinity, 0, 0},
                                               framework::Vector3{0, 0, infinity}),
              "Special motion write failed");
        stored = world.object(root.id).value();
        check(stored.velocity == framework::Vector3{}, "Invalid X infinity was retained");
        check(std::isinf(stored.angular_velocity.z), "Original Z infinity behavior changed");
        check(world.schedule_keyframed_terse(root), "Terse scheduling failed");
        auto events = world.drain_events();
        check(events.size() == 1 && events[0].kind == scene::WorldEventKind::TerseUpdated &&
                  world.object(root.id)->changed,
              "Terse did not dirty group and queue update");
        check(world.remove(root.id), "Root removal failed");
        auto replacement = root;
        replacement.local_id = 0;
        replacement = world.add(replacement).value();
        check(replacement.local_id != root.local_id, "Replacement reused local ID");
        world.drain_events();
        check(!world.store_keyframed_properties(root, {}, framework::Vector3{99, 0, 0}, {}),
              "Stale motion wrote replacement");
        check(!world.schedule_keyframed_terse(root) && world.drain_events().empty(),
              "Stale motion queued replacement update");
        check(!world.store_keyframed_position(root, {100, 100, 100}), "Stale motion moved replacement");
        std::cout << "PASS: keyframed World property storage, terse and instance guards\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
