// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_teleport.hpp"
#include "opensim/scene/viewer_physics.hpp"
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include <atomic>
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

void near(Vector3 actual, Vector3 expected, float tolerance, const char *message) {
    check(std::abs(actual.x - expected.x) < tolerance && std::abs(actual.y - expected.y) < tolerance &&
              std::abs(actual.z - expected.z) < tolerance,
          message);
}

void live_region(const std::filesystem::path &library, std::shared_ptr<const DefaultAnimations> animations) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "Physical teleport region";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto owner = Uuid::random();
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->owner = owner;
    parcel->id = Uuid::random();
    std::atomic<unsigned> sends = 0;
    std::atomic<bool> valid_snapshot = false;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.physics_library = library;
    policies.animations = std::move(animations);
    policies.script_parcel_at = [parcel](Vector3) { return parcel; };
    auto remote = region;
    remote.id = Uuid::random();
    remote.x = 256;
    policies.teleport_destination = [remote](std::uint64_t) { return std::optional{remote}; };
    policies.send_crossing_object = [&](auto &, Vector3, std::shared_ptr<services::SimulationObject> object) {
        const auto xml = object->to_xml2();
        const auto parsed = read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size()});
        if (parsed) {
            const auto &part = parsed->linksets.front().parts.front().object;
            valid_snapshot =
                part.velocity.x > 1.9f && std::abs(part.rotation.z) > .7f && std::abs(part.rotation.w) > .7f;
        }
        ++sends;
        return false;
    };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.world().terrain().clear_land(0);
    auto listener = std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9002, 0, std::string("127.0.0.1")});
    runtime.start(listener);
    auto object = SceneObject::primitive(owner, {50, 50, 30}, {1, 2, 4});
    object.flags = 1;
    object.physics.gravity = 0;
    object.velocity = {2, 0, 0};
    const auto root = runtime.world().add(object, false).value();
    auto child = SceneObject::primitive(owner, {1, 0, 0});
    child.parent_id = root.local_id;
    child = runtime.world().add(child, false).value();
    const auto poll = [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        runtime.poll();
    };
    poll();
    poll();
    check(runtime.world().object(root.id)->position.x > 50, "Configured region did not simulate its physical object");
    check(runtime.teleport_object(child.id, root.id, {100, 100, 30}, {0, 0, 1, 0}, 5) == 1,
          "Live region physical teleport failed");
    poll();
    poll();
    auto current = runtime.world().object(root.id).value();
    near(current.position, {100, 100, 30}, .002f, "Region physics reverted the local teleport position");
    near(current.velocity, {}, .002f, "Region physics did not stop at the teleport target");
    check(std::abs(current.rotation.z) > .999f && std::abs(current.rotation.w) < .002f,
          "Region physics reverted the teleport rotation");
    check(bool(runtime.world().store_teleport_motion(current, {{2, 0, 0}, {}, {}, current.rotation})),
          "Could not set region physical momentum");
    check(runtime.teleport_object(child.id, root.id, {80, 80, 30}, {0, 0, .70710677f, .70710677f}, 4) == 1,
          "Live region momentum teleport failed");
    poll();
    poll();
    current = runtime.world().object(root.id).value();
    check(std::abs(current.position.x - 80) < .002f && current.position.y < 79.99f && current.velocity.y < -1.9f &&
              std::abs(current.rotation.z) > .7f && std::abs(current.rotation.w) > .7f,
          "Region actor did not continue with rotated teleport momentum");
    check(bool(runtime.world().store_teleport_motion(current, {{2, 0, 0}, {}, {}, current.rotation})),
          "Could not prepare the physical crossing fixture");
    check(runtime.teleport_object(child.id, root.id, {270, 80, 30}, {0, 0, 1, 0}, 5) == 0,
          "Live region physical remote teleport did not queue");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        poll();
        if (sends && runtime.world().object(root.id)->attached_to_backup)
            break;
    }
    poll();
    current = runtime.world().object(root.id).value();
    check(sends == 1 && valid_snapshot && current.attached_to_backup,
          "Region crossing lost unseated motion semantics or did not complete rejection recovery");
    near(current.velocity, {}, .002f, "Rejected physical teleport did not stop the region actor");
    check(std::abs(current.rotation.z) > .7f && std::abs(current.rotation.w) > .7f &&
              runtime.world().object(child.id)->position == child.position,
          "Rejected physical teleport changed source orientation or linked child offset");
    runtime.stop();
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Physics library and animation catalog paths required");
        World world(Uuid::random());
        world.terrain().clear_land(0);
        AgentCircuitManager circuits;
        ViewerRegion viewers(
            world, circuits, [](const auto &, auto) { return true; },
            [](const auto &, const auto &, const auto &) { return false; });
        auto animations = std::make_shared<DefaultAnimations>(argv[2]);
        ViewerPhysics engine(world, viewers, argv[1], animations, [](const auto &) {});
        auto object = SceneObject::primitive(Uuid::random(), {50, 50, 30}, {1, 2, 4});
        object.flags = 1;
        object.physics.gravity = 0;
        object.velocity = {2, 0, 0};
        const auto root = world.add(object).value();
        auto child = SceneObject::primitive(root.owner, {1, 0, 0});
        child.parent_id = root.local_id;
        child = world.add(child).value();
        const auto step = [&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            const auto result = engine.poll();
            check(result.steps > 0 && result.physical_parts == 2, "Live linked ubODE actor did not step");
        };
        step();
        check(world.object(root.id)->position.x > 50 && bool(engine.mass_properties(root.id)),
              "Fixture did not create a moving physical actor");
        const auto before = world.object(root.id).value();
        ObjectTeleportState state;
        state.id = root.id;
        state.position = before.position;
        state.scale = before.scale;
        state.motion = {before.velocity, before.angular_velocity, before.acceleration, before.rotation};
        ObjectTeleportHost host;
        host.inside_region = [](Vector3) { return true; };
        host.cooldown = [](Uuid, Uuid, int) { return false; };
        host.land_at = [](float, float) { return std::shared_ptr<ObjectTeleportParcel>{}; };
        host.ground_height = [&](float x, float y) { return world.terrain().height_at(x, y); };
        host.store_motion = [&](ObjectTeleportMotion motion) {
            const auto stored = world.store_teleport_motion(before, motion);
            check(bool(stored), "Physical teleport motion store failed");
            return *stored;
        };
        host.terse_update = [&] {
            check(world.finish_object_teleport(before, state.position), "Physical teleport position store failed");
        };
        check(teleport_object(state, root.id, {100, 100, 30}, {0, 0, 1, 0}, 5, host) == 1,
              "Physical local teleport failed");
        step();
        step();
        const auto stopped = world.object(root.id).value();
        near(stopped.position, {100, 100, 30}, .002f, "Old actor motion reverted the teleport position");
        near(stopped.velocity, {}, .002f, "Stop-at-target did not stop the actual actor");
        check(std::abs(stopped.rotation.z) > .999f && std::abs(stopped.rotation.w) < .002f,
              "Teleport rotation did not reach the actual actor");
        const auto mass = engine.mass_properties(root.id)->mass;
        engine.apply_prim_impulse(root.id, {mass * 2, 0, 0}, false);
        step();
        check(world.object(root.id)->position.x > 100.01f, "Teleported actor no longer responds to impulses");
        const auto moving = world.object(root.id).value();
        world.set_prim_look_at(root.id, {{0, 0, .70710677f, .70710677f}, .5f, true});
        step();
        step();
        check(std::abs(world.object(root.id)->angular_velocity.z) > .001f,
              "Fixture did not activate the actual angular position motor");
        const ObjectTeleportMotion prepared{{}, {}, {}, {0, 0, .70710677f, .70710677f}};
        check(bool(world.store_crossing_teleport_motion(moving, prepared)), "Crossing actor preparation failed");
        step();
        check(!world.object(root.id)->look_at.enabled, "Teleport crossing retained the angular position motor");
        near(world.object(root.id)->velocity, {}, .002f, "Prepared crossing motion did not stop the actor");
        near(world.object(root.id)->angular_velocity, {}, .002f, "Stopped crossing actor retained angular motion");
        world.patch_transform(root.id, {}, moving.rotation, {});
        check(world.restore_rejected_crossing(root.id), "Physical crossing recovery failed");
        step();
        near(world.object(root.id)->velocity, {}, .002f, "Recovered actor retained linear motion");
        const auto recovered = world.object(root.id).value();
        check(std::abs(recovered.rotation.z) > .999f && std::abs(recovered.rotation.w) < .002f,
              "Recovered actor did not restore the source orientation");
        check(world.object(child.id)->position == child.position, "Physics teleport changed the child local offset");
        live_region(argv[1], animations);
        std::cout
            << "PASS: live linked ubODE teleport, stopped/rotated actor, post-teleport impulse and crossing recovery\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
