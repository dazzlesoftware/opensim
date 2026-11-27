// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/viewer_physics.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace scene;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void near(Vector3 actual, Vector3 expected, const char *message) {
    if (std::abs(actual.x - expected.x) >= .002f || std::abs(actual.y - expected.y) >= .002f ||
        std::abs(actual.z - expected.z) >= .002f)
        throw std::runtime_error(std::string(message) + ": actual=" + std::to_string(actual.x) + "," +
                                 std::to_string(actual.y) + "," + std::to_string(actual.z) +
                                 "; expected=" + std::to_string(expected.x) + "," + std::to_string(expected.y) + "," +
                                 std::to_string(expected.z));
}

bool contacts(const ViewerPhysics::PollResult &result, Uuid first, Uuid second) {
    for (const auto &report : result.prim_collision_reports)
        for (const auto &[actor, other] : report.objects)
            if ((actor == first && other.id == second) || (actor == second && other.id == first))
                return true;
    return false;
}

void exercise(const std::filesystem::path &library, std::shared_ptr<const DefaultAnimations> animations) {
    World world(Uuid::random());
    world.terrain().clear_land(0);
    AgentCircuitManager circuits;
    ViewerRegion viewers(
        world, circuits, [](const auto &, auto) { return true; },
        [](const auto &, const auto &, const auto &) { return false; });
    ViewerPhysics engine(world, viewers, library, std::move(animations), [](const auto &) {});
    unsigned creations = 0;
    engine.set_physical_root_created([&](const SceneObject &) { ++creations; });
    const auto step = [&] {
        // Exercise real solver steps rather than only the actor synchronization
        // path. Each wait is bounded to a single 20ms ubODE simulation interval.
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        auto result = engine.poll();
        check(result.steps > 0, "Actual physics fixture did not advance the solver");
        return result;
    };
    const auto collision_step = [&] {
        // Collision reports batch three 20ms solver steps. Flush a complete
        // batch after a filter/pose transition so pre-transition contacts cannot
        // leak into the assertion, then collect an entirely fresh report window.
        for (std::size_t steps = 0; steps < 3;)
            steps += step().steps;
        ViewerPhysics::PollResult result;
        while (result.steps < 3) {
            auto current = step();
            result.steps += current.steps;
            for (auto &report : current.prim_collision_reports)
                result.prim_collision_reports.push_back(std::move(report));
        }
        check(!result.prim_collision_reports.empty(), "Fresh collision report window was not emitted");
        return result;
    };
    auto object = SceneObject::primitive(Uuid::random(), {50, 50, 30}, {1, 1, 1});
    object.flags = 1;
    object.physics.gravity = 0;
    object.velocity = {2, 0, 0};
    object.angular_velocity = {0, 0, .25f};
    const auto root = world.add(object).value();
    auto child = SceneObject::primitive(root.owner, {1, 0, 0});
    child.parent_id = root.local_id;
    child = world.add(child).value();
    step();
    step();
    check(creations == 1 && engine.prim_is_physical(root), "Initial linked dynamic actor was not created once");
    check(world.object(root.id)->position.x > 50.01f, "Initial actor did not have live momentum");
    const auto mass = engine.mass_properties(root.id);
    const auto child_mass = engine.mass_properties(root.id, child.id);
    check(mass && child_mass && mass->mass > 0 && child_mass->mass > 0, "Linked actor mass fixture is unavailable");
    const auto assert_retained = [&] {
        check(creations == 1 && engine.prim_is_physical(root), "Selection removed or recreated the dynamic root actor");
        const auto current = engine.mass_properties(root.id);
        const auto current_child = engine.mass_properties(root.id, child.id);
        check(current && current_child && std::abs(current->mass - mass->mass) < .001f &&
                  std::abs(current_child->mass - child_mass->mass) < .001f,
              "Selection lost or changed root/child mass properties");
    };
    const auto stopped_at = [&](Vector3 position) {
        const auto current = world.object(root.id).value();
        near(current.position, position, "Selected actor changed position while disabled");
        near(current.velocity, {}, "Selected actor retained linear velocity");
        near(current.angular_velocity, {}, "Selected actor retained angular velocity");
        near(world.object(child.id)->position, child.position, "Selected linked child changed its local offset");
        assert_retained();
    };
    const auto selected_position = world.object(root.id)->position;
    const Vector3 retained_torque{0, 0, 1};
    world.set_prim_torque(root.id, retained_torque, false);
    check(world.set_selected(child.id, true), "Could not select linked child");
    check(world.teleport_group(root.id)->selected, "Child selection did not select the group");
    engine.poll(false);
    world.set_prim_torque(child.id, {0, 0, 9}, false);
    near(world.prim_torque(root.id), retained_torque, "Selected torque write replaced the retained constant torque");
    const Vector3 selected_force{mass->mass * 10, 0, 0};
    world.set_prim_force(child.id, selected_force);
    near(world.prim_force(root.id), selected_force, "Selected force write was discarded instead of retained");
    engine.apply_prim_impulse(root.id, {mass->mass * 10, 0, 0}, false);
    engine.apply_prim_impulse(child.id, {0, 0, child_mass->mass * 10}, true);
    for (int i = 0; i < 3; ++i) {
        step();
        stopped_at(selected_position);
    }
    world.set_prim_force(root.id, {});

    const Quaternion edited_rotation{0, 0, .70710677f, .70710677f};
    const auto before_edit = world.object(root.id).value();
    check(bool(world.store_teleport_motion(before_edit, {{5, 0, 0}, {0, 0, 3}, {}, edited_rotation})),
          "Could not write a selected actor's transform and motion");
    check(world.finish_object_teleport(before_edit, {70, 70, 40}), "Could not edit selected actor position");
    step();
    stopped_at({70, 70, 40});
    check(std::abs(world.object(root.id)->rotation.z - edited_rotation.z) < .002f &&
              std::abs(world.object(root.id)->rotation.w - edited_rotation.w) < .002f,
          "Selected actor discarded its edited rotation");
    check(world.set_selected(child.id, false), "Could not deselect linked child");
    // The old constant torque intentionally survives selection. Clear it after
    // deselection, before stepping, to isolate discarded transient impulses.
    world.set_prim_torque(root.id, {}, false);
    for (int i = 0; i < 3; ++i) {
        step();
        stopped_at({70, 70, 40});
    }
    // Selected impulses and velocity writes must not become a delayed kick on
    // release. A new continuous force is a separate request and should resume.
    world.set_prim_force(root.id, {mass->mass * 10, 0, 0});
    step();
    step();
    check(world.object(root.id)->velocity.x > .05f && world.object(root.id)->position.x > 70.001f,
          "Retained deselected actor did not resume under a fresh force");
    assert_retained();

    const auto before_coalesced = world.object(root.id)->position;
    world.set_prim_force(root.id, {});
    engine.apply_prim_impulse(root.id, {mass->mass * 10, 0, 0}, false);
    check(world.set_selected(root.id, true) && world.set_selected(root.id, false),
          "Could not coalesce a select/deselect pair before synchronization");
    check(!world.teleport_group(root.id)->selected, "Coalesced selection did not end deselected");
    step();
    stopped_at(before_coalesced);
    step();
    stopped_at(before_coalesced);

    // A same-valued explicit velocity request made after release is newer than
    // the selection Stop, even when the final selected flag is false at poll.
    world.set_prim_velocity(root.id, {3, 0, 0}, false, false);
    step();
    const auto previous_momentum = world.object(root.id)->velocity;
    check(previous_momentum.x > 2.5f, "Could not prepare velocity-ordering momentum");
    const auto before_velocity_release = world.object(root.id)->position;
    check(world.set_selected(root.id, true) && world.set_selected(root.id, false),
          "Could not coalesce selection for explicit velocity ordering");
    world.set_prim_velocity(root.id, previous_momentum, false, false);
    step();
    check(world.object(root.id)->velocity.x > 2.5f &&
              world.object(root.id)->position.x > before_velocity_release.x + .02f,
          "Selection Stop discarded a newer same-valued post-release velocity request");
    assert_retained();

    // Conversely, a queued impulse from before selection was discarded above.
    // A new impulse queued after release must apply after the Stop operation.
    const auto before_impulse_release = world.object(root.id)->position;
    check(world.set_selected(root.id, true) && world.set_selected(root.id, false),
          "Could not coalesce selection for post-release impulse ordering");
    engine.apply_prim_impulse(root.id, {mass->mass * 2, 0, 0}, false);
    step();
    check(world.object(root.id)->velocity.x > 1.5f && world.object(root.id)->velocity.x < 2.5f &&
              world.object(root.id)->position.x > before_impulse_release.x + .01f,
          "Post-release impulse was discarded or added to uncleared old momentum");
    assert_retained();
    world.set_prim_velocity(root.id, {}, false, false);
    step();

    // A selected static sensor keeps its initial contacts until the first edit,
    // unlike a selected dynamic body. Sensors avoid contact response moving the
    // probe away while testing filter transitions through collision reports.
    auto sensor_object = SceneObject::primitive(root.owner, {120, 120, 30}, {2, 2, 2});
    const auto sensor = world.add(sensor_object).value();
    world.set_group_flags(sensor.id, false, false, false, true);
    auto probe_object = SceneObject::primitive(root.owner, {120, 120, 30});
    probe_object.flags = 1;
    probe_object.physics.gravity = 0;
    const auto probe = world.add(probe_object).value();
    check(contacts(collision_step(), sensor.id, probe.id), "Static sensor and dynamic probe did not initially overlap");
    const auto callbacks_with_probe = creations;
    check(callbacks_with_probe == 2, "Collision probe did not create exactly one additional physical actor");
    check(world.set_selected(sensor.id, true), "Could not select static sensor");
    check(contacts(collision_step(), sensor.id, probe.id),
          "Initial static selection incorrectly suppressed collider contacts");
    check(engine.mass_properties(sensor.id).has_value() && !engine.prim_is_physical(sensor),
          "Selected static sensor lost its actor or became physical");
    check(world.store_keyframed_position(sensor, {120.1f, 120, 30}), "Could not edit selected static sensor pose");
    for (int i = 0; i < 3; ++i)
        check(!contacts(collision_step(), sensor.id, probe.id),
              "Edited selected static sensor retained collision contacts");
    world.set_prim_force(sensor.id, {1, 2, 3});
    check(!contacts(collision_step(), sensor.id, probe.id), "Static force update restored a hidden selected collider");
    auto material = world.object(sensor.id)->physics;
    material.friction = .2f;
    check(world.update_extra_physics(sensor.id, material, false).has_value(),
          "Could not update static contact material");
    check(!contacts(collision_step(), sensor.id, probe.id),
          "Static material update restored a hidden selected collider");
    check(world.set_selected(sensor.id, false), "Could not deselect static sensor");
    check(contacts(collision_step(), sensor.id, probe.id), "Static sensor contacts did not return on deselection");

    check(world.store_keyframed_position(sensor, {120.2f, 120, 30}), "Could not edit static sensor before selecting");
    check(world.set_selected(sensor.id, true), "Could not select static sensor after its edit");
    check(contacts(collision_step(), sensor.id, probe.id),
          "Position edit before first static selection incorrectly suppressed the collider");
    check(world.set_selected(sensor.id, false), "Could not release the pre-edited static sensor");
    check(contacts(collision_step(), sensor.id, probe.id),
          "Pre-edited static sensor lost its contacts after deselection");

    check(world.set_selected(sensor.id, true), "Could not select static sensor before editing");
    check(world.store_keyframed_position(sensor, {120.3f, 120, 30}), "Could not edit static sensor after selecting");
    check(!contacts(collision_step(), sensor.id, probe.id),
          "Selection followed by position edit before one poll did not suppress the static collider");
    check(world.set_selected(sensor.id, false), "Could not release the post-selection edited static sensor");
    check(contacts(collision_step(), sensor.id, probe.id),
          "Post-selection edited static sensor did not regain contacts");

    check(world.store_keyframed_position(sensor, {125, 120, 30}), "Could not relocate the static sensor");
    check(!contacts(collision_step(), sensor.id, probe.id),
          "Relocated static collider still contacted its old position");
    const auto before_probe_move = world.object(probe.id).value();
    check(bool(world.store_teleport_motion(before_probe_move, {{}, {}, {}, before_probe_move.rotation})),
          "Could not stop the relocation probe");
    check(world.finish_object_teleport(before_probe_move, {125, 120, 30}),
          "Could not move probe to relocated collider");
    check(contacts(collision_step(), sensor.id, probe.id),
          "Relocated static collider did not contact its new position");

    check(world.set_selected(sensor.id, true), "Could not begin static reselection fixture");
    check(contacts(collision_step(), sensor.id, probe.id), "Fresh static selection suppressed initial contacts");
    check(world.store_keyframed_position(sensor, {125.1f, 120, 30}), "Could not hide the selected static sensor");
    check(!contacts(collision_step(), sensor.id, probe.id),
          "Static reselection fixture did not initially become hidden");
    check(world.set_selected(sensor.id, false) && world.set_selected(sensor.id, true),
          "Could not coalesce static deselection and reselection");
    check(contacts(collision_step(), sensor.id, probe.id),
          "Coalesced static reselection retained a previous hidden filter");
    check(world.set_selected(sensor.id, false) && world.set_selected(sensor.id, true),
          "Could not coalesce static reselection before the same-pose edit");
    // OdePrim.Position queues a change even when its numeric value is equal.
    check(world.store_keyframed_position(sensor, {125.1f, 120, 30}), "Could not request the same selected static pose");
    check(!contacts(collision_step(), sensor.id, probe.id),
          "Same-pose edit after coalesced static reselection failed to suppress collision");
    check(world.set_selected(sensor.id, false), "Could not release same-pose edited static sensor");
    check(contacts(collision_step(), sensor.id, probe.id),
          "Same-pose edited static sensor did not regain contacts on release");
    check(creations == callbacks_with_probe, "Selection or static editing recreated a dynamic body");

    const auto linked_root = world.add(SceneObject::primitive(root.owner, {160, 160, 30}, {2, 2, 2})).value();
    auto local_child = SceneObject::primitive(root.owner, {4, 0, 0}, {2, 2, 2});
    local_child.parent_id = linked_root.local_id;
    local_child = world.add(local_child).value();
    auto sibling = SceneObject::primitive(root.owner, {0, 4, 0}, {2, 2, 2});
    sibling.parent_id = linked_root.local_id;
    sibling = world.add(sibling).value();
    world.set_group_flags(linked_root.id, false, false, false, true);
    const auto add_probe = [&](Vector3 position) {
        auto object = SceneObject::primitive(root.owner, position);
        object.flags = 1;
        object.physics.gravity = 0;
        return world.add(object).value();
    };
    const auto root_probe = add_probe({160, 160, 30});
    const auto child_probe = add_probe({164, 160, 30});
    const auto sibling_probe = add_probe({160, 164, 30});
    auto linked_reports = collision_step();
    check(contacts(linked_reports, linked_root.id, root_probe.id) &&
              contacts(linked_reports, local_child.id, child_probe.id) &&
              contacts(linked_reports, sibling.id, sibling_probe.id),
          "Linked static scope fixture did not expose each part's separate collider contacts");
    const auto linked_callbacks = creations;
    check(linked_callbacks == callbacks_with_probe + 3, "Linked static probes did not create three dynamic bodies");
    check(world.set_selected(linked_root.id, true), "Could not select linked static group");
    linked_reports = collision_step();
    check(contacts(linked_reports, linked_root.id, root_probe.id) &&
              contacts(linked_reports, local_child.id, child_probe.id) &&
              contacts(linked_reports, sibling.id, sibling_probe.id),
          "Initial static group selection suppressed a root or child collider");
    check(world.patch_transform(local_child.id, Vector3{4.1f, 0, 0}, std::nullopt, std::nullopt),
          "Could not edit one selected static child's local pose");
    linked_reports = collision_step();
    check(!contacts(linked_reports, local_child.id, child_probe.id),
          "Edited selected static child's collider did not become hidden");
    check(contacts(linked_reports, linked_root.id, root_probe.id) &&
              contacts(linked_reports, sibling.id, sibling_probe.id),
          "Editing one static child incorrectly suppressed root or sibling colliders");
    check(world.store_keyframed_position(linked_root, {160.1f, 160, 30}),
          "Could not edit the selected static group's root position");
    linked_reports = collision_step();
    check(!contacts(linked_reports, linked_root.id, root_probe.id) &&
              !contacts(linked_reports, local_child.id, child_probe.id) &&
              !contacts(linked_reports, sibling.id, sibling_probe.id),
          "Group root position edit did not suppress every selected static collider");
    check(world.set_selected(linked_root.id, false), "Could not release selected static linkset");
    linked_reports = collision_step();
    check(contacts(linked_reports, linked_root.id, root_probe.id) &&
              contacts(linked_reports, local_child.id, child_probe.id) &&
              contacts(linked_reports, sibling.id, sibling_probe.id),
          "Static linkset release did not restore all independently edited collider contacts");
    check(creations == linked_callbacks, "Static linkset edit scope recreated a dynamic probe");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Physics library and animation catalog paths required");
        exercise(argv[1], std::make_shared<DefaultAnimations>(argv[2]));
        std::cout << "PASS: selected actor retention, stopped linkset, edits, coalesced edges and static filters\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
