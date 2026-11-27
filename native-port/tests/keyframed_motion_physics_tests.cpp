// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/viewer_physics.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
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

void deferred_mesh(const std::filesystem::path &library, std::shared_ptr<const DefaultAnimations> animations) {
    World world(Uuid::random());
    world.terrain().clear_land(0);
    AgentCircuitManager circuits;
    ViewerRegion viewers(
        world, circuits, [](const auto &, auto) { return true; },
        [](const auto &, const auto &, const auto &) { return false; });
    ViewerPhysics::MeshReady ready;
    unsigned requests = 0, callbacks = 0;
    const auto asset = Uuid::random();
    ViewerPhysics::MeshLoader loader = [&](Uuid id, bool, ViewerPhysics::MeshReady completion) {
        check(id == asset, "Unexpected deferred mesh asset requested");
        ++requests;
        ready = std::move(completion);
    };
    ViewerPhysics engine(world, viewers, library, std::move(animations), [](const auto &) {}, {}, 20.f, {}, {}, loader);
    engine.set_physical_root_created([&](const SceneObject &) { ++callbacks; });
    auto object = SceneObject::primitive(Uuid::random(), {100, 100, 30});
    object.flags = 1;
    object.physics.gravity = 0;
    auto shape = PrimitiveBaseShapeCore::create([](const std::string &) {});
    shape.sculpt_entry = true;
    shape.set_sculpt_properties(5, asset);
    object.extra_params = shape.extra_params();
    const auto root = world.add(object).value();
    engine.poll();
    engine.poll();
    check(requests == 1 && ready && callbacks == 0 && !engine.prim_is_physical(root),
          "Deferred mesh emitted a callback before actor creation");
    auto mesh = std::make_shared<physics::AssetPhysicsMesh>();
    mesh->vertices = {{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {0, .5f, -.5f}, {0, 0, .5f}};
    mesh->indices = {0, 2, 1, 0, 1, 3, 1, 2, 3, 2, 0, 3};
    ready(mesh);
    engine.poll();
    check(callbacks == 1 && engine.prim_is_physical(root), "Ready mesh did not emit one successful creation callback");
    engine.poll();
    check(callbacks == 1, "Unchanged mesh actor repeated its creation callback");
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
        unsigned callbacks = 0;
        engine.set_physical_root_created([&](const SceneObject &root) {
            ++callbacks;
            check(engine.prim_is_physical(root), "Creation callback preceded the actual solver body");
            check(world.store_keyframed_properties(root, {}, Vector3{}, Vector3{}),
                  "Creation callback could not stop scene motion");
        });

        auto object = SceneObject::primitive(Uuid::random(), {50, 50, 30});
        object.flags = 1;
        object.physics.gravity = 0;
        object.velocity = {5, 0, 0};
        object.angular_velocity = {0, 0, 2};
        const auto dynamic = world.add(object).value();
        check(!engine.prim_is_physical(dynamic), "Physical flag reported an actor before creation");
        engine.poll();
        check(engine.prim_is_physical(dynamic) && engine.mass_properties(dynamic.id).has_value(),
              "Created dynamic solver body was not reported physical");
        check(callbacks == 1, "Dynamic creation did not notify exactly once");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        bool stepped = false;
        do {
            stepped = engine.poll().steps > 0;
            if (!stepped)
                std::this_thread::yield();
        } while (!stepped && std::chrono::steady_clock::now() < deadline);
        check(stepped, "Stopped actor fixture did not execute a solver step");
        const auto stopped = world.object(dynamic.id).value();
        check(stopped.position == object.position && stopped.velocity == Vector3{} &&
                  stopped.angular_velocity == Vector3{} && std::abs(stopped.rotation.z) < .00001f &&
                  std::abs(stopped.rotation.w - 1.f) < .00001f && callbacks == 1,
              "New actor advanced before callback-stopped motion reached the solver");

        const auto stationary = world.add(SceneObject::primitive(dynamic.owner, {60, 50, 30})).value();
        engine.poll();
        check(engine.mass_properties(stationary.id).has_value(), "Static actor fixture was not created");
        check(!engine.prim_is_physical(stationary), "Static solver actor was reported physical");
        auto missing = dynamic;
        missing.id = Uuid::random();
        check(!engine.prim_is_physical(missing), "Missing actor was reported physical");
        check(callbacks == 1, "Static or missing actor produced a creation callback");

        auto invalid = object;
        invalid.id = Uuid::random();
        invalid.velocity.x = std::numeric_limits<float>::quiet_NaN();
        invalid = world.add(invalid).value();
        engine.poll();
        check(!engine.prim_is_physical(invalid) && callbacks == 1,
              "Failed actor creation emitted a physical-root callback");
        world.remove(invalid.id);

        world.set_group_flags(dynamic.id, false, false, false);
        check(engine.prim_is_physical(world.object(dynamic.id).value()),
              "Persisted flag overrode a still-existing dynamic actor before synchronization");
        engine.poll();
        check(!engine.prim_is_physical(dynamic), "Dynamic-to-static actor change was not observed");
        check(callbacks == 1, "Static transition produced a dynamic creation callback");
        world.set_group_flags(dynamic.id, true, false, false);
        check(!engine.prim_is_physical(dynamic), "Physical flag overrode a still-existing static actor");
        engine.poll();
        check(engine.prim_is_physical(dynamic), "Static-to-dynamic actor change was not observed");
        check(callbacks == 2, "Static-to-dynamic rebuild did not notify exactly once");

        check(world.remove(dynamic.id), "Could not remove dynamic actor fixture");
        check(!engine.prim_is_physical(dynamic), "Deleted scene part retained actor eligibility");
        auto replacement = object;
        replacement.id = dynamic.id;
        replacement.local_id = 0;
        replacement = world.add(replacement).value();
        check(replacement.local_id != dynamic.local_id, "Replacement fixture reused its local ID");
        check(!engine.prim_is_physical(dynamic) && !engine.prim_is_physical(replacement),
              "Replacement UUID reused the previous instance's cached actor");
        engine.poll();
        check(engine.prim_is_physical(replacement) && !engine.prim_is_physical(dynamic),
              "Replacement actor identity was not isolated from the original instance");
        check(callbacks == 3, "Replacement actor did not notify exactly once");
        check(world.patch_transform(replacement.id, {}, {}, Vector3{1, 1, 1}),
              "Could not resize dynamic actor fixture");
        engine.poll();
        check(callbacks == 4 && engine.prim_is_physical(replacement),
              "Dynamic geometry rebuild did not emit one creation callback");
        engine.poll();
        check(callbacks == 4, "Unchanged rebuilt actor repeated its creation callback");
        deferred_mesh(argv[1], animations);
        std::cout << "PASS: effective dynamic/static/missing and replaced root physics actor eligibility\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
