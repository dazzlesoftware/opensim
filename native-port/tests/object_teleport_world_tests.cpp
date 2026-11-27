// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/world.hpp"
#include "opensim/scene/object_teleport.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace opensim;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

int main() {
    try {
        scene::World world(framework::Uuid::random());
        world.terrain().clear_land(21.f);
        const auto owner = framework::Uuid::random();
        auto root = scene::SceneObject::primitive(owner, {12, 13, 30}, {1, 2, 4});
        root.flags = 1;
        root.velocity = {1, 2, 3};
        root.angular_velocity = {.1f, .2f, .3f};
        root.acceleration = {.01f, .02f, .03f};
        root.rotation = {0, 0, .6f, .8f};
        const auto added = world.add(root);
        check(bool(added), "Root add failed");
        root = *added;
        auto child = scene::SceneObject::primitive(owner, {2, 3, 4});
        child.parent_id = root.local_id;
        const auto added_child = world.add(child);
        check(bool(added_child), "Child add failed");
        child = *added_child;
        check(!world.teleport_group(child.id), "Child UUID must not resolve as a scene group");
        check(!world.teleport_group(framework::Uuid::random()), "Missing UUID resolved");
        world.set_selected(child.id, true);
        check(world.teleport_group(root.id)->selected, "Child selection must select the group");
        world.set_selected(child.id, false);
        const auto snapshot = world.teleport_group(root.id).value();
        check(!snapshot.selected, "Deselection not reflected");
        const auto physical = world.collision_prims();
        check(physical.size() == 1, "Physical linkset snapshot failed");
        scene::ObjectTeleportState state;
        state.id = root.id;
        state.position = root.position;
        state.scale = root.scale;
        state.motion = {root.velocity, root.angular_velocity, root.acceleration, root.rotation};
        scene::ObjectTeleportHost host;
        host.inside_region = [](auto p) { return p.x >= 0 && p.x < 256 && p.y >= 0 && p.y < 256; };
        host.cooldown = [](auto, auto, int) { return false; };
        host.land_at = [](auto, auto) { return std::shared_ptr<scene::ObjectTeleportParcel>{}; };
        host.ground_height = [&](auto x, auto y) { return world.terrain().height_at(x, y); };
        host.store_motion = [&](auto motion) {
            auto stored = world.store_teleport_motion(snapshot.root, motion);
            check(bool(stored), "Live motion store failed");
            return *stored;
        };
        host.terse_update = [&] {
            check(world.finish_object_teleport(snapshot.root, state.position), "Live position store failed");
        };
        world.update_object_text(root.id, false, "Concurrent name edit");
        world.drain_events();
        check(scene::teleport_object(state, root.id, {40, 45, 0}, {0, 0, 0, 1}, 5, host) == 1,
              "Local live teleport failed");
        const auto moved = world.object(root.id).value();
        check(moved.position.x == 40 && moved.position.y == 45 && std::abs(moved.position.z - 23.01f) < 1e-5f,
              "Terrain clearance differs");
        check(moved.velocity == framework::Vector3{} && moved.angular_velocity == framework::Vector3{} &&
                  moved.acceleration == framework::Vector3{},
              "Stop-at-target motion not stored");
        check(moved.name == "Concurrent name edit" && moved.changed,
              "Teleport lost another edit or backup dirty state");
        const auto unchanged_child = world.object(child.id).value();
        check(unchanged_child.position == child.position && unchanged_child.rotation == child.rotation,
              "Child local transform changed");
        const auto events = world.drain_events();
        check(events.size() == 1 && events[0].kind == scene::WorldEventKind::TerseUpdated &&
                  events[0].object.position == moved.position,
              "Teleport must publish the final root terse snapshot");
        check(!world.apply_physical_prim_motion(physical.front(), physical.front()),
              "Stale physics result reverted teleport");
        // Ground failure happens after motion storage and before position/update.
        host.ground_height = [](auto, auto) -> float { throw std::runtime_error("controlled ground failure"); };
        state.in_transit = false;
        bool failed = false;
        try {
            scene::teleport_object(state, root.id, {50, 55, 0}, {0, 0, 1, 0}, 4, host);
        } catch (const std::runtime_error &) {
            failed = true;
        }
        check(failed && state.in_transit && world.object(root.id)->position == moved.position &&
                  world.drain_events().empty(),
              "Ground failure must retain motion and transit without publishing a new position");
        check(world.object(root.id)->rotation.z == 1, "Motion/rotation setter did not run before failed ground query");
        const auto before_crossing = world.object(root.id).value();
        world.drain_events();
        const scene::ObjectTeleportMotion crossing_motion{{7, 8, 9}, {3, 2, 1}, {.3f, .2f, .1f}, {0, 0, 0, 2}};
        const auto crossing_stored = world.store_crossing_teleport_motion(before_crossing, crossing_motion);
        check(bool(crossing_stored), "Crossing motion store failed");
        const auto prepared_crossing = world.object(root.id).value();
        check(prepared_crossing.position == before_crossing.position &&
                  prepared_crossing.velocity == crossing_motion.velocity &&
                  prepared_crossing.angular_velocity == crossing_motion.angular_velocity &&
                  prepared_crossing.acceleration == crossing_motion.acceleration && prepared_crossing.rotation.w == 1 &&
                  !prepared_crossing.look_at.enabled && world.drain_events().empty(),
              "Crossing preparation must store normalized motion without moving or publishing the root");
        check(!world.store_crossing_teleport_motion(child, crossing_motion),
              "Crossing preparation accepted a child part");
        world.remove(root.id, false);
        auto replacement = scene::SceneObject::primitive(owner, {100, 100, 100});
        replacement.id = root.id;
        const auto replaced = world.add(replacement);
        check(bool(replaced) && replaced->local_id != root.local_id, "Replacement fixture failed");
        world.drain_events();
        check(!world.store_teleport_motion(snapshot.root, state.motion) &&
                  !world.store_crossing_teleport_motion(snapshot.root, crossing_motion) &&
                  !world.finish_object_teleport(snapshot.root, {0, 0, 0}),
              "Stale teleport wrote into a replacement group");
        check(world.object(root.id)->position == replacement.position && world.drain_events().empty(),
              "Stale write changed replacement");
        // A newly inserted instance may have exactly the same transform and
        // physical properties. A solver result still belongs to the old part.
        scene::World reused_world(framework::Uuid::random());
        auto original = scene::SceneObject::primitive(owner, {80, 80, 40});
        original.flags = 1;
        const auto original_part = reused_world.add(original).value();
        const auto old_physical = reused_world.collision_prims().front();
        auto old_motion = old_physical;
        old_motion.position.x += 1;
        reused_world.remove(original_part.id, false);
        const auto new_part = reused_world.add(original).value();
        check(new_part.id == original_part.id && new_part.local_id != original_part.local_id,
              "Identical physical replacement fixture failed");
        check(!reused_world.apply_physical_prim_motion(old_physical, old_motion) &&
                  reused_world.object(new_part.id)->position == original.position,
              "Stale solver result wrote into an identical replacement instance");
        std::cout << "PASS: live world teleport, linked transforms, selection, terrain, dirty/terse state, physics and "
                     "replacement guards\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
