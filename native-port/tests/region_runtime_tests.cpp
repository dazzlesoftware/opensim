#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/framework/agent_data.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/server/object_handler.hpp"
#include "opensim/network/sdk_llsd_binary.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>
using namespace opensim;
using namespace framework;
using namespace services;
using namespace scene;
struct FailingPresence : IPresenceService {
    NullPresenceStore store;
    PresenceService service{store};
    int region_logouts = 0;
    bool login(const std::string& u, const Uuid& s, const Uuid& secure) override {
        return service.login(u, s, secure);
    }
    bool logout(const Uuid& s) override {
        return service.logout(s);
    }
    bool logout_region(const Uuid& r) override {
        ++region_logouts;
        service.logout_region(r);
        throw std::runtime_error("controlled presence cleanup failure");
    }
    bool report(const Uuid& s, const Uuid& r) override {
        return service.report(s, r);
    }
    std::optional<PresenceInfo> agent(const Uuid& s) override {
        return service.agent(s);
    }
    std::vector<PresenceInfo> agents(const std::vector<std::string>& u) override {
        return service.agents(u);
    }
};
namespace {
const Uuid legacy_material_id("12345678-1234-1234-1234-123456789abc");
const Uuid empty_legacy_material_id("2ad49c0c-8ea9-45b6-b049-e4f4f8f97a98");
// Original LLSDxmlEncode2 serialization and FaceMaterial.genID for this fixture.
const Uuid generated_material("69eb6a5b-f21e-c9cc-f74f-cd9fa682fc55");
const Uuid object_animation_script("52522d70-4d31-45e5-953c-94c678f8a78c");
const Uuid physics_mesh_asset_id("52522d70-4d31-45e5-953c-94c678f8a78d");
const Uuid convex_mesh_asset_id("52522d70-4d31-45e5-953c-94c678f8a78e");

std::shared_ptr<AssetBase> make_physics_mesh_asset(bool convex = false) {
    using network::Value;
    const auto binary = [](std::initializer_list<std::uint8_t> bytes) {
        return network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(bytes));
    };
    Value mesh;
    mesh.kind = Value::Kind::Array;
    mesh.array.push_back(Value::object({
        {"Position", binary({0, 0, 0, 0, 0, 128, 255, 255, 0, 0, 0, 128,
                             255, 255, 255, 255, 0, 128, 0, 0, 255, 255, 0, 128})},
        {"TriangleList", binary({0, 0, 1, 0, 2, 0, 0, 0, 2, 0, 3, 0})}
    }));
    if (convex) {
        mesh = Value::object({
            {"BoundingVerts", binary({0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0,
                                      255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 0, 0,
                                      0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 255, 255,
                                      255, 255, 255, 255, 255, 255, 0, 0, 255, 255, 255, 255})}
        });
    }
    const auto compressed = network::serialize_zipped_llsd_binary(mesh);
    std::string header = "<?llsd/binary?>\n";
    const auto number = [&](std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
            header += static_cast<char>((value >> shift) & 255);
    };
    const auto key = [&](std::string_view name) {
        header += 'k';
        number(static_cast<std::uint32_t>(name.size()));
        header += name;
    };
    header += '{';
    number(1);
    key(convex ? "physics_convex" : "physics_mesh");
    header += '{';
    number(2);
    key("offset");
    header += 'i';
    number(0);
    key("size");
    header += 'i';
    number(static_cast<std::uint32_t>(compressed.size()));
    const auto bytes = header + "}}" + compressed;
    auto asset = std::make_shared<AssetBase>(convex ? convex_mesh_asset_id : physics_mesh_asset_id, "Physics platform",
                                            static_cast<std::int8_t>(AssetType::Mesh), "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    return asset;
}

void verify_unattended_object_physics(RegionRuntime& runtime) {
    const auto poll_until = [&](auto ready, const char* failure) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        do {
            runtime.poll();
            if (ready())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error(failure);
    };
    constexpr Vector3 start{220, 215, 25};
    const auto prim = SceneObject::primitive(Uuid::random(), start, {1, 1, 1});
    const float mesh_ground = runtime.world().terrain().height_at(222, 215);
    auto mesh_platform = SceneObject::primitive(prim.owner, {222, 215, mesh_ground + 2.f}, {2, 2, 1});
    auto mesh_shape = PrimitiveBaseShapeCore::create([](const std::string&) {});
    mesh_shape.sculpt_entry = true;
    mesh_shape.set_sculpt_properties(5, physics_mesh_asset_id);
    mesh_platform.extra_params = mesh_shape.extra_params();
    auto mesh_probe = SceneObject::primitive(prim.owner, {222, 215, mesh_ground + 4.f}, {.3f, .3f, .3f});
    mesh_probe.flags = 1u;
    if (!runtime.world().add(mesh_platform) || !runtime.world().add(mesh_probe))
        throw std::runtime_error("Could not rez the mesh collider fixture");
    poll_until([&] {
        const auto object = runtime.world().object(mesh_probe.id);
        return object && std::abs(object->position.z - (mesh_ground + 2.15f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "Probe did not land on the asset-backed triangle mesh");
    runtime.world().remove(mesh_platform.id);
    mesh_platform.id = Uuid::random();
    mesh_platform.physics.shape = 2;
    mesh_shape.set_sculpt_properties(5, convex_mesh_asset_id);
    mesh_platform.extra_params = mesh_shape.extra_params();
    if (!runtime.world().add(mesh_platform) ||
        !runtime.world().patch_transform(mesh_probe.id, Vector3{222, 215, mesh_ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(mesh_probe.id, {}, {}, {}))
        throw std::runtime_error("Could not rez the convex mesh fixture");
    poll_until([&] {
        const auto object = runtime.world().object(mesh_probe.id);
        return object && std::abs(object->position.z - (mesh_ground + 2.65f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "Probe did not land on the asset-backed convex hull");
    runtime.world().remove(mesh_platform.id);
    poll_until([&] {
        const auto object = runtime.world().object(mesh_probe.id);
        return object && object->position.z < mesh_ground + 1.f;
    }, "Removing the mesh prim did not remove its collider");
    runtime.world().remove(mesh_probe.id);
    mesh_platform.id = Uuid::random();
    mesh_platform.flags = 1u;
    if (!runtime.world().add(mesh_platform))
        throw std::runtime_error("Could not rez the dynamic asset mesh");
    poll_until([&] {
        const auto object = runtime.world().object(mesh_platform.id);
        return object && std::abs(object->position.z - (mesh_ground + .5f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "Dynamic asset mesh did not fall and settle on terrain");
    runtime.world().remove(mesh_platform.id);
    auto platform_root = SceneObject::primitive(prim.owner, {220, 213, 22.5f});
    platform_root.rotation = {0, 0, .70710678f, .70710678f};
    const auto platform = runtime.world().add(platform_root);
    if (!platform)
        throw std::runtime_error("Could not rez the static platform linkset");
    auto platform_child = SceneObject::primitive(prim.owner, {2, 0, 0}, {1.5f, 1.5f, 1});
    platform_child.parent_id = platform->local_id;
    if (!runtime.world().add(platform_child))
        throw std::runtime_error("Could not rez the static platform child");
    if (!runtime.world().add(prim))
        throw std::runtime_error("Could not rez the unattended physics fixture");
    runtime.world().set_group_flags(prim.id, true, false, false);
    poll_until([&] {
        const auto object = runtime.world().object(prim.id);
        return object && object->position.z < 24.8f && object->velocity.z < 0;
    }, "Physical box did not fall in the empty region");
    poll_until([&] {
        const auto object = runtime.world().object(prim.id);
        return object && std::abs(object->position.z - 23.5f) < .05f && std::abs(object->velocity.z) < .1f;
    }, "Falling box did not land on the rotated linkset's static child");

    if (!runtime.world().patch_transform(platform_child.id, Vector3{2, 0, -.5f}, {}, {}))
        throw std::runtime_error("Could not move the static platform child");
    poll_until([&] {
        const auto object = runtime.world().object(prim.id);
        return object && std::abs(object->position.z - 23.f) < .05f && std::abs(object->velocity.z) < .1f;
    }, "Moving a static child did not update its collision geometry");
    runtime.world().remove(platform->id);
    poll_until([&] {
        const auto object = runtime.world().object(prim.id);
        return object && object->position.z < 22.8f;
    }, "Removing the platform did not remove the collider");

    runtime.world().set_group_flags(prim.id, false, false, false);
    if (!runtime.world().patch_transform(prim.id, start, {}, {}) ||
        !runtime.world().update_motion(prim.id, {}, {}, {}))
        throw std::runtime_error("Could not move the disabled physics fixture");
    const auto stop = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    poll_until([&] { return std::chrono::steady_clock::now() >= stop; }, "Could not poll the disabled fixture");
    if (runtime.world().object(prim.id)->position != start)
        throw std::runtime_error("Disabled physics body overwrote an edit");
    runtime.world().remove(prim.id);

    const Vector3 suspended_position{220, 215, 28};
    auto suspended = SceneObject::primitive(prim.owner, suspended_position);
    suspended.flags = 1u;
    suspended.physics.gravity = 0.f;
    if (!runtime.world().add(suspended))
        throw std::runtime_error("Could not rez the zero-gravity prim");
    const auto settle_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    poll_until([&] { return std::chrono::steady_clock::now() >= settle_until; },
               "Could not poll the zero-gravity prim");
    const auto floating = runtime.world().object(suspended.id);
    if (!floating || floating->position != suspended_position || floating->velocity != Vector3{})
        throw std::runtime_error("Zero gravity did not suspend the physical prim");
    auto reversed_gravity = suspended.physics;
    reversed_gravity.gravity = -1.f;
    if (!runtime.world().update_extra_physics(suspended.id, reversed_gravity))
        throw std::runtime_error("Could not edit the physical prim's gravity modifier");
    poll_until([&] {
        const auto object = runtime.world().object(suspended.id);
        return object && object->position.z > suspended_position.z + .2f && object->velocity.z > 0.f;
    }, "Reversing gravity did not make the physical prim rise");
    runtime.world().set_prim_buoyancy(suspended.id, 2.f);
    poll_until([&] {
        const auto object = runtime.world().object(suspended.id);
        return object && object->velocity.z < -.2f;
    }, "Buoyancy did not combine with the gravity modifier");
    runtime.world().remove(suspended.id);

    auto compound_root = SceneObject::primitive(prim.owner, {220, 215, 26}, {1, 1, 1});
    compound_root.flags = 1u;
    const auto compound = runtime.world().add(compound_root);
    if (!compound)
        throw std::runtime_error("Could not rez the physical linkset root");
    std::vector<Uuid> feet;
    for (const float x : {-1.f, 1.f}) {
        auto foot = SceneObject::primitive(prim.owner, {x, 0, -1}, {1, 1, 1});
        foot.parent_id = compound->local_id;
        if (!runtime.world().add(foot))
            throw std::runtime_error("Could not rez a physical linkset child");
        feet.push_back(foot.id);
    }
    const float ground = runtime.world().terrain().height_at(220, 215);
    const auto rests_at = [&](float z) {
        const auto root = runtime.world().object(compound->id);
        return root && std::abs(root->position.z - z) < .05f && std::abs(root->velocity.z) < .1f;
    };
    poll_until([&] { return rests_at(ground + 1.5f); },
               "Physical linkset children did not support the root above terrain");
    for (std::size_t i = 0; i < feet.size(); ++i) {
        const float x = i == 0 ? -1.f : 1.f;
        const auto foot = runtime.world().object(feet[i]);
        if (!foot || foot->position != Vector3{x, 0, -1} ||
            !runtime.world().patch_transform(feet[i], Vector3{x, 0, -.5f}, {}, {}))
            throw std::runtime_error("Physical linkset did not preserve its child-local transforms");
    }
    poll_until([&] { return rests_at(ground + 1.f); },
               "Editing linkset children did not rebuild the compound body");
    runtime.world().set_prim_buoyancy(feet.front(), 2.f);
    const Vector3 force{300, 0, 0};
    runtime.world().set_prim_force(feet.front(), force);
    const Vector3 torque{0, 0, 25};
    runtime.world().set_prim_torque(feet.front(), torque, false);
    if (runtime.world().prim_force(compound->id) != force ||
        runtime.world().prim_force(feet.back()) != force)
        throw std::runtime_error("Linkset parts did not share the root's applied force");
    if (runtime.world().prim_torque(feet.back()) != torque)
        throw std::runtime_error("Linkset parts did not share the root's applied torque");
    poll_until([&] {
        const auto root = runtime.world().object(compound->id);
        return root && root->position.z > ground + 1.2f && root->velocity.z > 0.f &&
               root->position.x > compound_root.position.x + .2f && root->velocity.x > 0.f &&
               root->angular_velocity.z > .1f;
    }, "Buoyancy, force and torque from a child did not move the linkset");
    runtime.world().remove(compound->id);

    auto sphere = SceneObject::primitive(prim.owner, {220, 215, 25}, {1, 1, 1});
    sphere.flags = 1u;
    sphere.shape.path_curve = 32;
    sphere.shape.profile_curve = 5;
    if (!runtime.world().add(sphere))
        throw std::runtime_error("Could not rez the physical sphere");
    poll_until([&] {
        const auto object = runtime.world().object(sphere.id);
        return object && std::abs(object->position.z - (ground + .5f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "Physical sphere did not settle on the terrain");
    if (!runtime.world().patch_transform(sphere.id, Vector3{220, 215, 25}, {}, Vector3{2, 1.5f, 1}))
        throw std::runtime_error("Could not stretch the physical sphere");
    poll_until([&] {
        const auto object = runtime.world().object(sphere.id);
        return object && std::abs(object->position.z - (ground + .5f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "Stretched physical sphere did not settle on the terrain");
    runtime.world().remove(sphere.id);

    auto cylinder = SceneObject::primitive(prim.owner, {220, 215, 25}, {1, 2, 1});
    cylinder.flags = 1u;
    cylinder.shape.profile_curve = 0;
    cylinder.physics.shape = 2;
    if (!runtime.world().add(cylinder))
        throw std::runtime_error("Could not rez the physical cylinder");
    poll_until([&] {
        const auto object = runtime.world().object(cylinder.id);
        return object && std::abs(object->position.z - (ground + .5f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "Physical cylinder did not settle on the terrain");
    runtime.world().remove(cylinder.id);

    auto pipe = SceneObject::primitive(prim.owner, {220, 215, ground + 2.f}, {2, 2, 1});
    pipe.shape.profile_curve = 0x10;
    auto probe = SceneObject::primitive(prim.owner, {220, 215, ground + 4.f}, {.2f, .2f, .2f});
    probe.flags = 1u;
    if (!runtime.world().add(pipe) || !runtime.world().add(probe))
        throw std::runtime_error("Could not rez the hollow cylinder fixture");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.6f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "Probe did not settle on the cylinder cap");
    pipe.shape.path_end = 12500;
    if (!runtime.world().update_shape(pipe.id, pipe.shape))
        throw std::runtime_error("Could not cut the cylinder path");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.35f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "Path cut did not lower the cylinder cap");
    pipe.shape.profile_hollow = 25000;
    if (!runtime.world().update_shape(pipe.id, pipe.shape))
        throw std::runtime_error("Could not hollow out the cylinder");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + .1f)) < .05f;
    }, "Hollowing the cylinder did not open the collision mesh");
    pipe.shape.profile_hollow = 0;
    pipe.shape.profile_end = 25000;
    if (!runtime.world().update_shape(pipe.id, pipe.shape) ||
        !runtime.world().patch_transform(probe.id, Vector3{220, 214.4f, ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(probe.id, {}, {}, {}))
        throw std::runtime_error("Could not set up the cylinder profile cut");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + .1f)) < .05f;
    }, "The removed cylinder profile sector did not open after a live edit");
    pipe.shape.profile_curve = 0x11;
    pipe.shape.profile_end = 0;
    pipe.shape.profile_hollow = 35000;
    if (!runtime.world().update_shape(pipe.id, pipe.shape) ||
        !runtime.world().patch_transform(probe.id, Vector3{220.8f, 215.8f, ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(probe.id, {}, {}, {}))
        throw std::runtime_error("Could not set up the circular opening in a box");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.35f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "A square corner did not support the probe after switching to a circular opening");
    pipe.shape.profile_curve = 0x13;
    if (!runtime.world().update_shape(pipe.id, pipe.shape) ||
        !runtime.world().patch_transform(probe.id, Vector3{220.6f, 215, ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(probe.id, {}, {}, {}))
        throw std::runtime_error("Could not set up the circular opening in a triangular prism");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.35f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "The triangular cap did not support the probe after switching the exterior");
    pipe.shape.profile_curve = 1;
    pipe.shape.path_curve = 32;
    pipe.shape.path_end = 0;
    pipe.shape.path_scale_y = 175;
    pipe.shape.profile_hollow = 0;
    if (!runtime.world().update_shape(pipe.id, pipe.shape) ||
        !runtime.world().patch_transform(pipe.id, {}, Quaternion{0, -.70710678f, 0, .70710678f}, Vector3{1, 2, 2}) ||
        !runtime.world().patch_transform(probe.id, Vector3{220, 215.75f, ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(probe.id, {}, {}, {}))
        throw std::runtime_error("Could not set up the swept square tube");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.6f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "The curved square tube did not support the probe after a live shape edit");
    if (!runtime.world().patch_transform(probe.id, Vector3{220.75f, 215, ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(probe.id, {}, {}, {}))
        throw std::runtime_error("Could not move the probe onto the tube sector to be cut");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.6f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "The uncut tube did not support the probe on the sector to be cut");
    pipe.shape.path_end = 25000;
    if (!runtime.world().update_shape(pipe.id, pipe.shape))
        throw std::runtime_error("Could not cut the curved tube path");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + .1f)) < .05f;
    }, "A live curved path cut did not remove the tube sector");
    pipe.shape.path_taper_x = 100;
    if (!runtime.world().update_shape(pipe.id, pipe.shape) ||
        !runtime.world().patch_transform(probe.id, Vector3{219.25f, 215, ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(probe.id, {}, {}, {}))
        throw std::runtime_error("Could not taper the curved tube");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.475f)) < .06f &&
               std::abs(object->velocity.z) < .1f;
    }, "A live taper did not lower the curved tube's contact surface");
    pipe.shape.path_taper_x = 0;
    pipe.shape.path_skew = 50;
    if (!runtime.world().update_shape(pipe.id, pipe.shape))
        throw std::runtime_error("Could not skew the curved tube");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.225f)) < .06f &&
               std::abs(object->velocity.z) < .1f;
    }, "A live skew did not shift the curved tube's contact surface");
    pipe.shape.path_skew = 0;
    pipe.shape.path_shear_x = 50;
    if (!runtime.world().update_shape(pipe.id, pipe.shape) ||
        !runtime.world().patch_transform(probe.id, Vector3{219.25f, 215, ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(probe.id, {}, {}, {}))
        throw std::runtime_error("Could not shear the curved tube");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.7875f)) < .06f &&
               std::abs(object->velocity.z) < .1f;
    }, "A live X shear did not shift the curved tube's contact surface");
    pipe.shape.path_shear_x = 0;
    pipe.shape.path_revolutions = 50;
    if (!runtime.world().update_shape(pipe.id, pipe.shape) ||
        !runtime.world().patch_transform(probe.id, Vector3{220.53f, 214.47f, ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(probe.id, {}, {}, {}))
        throw std::runtime_error("Could not extend the curved tube sweep");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.6f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "A live revolution edit did not add contact beyond the original path");
    pipe.shape.profile_end = 37500;
    if (!runtime.world().update_shape(pipe.id, pipe.shape) ||
        !runtime.world().patch_transform(probe.id, Vector3{220.39f, 214.61f, ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(probe.id, {}, {}, {}))
        throw std::runtime_error("Could not cut the cross-section of the curved tube");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + .1f)) < .05f;
    }, "A live profile cut did not clear the inner part of the curved tube");
    pipe.shape.profile_end = 0;
    pipe.shape.path_revolutions = 0;
    pipe.shape.profile_hollow = 35000;
    if (!runtime.world().update_shape(pipe.id, pipe.shape) ||
        !runtime.world().patch_transform(probe.id, Vector3{219.25f, 215, ground + 4.f}, {}, {}) ||
        !runtime.world().update_motion(probe.id, {}, {}, {}))
        throw std::runtime_error("Could not hollow the curved tube");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && std::abs(object->position.z - (ground + 2.6f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "A live curved hollow edit lost the tube's outer wall");
    pipe.shape.profile_curve = 0x11;
    if (!runtime.world().update_shape(pipe.id, pipe.shape) ||
        !runtime.world().patch_transform(probe.id, Vector3{220.22f, 215.75f, ground + 2.f}, {},
                                         Vector3{.06f, .06f, .06f}) ||
        !runtime.world().update_motion(probe.id, {-4, 0, 0}, {}, {}))
        throw std::runtime_error("Could not change the curved tube to a circular bore");
    poll_until([&] {
        const auto object = runtime.world().object(probe.id);
        return object && object->position.x < 219.9f && object->position.z > ground + 1.5f;
    }, "A live mixed hollow edit did not leave the curved tube bore open");
    runtime.world().remove(pipe.id);
    runtime.world().remove(probe.id);

    auto twisted = SceneObject::primitive(prim.owner, {220, 215, ground + 2.f}, {2, 2, 2});
    twisted.shape.path_twist = 25;
    auto twist_probe = SceneObject::primitive(prim.owner, {221.2f, 215, ground + 5.f}, {.2f, .2f, .2f});
    twist_probe.flags = 1u;
    if (!runtime.world().add(twisted) || !runtime.world().add(twist_probe))
        throw std::runtime_error("Could not rez the twisted collision fixture");
    poll_until([&] {
        const auto object = runtime.world().object(twist_probe.id);
        return object && std::abs(object->position.z - (ground + 3.1f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "The twisted cap did not support the probe");
    twisted.shape.path_twist = 0;
    if (!runtime.world().update_shape(twisted.id, twisted.shape))
        throw std::runtime_error("Could not straighten the twisted prim");
    poll_until([&] {
        const auto object = runtime.world().object(twist_probe.id);
        return object && std::abs(object->position.z - (ground + .1f)) < .05f;
    }, "Straightening the prim did not remove the twisted corner");
    twisted.shape.profile_curve = 3;
    if (!runtime.world().update_shape(twisted.id, twisted.shape) ||
        !runtime.world().patch_transform(twist_probe.id, Vector3{220.65f, 215, ground + 5.f}, {}, {}) ||
        !runtime.world().update_motion(twist_probe.id, {}, {}, {}))
        throw std::runtime_error("Could not change the platform to a triangular prism");
    poll_until([&] {
        const auto object = runtime.world().object(twist_probe.id);
        return object && std::abs(object->position.z - (ground + 3.1f)) < .05f &&
               std::abs(object->velocity.z) < .1f;
    }, "The triangular cap did not support the probe after the live shape change");
    runtime.world().remove(twisted.id);
    runtime.world().remove(twist_probe.id);

    auto edge = SceneObject::primitive(prim.owner, {255.5f, 215, 25});
    edge.flags = 1u;
    edge.velocity = {8, 0, 0};
    edge.die_at_edge = true;
    if (!runtime.world().add(edge))
        throw std::runtime_error("Could not rez the physics edge fixture");
    poll_until([&] { return !runtime.world().object(edge.id); },
               "Solver-driven region exit did not trigger die-at-edge");

    auto rejected = SceneObject::primitive(prim.owner, {255.5f, 215, 25});
    rejected.flags = 1u;
    rejected.velocity = {8, 0, 0};
    if (!runtime.world().add(rejected))
        throw std::runtime_error("Could not rez the rejected physics crossing fixture");
    poll_until([&] {
        const auto object = runtime.world().object(rejected.id);
        return object && object->velocity.x == 0 && object->position.x < 256;
    }, "Rejected physics crossing did not stop the box at the region edge");
    runtime.world().remove(rejected.id);
}

void verify_transferred_seat(RegionRuntime& runtime, GridRegion& region, IPresenceService& presence,
                             const network::UdpEndpoint& endpoint, Uuid attachment_script,
                             std::uint32_t teleport_flags, std::string_view expected_description) {
    namespace udp = network::lludp;
    auto circuit = std::make_shared<AgentCircuitData>();
    circuit->agent_id = Uuid::random();
    circuit->session_id = Uuid::random();
    circuit->secure_session_id = Uuid::random();
    circuit->circuit_code = 45;
    circuit->caps_path = "transferred-seat";
    circuit->start_position = {160, 160, 40};
    presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id);

    auto root = runtime.world().add(SceneObject::primitive(circuit->agent_id, circuit->start_position));
    if (!root)
        throw std::runtime_error("Could not create the transferred seat linkset");
    auto child = SceneObject::primitive(circuit->agent_id, {2, 0, 0});
    child.parent_id = root->local_id;
    child.rotation = {0, 0, 1, 0};
    const auto part = runtime.world().add(child);
    if (!part)
        throw std::runtime_error("Could not create the transferred child seat");
    // Keep the stand-up pose and script-control assertions independent of seat collisions.
    runtime.world().set_group_flags(root->id, false, false, true);

    auto source = region;
    source.id = Uuid::random();
    LoginTransferContext context;
    std::string reason;
    if (!runtime.create_agent(&source, region, circuit, teleport_flags, context, reason))
        throw std::runtime_error("Could not reserve the transferred seat's circuit: " + reason);
    auto update = std::make_shared<AgentData>();
    update->agent_id = circuit->agent_id;
    update->session_id = circuit->session_id;
    update->circuit_code = circuit->circuit_code;
    update->position = circuit->start_position;
    update->parent_part = part->id;
    update->sit_offset = {1, 0, 0};
    update->body_rotation = {0, 0, 0, 1};
    update->crossing_flags = 7;
    ViewerAgent source_avatar;
    source_avatar.circuit = circuit;
    source_avatar.local_id = runtime.world().allocate_local_id();
    auto attachment = SceneObject::primitive(circuit->agent_id, {0.125f, 0.25f, 0.5f});
    attachment.is_attachment = true;
    attachment.parent_id = source_avatar.local_id;
    attachment.shape.state = 6;
    attachment.from_inventory_item = Uuid::random();
    const auto worn = runtime.world().add(attachment, false);
    if (!worn)
        throw std::runtime_error("Could not create the source attachment");
    auto attachment_child = SceneObject::primitive(circuit->agent_id, {0.25f, 0, 0});
    attachment_child.parent_id = worn->local_id;
    attachment_child.rotation = {0, 0, 1, 0};
    if (!runtime.world().add(attachment_child, false))
        throw std::runtime_error("Could not create the source attachment child");
    auto contents = std::make_shared<TaskInventoryItem>();
    contents->item_id = Uuid::random();
    contents->parent_id = contents->parent_part_id = attachment_child.id;
    contents->asset_id = attachment_script;
    contents->owner_id = contents->creator_id = circuit->agent_id;
    contents->type = contents->inventory_type = 10;
    contents->script_running = true;
    contents->name = "Transferred attachment script";
    if (teleport_flags) {
        update->controllers = std::make_shared<std::vector<std::shared_ptr<ControllerData>>>();
        update->controllers->push_back(std::make_shared<ControllerData>(attachment.id, contents->item_id, 1, 1));
    }
    runtime.restore_task_inventory(attachment_child.id, {contents});
    const auto source_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < source_deadline) {
        runtime.poll();
        if (runtime.world().object(attachment_child.id)->description == "Attachment checkpoint")
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (runtime.world().object(attachment_child.id)->description != "Attachment checkpoint")
        throw std::runtime_error("Attachment script did not reach its sleep checkpoint");
    if (teleport_flags) {
        contents->permissions_granter = circuit->agent_id;
        contents->permissions_mask = 4;
    }
    runtime.snapshot_attachments(source_avatar, *update);
    if (!update->attachment_object_states || update->attachment_object_states->size() != 1)
        throw std::runtime_error("Expected one attachment state snapshot");
    const auto attachment_state = update->attachment_object_states->get(0);
    if (!attachment_state || attachment_state->find("<NativeCheckpoint>TFNDASAA") == std::string::npos ||
        attachment_state->find("<NativeListeners>") == std::string::npos)
        throw std::runtime_error("Attachment crossing must retain the version-one VM checkpoint");
    runtime.world().remove(attachment.id);
    runtime.poll();
    auto wire = update->pack();
    auto transfer_scene = runtime.get_scene(region.id);
    update = std::make_shared<AgentData>();
    update->unpack(wire, transfer_scene.get());
    if (!runtime.update_agent(region, update, context))
        throw std::runtime_error("Could not apply the transferred seat state");

    network::UdpSocket viewer({"127.0.0.1", 0}, [](const auto&, auto) {});
    viewer.start();
    auto connect = udp::make_packet("UseCircuitCode");
    connect.sequence = 1;
    auto& credentials = connect.block("CircuitCode");
    credentials["Code"] = std::uint64_t{circuit->circuit_code};
    credentials["ID"] = circuit->agent_id;
    credentials["SessionID"] = circuit->session_id;
    if (!viewer.send_to(endpoint, udp::encode_datagram(connect)))
        throw std::runtime_error("Could not open the transferred seat's viewer circuit");
    auto movement = udp::make_packet("CompleteAgentMovement");
    movement.sequence = 2;
    auto& identity = movement.block("AgentData");
    identity["AgentID"] = circuit->agent_id;
    identity["SessionID"] = circuit->session_id;
    identity["CircuitCode"] = std::uint64_t{circuit->circuit_code};
    if (!viewer.send_to(endpoint, udp::encode_datagram(movement)))
        throw std::runtime_error("Could not complete movement for the transferred seat");
    enum class InputPhase { seated, standing, blocking_backward, captured, releasing, walking, listening };
    auto phase = InputPhase::seated;
    std::uint32_t input_sequence = 2;
    Vector3 standing_position;
    std::chrono::steady_clock::time_point capture_started;
    const auto send_input = [&](std::uint32_t flags) {
        auto input = udp::make_packet("AgentUpdate");
        input.sequence = ++input_sequence;
        auto& agent = input.block("AgentData");
        agent["AgentID"] = circuit->agent_id;
        agent["SessionID"] = circuit->session_id;
        agent["ControlFlags"] = std::uint64_t{flags};
        if (!viewer.send_to(endpoint, udp::encode_datagram(input)))
            throw std::runtime_error("Could not send viewer input to the attachment script");
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.poll();
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        const auto restored = runtime.world().object(attachment.id);
        const auto restored_child = runtime.world().object(attachment_child.id);
        const auto inventory = runtime.snapshot_task_inventory(attachment_child.id);
        if (restored_child && restored_child->description == "Attachment restarted")
            throw std::runtime_error("Transferred attachment script restarted its state_entry event");
        if (restored_child && restored_child->description == "Attachment lost permission")
            throw std::runtime_error("Transferred attachment script lost its control grant");
        if (avatar && avatar->root && restored && restored_child && inventory) {
            if (!restored->is_attachment || restored->parent_id != avatar->local_id ||
                restored->position != attachment.position || restored->shape.state != attachment.shape.state ||
                restored->from_inventory_item != attachment.from_inventory_item || restored->attached_to_backup ||
                inventory->size() != 1 || inventory->front().item_id != contents->item_id)
                throw std::runtime_error("Transferred attachment lost its wearer, offset, or inventory");
            if (restored_child->parent_id != restored->local_id || restored_child->attached_to_backup ||
                restored_child->position != attachment_child.position ||
                restored_child->rotation != attachment_child.rotation ||
                runtime.world().prim_count(restored->id, true) != 2 ||
                runtime.world().prim_count(restored_child->id, false) != 0 ||
                runtime.world().link_key(restored->id, 2) != restored_child->id ||
                runtime.world().link_number(restored_child->id) != 2)
                throw std::runtime_error("Transferred attachment child lost its link or transform");
            if (phase == InputPhase::seated && (!avatar->seat || avatar->seat->part != part->id ||
                avatar->seat->parent_local != root->local_id || !avatar->seat->explicit_target ||
                avatar->seat->offset != Vector3{1, 0, 0} || avatar->position != Vector3{161, 160, 40} ||
                avatar->seat->rotation != Quaternion{0, 0, 1, 0}))
                throw std::runtime_error("Transferred avatar did not restore its linked seat transform");
            if (phase == InputPhase::seated && restored_child->description == expected_description) {
                AgentData outgoing;
                runtime.snapshot_attachments(*avatar, outgoing);
                if (!outgoing.controllers || outgoing.controllers->size() != 1 ||
                    outgoing.controllers->front()->object_id != attachment.id ||
                    outgoing.controllers->front()->item_id != contents->item_id ||
                    outgoing.controllers->front()->ignore_controls != 1 ||
                    outgoing.controllers->front()->event_controls != 1)
                    throw std::runtime_error("Could not snapshot active control registration");
                send_input(0x10000u);
                phase = InputPhase::standing;
            } else if (phase == InputPhase::standing && !avatar->seat) {
                standing_position = avatar->position;
                capture_started = std::chrono::steady_clock::now();
                if (!teleport_flags) {
                    send_input(2);
                    phase = InputPhase::blocking_backward;
                } else {
                    send_input(1);
                    phase = InputPhase::captured;
                }
            } else if (phase == InputPhase::blocking_backward &&
                       std::chrono::steady_clock::now() - capture_started >= std::chrono::milliseconds(500)) {
                if (std::hypot(avatar->position.x - standing_position.x,
                               avatar->position.y - standing_position.y) > 0.05f)
                    throw std::runtime_error("Replacing a registration dropped the viewer's blocked backward input");
                send_input(1);
                capture_started = std::chrono::steady_clock::now();
                phase = InputPhase::captured;
            } else if (phase == InputPhase::captured && restored_child->description == "Attachment held" &&
                       std::chrono::steady_clock::now() - capture_started >= std::chrono::milliseconds(500)) {
                if (std::hypot(avatar->position.x - standing_position.x,
                               avatar->position.y - standing_position.y) > 0.05f)
                    throw std::runtime_error("Captured forward input moved the avatar");
                AgentData outgoing;
                runtime.snapshot_attachments(*avatar, outgoing);
                if (outgoing.control_flags & 1)
                    throw std::runtime_error("Captured forward input leaked into the agent transfer");
                send_input(0);
                phase = InputPhase::releasing;
            } else if (phase == InputPhase::releasing && restored_child->description == "Attachment released 0") {
                send_input(1);
                phase = InputPhase::walking;
            }
            if (phase == InputPhase::walking &&
                std::hypot(avatar->position.x - standing_position.x,
                           avatar->position.y - standing_position.y) >= 0.25f) {
                auto chat = udp::make_packet("ChatFromViewer");
                chat.sequence = ++input_sequence;
                chat.block("AgentData")["AgentID"] = circuit->agent_id;
                chat.block("AgentData")["SessionID"] = circuit->session_id;
                auto& fields = chat.block("ChatData");
                fields["Channel"] = std::int64_t{7195};
                fields["Type"] = std::uint64_t{1};
                const std::string text = "Transferred listener";
                fields["Message"] = std::vector<std::uint8_t>(text.begin(), text.end());
                if (!viewer.send_to(endpoint, udp::encode_datagram(chat)))
                    throw std::runtime_error("Could not send chat to the transferred listener");
                phase = InputPhase::listening;
            }
            if (phase != InputPhase::listening || restored_child->description != "Attachment heard chat") {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            runtime.close_agent_session(circuit->agent_id, circuit->session_id.str());
            if (runtime.world().object(attachment.id) || runtime.world().object(attachment_child.id))
                throw std::runtime_error("Closing the avatar left attachment parts in the region");
            runtime.world().remove(root->id);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Transferred seated viewer or its attachment script did not finish arriving");
}

Uuid verify_viewer_packets(RegionRuntime& runtime, const server::RegistryCapabilityListener& listener,
                           const network::UdpEndpoint& endpoint,
                           const AgentCircuitData& circuit, const AssetBase& covenant, Uuid library_item,
                           const std::function<std::shared_ptr<AssetBase>(Uuid)>& uploaded_asset,
                           InventoryService& inventory, const SceneObject& hub, float expected_arrival_x,
                           const std::atomic<unsigned>& parcel_refreshes, Uuid unsit_asset, Uuid force_sit_asset,
                           Uuid parcel_owner, Uuid parcel_group, Uuid administrator,
                           std::chrono::steady_clock::time_point& dwell_time, Uuid dwell_parcel,
                           const std::vector<std::pair<Uuid, float>>& dwell_writes, bool use_trash_on_delete,
                           std::atomic<bool>& grant_viewer_admin, Uuid duplicate_script_asset) {
    namespace udp = network::lludp;
    std::mutex mutex;
    std::vector<udp::Packet> received;
    std::atomic<std::uint32_t> sequence = 0;
    network::UdpSocket viewer({"127.0.0.1", 0}, [&](const auto& from, auto bytes) {
        auto packet = udp::decode_datagram(bytes);
        if (packet.reliable) {
            auto ack = udp::make_packet("PacketAck");
            ack.sequence = ++sequence;
            auto block = udp::make_block(ack.spec->blocks.front());
            block["ID"] = std::uint64_t(packet.sequence);
            ack.blocks["Packets"].push_back(std::move(block));
            viewer.send_to(from, udp::encode_datagram(ack));
        }
        std::lock_guard lock(mutex);
        received.push_back(std::move(packet));
    });
    viewer.start();
    const auto transmit = [&](udp::Packet packet) {
        packet.sequence = ++sequence;
        if (!viewer.send_to(endpoint, udp::encode_datagram(packet)))
            throw std::runtime_error("Viewer could not send asset transfer fixture packet");
    };
    const auto wait = [&](auto predicate, const char* error) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            runtime.poll();
            {
                std::lock_guard lock(mutex);
                if (predicate(received))
                    return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error(error);
    };
    auto use = udp::make_packet("UseCircuitCode");
    auto& credentials = use.block("CircuitCode");
    credentials["Code"] = std::uint64_t{circuit.circuit_code};
    credentials["ID"] = circuit.agent_id;
    credentials["SessionID"] = circuit.session_id;
    transmit(std::move(use));
    wait(
        [](const auto& packets) {
            return std::any_of(packets.begin(), packets.end(),
                               [](const auto& packet) { return packet.spec->name == "RegionHandshake"; });
        },
        "Viewer did not connect for covenant transfer");
    const auto arriving_viewer = runtime.viewers().agent(circuit.agent_id);
    if (!arriving_viewer || arriving_viewer->camera_position != Vector3{40, 50, 60} ||
        arriving_viewer->camera_rotation != Quaternion{0, 0, 1, 0})
        throw std::runtime_error("Viewer did not restore the transferred camera before its first update");
    if (arriving_viewer->velocity != Vector3{1, 2, 3} ||
        arriving_viewer->rotation != Quaternion{0, 0, 1, 0} || !arriving_viewer->always_run)
        throw std::runtime_error("Viewer did not restore the transferred movement state");
    auto handshake = udp::make_packet("RegionHandshakeReply");
    handshake.block("AgentData")["AgentID"] = circuit.agent_id;
    handshake.block("AgentData")["SessionID"] = circuit.session_id;
    transmit(std::move(handshake));

    for (const auto crc : {0u, 123u}) {
        auto request = udp::make_packet("MuteListRequest");
        request.block("AgentData")["AgentID"] = circuit.agent_id;
        request.block("AgentData")["SessionID"] = circuit.session_id;
        request.block("MuteData")["MuteCRC"] = std::uint64_t{crc};
        transmit(std::move(request));
        wait(
            [&](const auto& packets) {
                for (const auto& reply : packets) {
                    if (crc != 0 && reply.spec->name == "UseCachedMuteList" &&
                        std::get<Uuid>(reply.block("AgentData").at("AgentID")) == circuit.agent_id)
                        return true;
                    if (crc == 0 && reply.spec->name == "GenericMessage") {
                        const auto& bytes =
                            std::get<std::vector<std::uint8_t>>(reply.block("MethodData").at("Method"));
                        if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                                "emptymutelist" &&
                            reply.blocks.at("ParamList").size() == 1)
                            return true;
                    }
                }
                return false;
            },
            "Viewer did not receive the disabled mute-list response");
    }

    runtime.set_estate_covenant(covenant.full_id(), 123);
    auto covenant_request = udp::make_packet("EstateCovenantRequest");
    auto& identity = covenant_request.block("AgentData");
    identity["AgentID"] = circuit.agent_id;
    identity["SessionID"] = circuit.session_id;
    transmit(std::move(covenant_request));
    wait(
        [&](const auto& packets) {
            for (const auto& packet : packets) {
                if (packet.spec->name != "EstateCovenantReply")
                    continue;
                const auto& data = packet.block("Data");
                if (std::get<Uuid>(data.at("CovenantID")) == covenant.full_id() &&
                    std::get<std::uint64_t>(data.at("CovenantTimestamp")) == 123)
                    return true;
            }
            return false;
        },
        "Viewer did not receive the configured covenant");

    const auto verify = [&](std::int64_t source, const std::vector<std::uint8_t>& params,
                            const std::vector<std::uint8_t>& expected_params) {
        const auto id = Uuid::random();
        auto transfer = udp::make_packet("TransferRequest");
        auto& request = transfer.block("TransferInfo");
        request["TransferID"] = id;
        request["ChannelType"] = std::int64_t{2};
        request["SourceType"] = source;
        request["Params"] = params;
        transmit(std::move(transfer));
        wait(
            [&](const auto& packets) {
                bool header = false;
                std::map<int, std::vector<std::uint8_t>> chunks;
                for (const auto& packet : packets) {
                    if (packet.spec->name == "TransferInfo") {
                        const auto& info = packet.block("TransferInfo");
                        if (std::get<Uuid>(info.at("TransferID")) != id)
                            continue;
                        if (std::get<std::int64_t>(info.at("Status")) != 0 ||
                            std::get<std::int64_t>(info.at("Size")) !=
                                static_cast<std::int64_t>(covenant.data->size()) ||
                            std::get<std::vector<std::uint8_t>>(info.at("Params")) != expected_params)
                            throw std::runtime_error(
                                "Covenant or library transfer header differs from legacy");
                        header = true;
                    } else if (packet.spec->name == "TransferPacket") {
                        const auto& data = packet.block("TransferData");
                        if (std::get<Uuid>(data.at("TransferID")) != id)
                            continue;
                        const auto index = std::get<std::int64_t>(data.at("Packet"));
                        if (index < 0 || index > 1 || std::get<std::int64_t>(data.at("Status")) != index)
                            throw std::runtime_error("Covenant or library transfer chunk has invalid status");
                        chunks[static_cast<int>(index)] =
                            std::get<std::vector<std::uint8_t>>(data.at("Data"));
                    }
                }
                if (!header || chunks.size() != 2)
                    return false;
                auto assembled = chunks.at(0);
                assembled.insert(assembled.end(), chunks.at(1).begin(), chunks.at(1).end());
                if (chunks.at(0).size() != 600 || assembled != *covenant.data)
                    throw std::runtime_error("Covenant or library transfer changed asset bytes");
                return true;
            },
            "Covenant or library transfer did not complete");
    };
    std::vector<std::uint8_t> estate_params(20);
    estate_params[16] = static_cast<std::uint8_t>(AssetType::Notecard);
    verify(4, {}, estate_params);
    std::vector<std::uint8_t> library_params(64);
    for (const auto& id : {library_item, covenant.full_id()})
        library_params.insert(library_params.end(), id.bytes().begin(), id.bytes().end());
    library_params.insert(library_params.end(), {static_cast<std::uint8_t>(AssetType::Notecard), 0, 0, 0});
    verify(3, library_params, library_params);

    const auto upload = [&](Uuid transaction, std::vector<std::uint8_t> bytes, bool local = true) {
        auto request = udp::make_packet("AssetUploadRequest");
        auto& fields = request.block("AssetBlock");
        fields["TransactionID"] = transaction;
        fields["Type"] = std::int64_t{static_cast<std::int8_t>(AssetType::Notecard)};
        fields["StoreLocal"] = local;
        fields["Tempfile"] = true;
        fields["AssetData"] = std::move(bytes);
        transmit(std::move(request));
    };
    const auto check_uploaded = [&](Uuid id, const std::vector<std::uint8_t>& expected) {
        wait(
            [&](const auto& packets) {
                return std::any_of(packets.begin(), packets.end(), [&](const auto& packet) {
                    if (packet.spec->name != "AssetUploadComplete")
                        return false;
                    const auto& result = packet.block("AssetBlock");
                    return std::get<Uuid>(result.at("UUID")) == id && std::get<bool>(result.at("Success"));
                });
            },
            "Viewer did not receive successful asset upload completion");
        const auto asset = uploaded_asset(id);
        if (!asset || !asset->data || *asset->data != expected || !asset->local() || !asset->temporary() ||
            asset->creator_id() != circuit.agent_id.str())
            throw std::runtime_error("Uploaded asset storage differs from the viewer request");
    };
    upload(Uuid("28731084-aa42-4fd8-8014-2027257d21c1"), {7, 0, 9});
    check_uploaded(Uuid("83f2cb8e-8d5a-1a33-22e2-f4b5ba4bb964"), {7, 0, 9});

    const Uuid multipart_id("1104ffd3-7e88-7876-be92-03f28163512b");
    upload(Uuid("28731084-aa42-4fd8-8014-2027257d21c2"), {});
    std::uint64_t xfer_id = 0;
    wait(
        [&](const auto& packets) {
            for (const auto& packet : packets) {
                if (packet.spec->name != "RequestXfer")
                    continue;
                const auto& fields = packet.block("XferID");
                if (std::get<Uuid>(fields.at("VFileID")) == multipart_id) {
                    xfer_id = std::get<std::uint64_t>(fields.at("ID"));
                    return xfer_id != 0;
                }
            }
            return false;
        },
        "Viewer did not receive the asset upload Xfer ID");
    const auto part = [&](std::uint64_t number, std::vector<std::uint8_t> data) {
        auto chunk = udp::make_packet("SendXferPacket");
        chunk.block("XferID")["ID"] = xfer_id;
        chunk.block("XferID")["Packet"] = number;
        chunk.block("DataPacket")["Data"] = std::move(data);
        transmit(std::move(chunk));
        wait(
            [&](const auto& packets) {
                return std::any_of(packets.begin(), packets.end(), [&](const auto& packet) {
                    if (packet.spec->name != "ConfirmXferPacket")
                        return false;
                    const auto& fields = packet.block("XferID");
                    return std::get<std::uint64_t>(fields.at("ID")) == xfer_id &&
                           std::get<std::uint64_t>(fields.at("Packet")) == number;
                });
            },
            "Viewer did not receive the asset Xfer packet confirmation");
    };
    part(0, {5, 0, 0, 0, 1, 2, 3});
    part(0x80000001, {4, 5});
    check_uploaded(multipart_id, {1, 2, 3, 4, 5});

    const auto folder = inventory.root(circuit.agent_id);
    if (!folder)
        throw std::runtime_error("Viewer inventory root is missing");
    const auto create_item = [&](Uuid transaction, std::uint64_t callback) {
        auto request = udp::make_packet("CreateInventoryItem");
        auto& identity = request.block("AgentData");
        identity["AgentID"] = circuit.agent_id;
        identity["SessionID"] = circuit.session_id;
        auto& fields = request.block("InventoryBlock");
        fields["TransactionID"] = transaction;
        fields["CallbackID"] = callback;
        fields["FolderID"] = folder->id;
        fields["Type"] = std::int64_t{7};
        fields["InvType"] = std::int64_t{7};
        fields["NextOwnerMask"] = std::uint64_t{0x8000};
        fields["Name"] = std::vector<std::uint8_t>{'U', 'p', 'l', 'o', 'a', 'd', 0};
        transmit(std::move(request));
    };
    const auto check_created = [&](Uuid transaction, std::uint64_t callback,
                                   const std::vector<std::uint8_t>& expected) {
        Uuid item_id, asset_id;
        wait(
            [&](const auto& packets) {
                for (const auto& packet : packets) {
                    if (packet.spec->name != "UpdateCreateInventoryItem" ||
                        std::get<Uuid>(packet.block("AgentData").at("TransactionID")) != transaction)
                        continue;
                    const auto& fields = packet.block("InventoryData");
                    if (std::get<std::uint64_t>(fields.at("CallbackID")) != callback)
                        throw std::runtime_error("Upload inventory callback ID changed");
                    item_id = std::get<Uuid>(fields.at("ItemID"));
                    asset_id = std::get<Uuid>(fields.at("AssetID"));
                    return true;
                }
                return false;
            },
            "Upload did not create a viewer inventory item");
        const auto item = inventory.item(circuit.agent_id, item_id);
        const auto asset = uploaded_asset(asset_id);
        if (!item || item->asset_id != asset_id || item->parent != folder->id || item->name != "Upload" ||
            item->next_permissions != 0x8000 || !asset || !asset->data || *asset->data != expected)
            throw std::runtime_error("Upload inventory item or asset differs from the request");
        return item_id;
    };
    const auto create_first = Uuid::random();
    create_item(create_first, 41);
    upload(create_first, {1, 7, 0, 8}, false);
    check_created(create_first, 41, {1, 7, 0, 8});
    const auto upload_first = Uuid::random();
    upload(upload_first, {2, 7, 0, 8}, false);
    create_item(upload_first, 42);
    const auto updated_item = check_created(upload_first, 42, {2, 7, 0, 8});
    const auto update_item = [&](Uuid transaction) {
        auto request = udp::make_packet("UpdateInventoryItem");
        auto& identity = request.block("AgentData");
        identity["AgentID"] = circuit.agent_id;
        identity["SessionID"] = circuit.session_id;
        auto fields = udp::make_block(request.spec->blocks.at(1));
        fields["ItemID"] = updated_item;
        fields["TransactionID"] = transaction;
        fields["Name"] = std::vector<std::uint8_t>{'E', 'd', 'i', 't', 0};
        request.blocks["InventoryData"].push_back(std::move(fields));
        transmit(std::move(request));
    };
    const auto check_updated = [&](Uuid transaction, const std::vector<std::uint8_t>& expected) {
        Uuid asset_id;
        wait(
            [&](const auto& packets) {
                for (const auto& packet : packets) {
                    if (packet.spec->name != "UpdateCreateInventoryItem" ||
                        std::get<Uuid>(packet.block("AgentData").at("TransactionID")) != transaction)
                        continue;
                    const auto& fields = packet.block("InventoryData");
                    if (std::get<Uuid>(fields.at("ItemID")) != updated_item ||
                        std::get<std::uint64_t>(fields.at("CallbackID")) != 0)
                        throw std::runtime_error(
                            "Inventory upload update replaced the item identity or callback");
                    asset_id = std::get<Uuid>(fields.at("AssetID"));
                    return true;
                }
                return false;
            },
            "Upload did not update the existing inventory item");
        const auto item = inventory.item(circuit.agent_id, updated_item);
        const auto asset = uploaded_asset(asset_id);
        if (!item || item->asset_id != asset_id || item->name != "Edit" || !asset || !asset->data ||
            *asset->data != expected)
            throw std::runtime_error("Updated inventory row or asset differs from the request");
    };
    const auto update_first = Uuid::random();
    update_item(update_first);
    upload(update_first, {3, 7, 0, 8}, false);
    check_updated(update_first, {3, 7, 0, 8});
    const auto update_later = Uuid::random();
    upload(update_later, {4, 7, 0, 8}, false);
    update_item(update_later);
    check_updated(update_later, {4, 7, 0, 8});

    const auto prim = runtime.world().add(SceneObject::primitive(circuit.agent_id, {128, 128, 25}));
    if (!prim)
        throw std::runtime_error("Could not create upload test prim");
    auto task_item = std::make_shared<TaskInventoryItem>();
    task_item->item_id = Uuid::random();
    task_item->asset_id = covenant.full_id();
    task_item->owner_id = circuit.agent_id;
    task_item->creator_id = circuit.agent_id;
    task_item->type = task_item->inventory_type = 7;
    task_item->name = "Task upload";
    runtime.restore_task_inventory(prim->id, {task_item});
    const auto task_change = [&](Uuid transaction) {
        auto change = udp::make_packet("UpdateTaskInventory");
        auto& identity = change.block("AgentData");
        identity["AgentID"] = circuit.agent_id;
        identity["SessionID"] = circuit.session_id;
        change.block("UpdateData")["LocalID"] = std::uint64_t{prim->local_id};
        auto& fields = change.block("InventoryData");
        fields["ItemID"] = task_item->item_id;
        fields["TransactionID"] = transaction;
        fields["OwnerID"] = fields["CreatorID"] = circuit.agent_id;
        fields["Type"] = fields["InvType"] = std::int64_t{7};
        fields["OwnerMask"] = fields["NextOwnerMask"] = std::uint64_t{0x7fffffff};
        fields["Name"] = std::vector<std::uint8_t>{'T', 'a', 's', 'k', 0};
        transmit(std::move(change));
    };
    const auto check_task = [&](const std::vector<std::uint8_t>& expected) {
        wait(
            [&](const auto&) {
                const auto contents = runtime.snapshot_task_inventory(prim->id);
                if (!contents || contents->size() != 1 || contents->front().item_id != task_item->item_id)
                    return false;
                const auto asset = uploaded_asset(contents->front().asset_id);
                return contents->front().name == "Task" && asset && asset->data && *asset->data == expected;
            },
            "UDP task inventory transaction did not install the uploaded asset");
    };
    const auto task_upload_first = Uuid::random();
    upload(task_upload_first, {8, 0, 255, 1}, false);
    task_change(task_upload_first);
    check_task({8, 0, 255, 1});
    const auto task_update_first = Uuid::random();
    task_change(task_update_first);
    upload(task_update_first, {8, 0, 255, 2}, false);
    check_task({8, 0, 255, 2});

    auto restricted = udp::make_packet("AssetUploadRequest");
    auto& restricted_fields = restricted.block("AssetBlock");
    restricted_fields["TransactionID"] = Uuid::random();
    restricted_fields["Type"] = std::int64_t{static_cast<std::int8_t>(AssetType::Texture)};
    restricted_fields["StoreLocal"] = true;
    restricted_fields["AssetData"] = std::vector<std::uint8_t>{1, 2, 3};
    transmit(std::move(restricted));
    wait(
        [](const auto& packets) {
            for (const auto& packet : packets) {
                if (packet.spec->name != "AgentAlertMessage")
                    continue;
                const auto& message =
                    std::get<std::vector<std::uint8_t>>(packet.block("AlertData").at("Message"));
                if (std::string(message.begin(), std::find(message.begin(), message.end(), 0)) ==
                    "/Unable to upload asset. Insufficient permissions.")
                    return true;
            }
            return false;
        },
        "Permanent texture upload did not enforce the configured user level");

    if (!runtime.world().add(hub))
        throw std::runtime_error("Could not add the arrival fixture telehub");
    auto movement = udp::make_packet("CompleteAgentMovement");
    auto& movement_data = movement.block("AgentData");
    movement_data["AgentID"] = circuit.agent_id;
    movement_data["SessionID"] = circuit.session_id;
    movement_data["CircuitCode"] = std::uint64_t{circuit.circuit_code};
    transmit(std::move(movement));
    wait([&](const auto& packets) {
        return std::any_of(packets.begin(), packets.end(), [&](const auto& packet) {
            if (packet.spec->name != "AgentMovementComplete")
                return false;
            const auto& position = std::get<std::vector<double>>(packet.block("Data").at("Position"));
            return position.at(0) == expected_arrival_x && position.at(1) == 64;
        });
    }, "Viewer did not enter the region at the expected telehub spawn point");
    const auto landed_viewer = runtime.viewers().agent(circuit.agent_id);
    if (!landed_viewer || landed_viewer->camera_position != landed_viewer->position ||
        landed_viewer->camera_rotation != Quaternion{0, 0, 1, 0})
        throw std::runtime_error("Teleport landing did not reset the camera center and retain its rotation");
    auto own_object = SceneObject::primitive(circuit.agent_id, {160, 100, 25});
    own_object.name = "Manually returned fixture";
    const std::string face_media =
        "<OSMedia type=\"sl\" version=\"0.1\"><OSData><llsd><array><map>"
        "<key>current_url</key><string>https://example.test/?one=1&amp;two=2</string>"
        "</map><map><key>current_url</key><string>https://hidden.test/</string></map>"
        "</array></llsd></OSData></OSMedia>";
    own_object.face_media_xml = face_media;
    own_object.rez_start_string = "return fixture";
    // Version 1, 64-byte limit, one entry: k = v with an empty password.
    own_object.linkset_data = {1, 64, 1, 1, 'k', 1, 'v', 0};
    auto second_object = SceneObject::primitive(circuit.agent_id, {162, 100, 25});
    second_object.creator = Uuid::random();
    constexpr std::uint32_t copy_permission = 1u << 15;
    second_object.owner_mask &= ~copy_permission;
    const auto other_object = SceneObject::primitive(Uuid::random(), {165, 100, 25});
    const auto first_root = runtime.world().add(own_object);
    const auto second_root = runtime.world().add(second_object);
    if (!first_root || !second_root || !runtime.world().add(other_object))
        throw std::runtime_error("Could not add manual return fixtures");
    auto return_request = udp::make_packet("DeRezObject");
    return_request.block("AgentData")["AgentID"] = circuit.agent_id;
    return_request.block("AgentData")["SessionID"] = circuit.session_id;
    auto& return_data = return_request.block("AgentBlock");
    return_data["Destination"] = std::uint64_t{9};
    return_data["TransactionID"] = Uuid::random();
    return_data["PacketCount"] = std::uint64_t{2};
    return_data["PacketNumber"] = std::uint64_t{1};
    return_request.blocks["ObjectData"] = {{{"ObjectLocalID", std::uint64_t{second_root->local_id}}}};
    transmit(return_request);
    transmit(return_request);
    return_data["PacketNumber"] = std::uint64_t{0};
    constexpr std::uint64_t stale_local_id = 0xfffffffeu;
    return_request.blocks["ObjectData"] = {
        {{"ObjectLocalID", std::uint64_t{first_root->local_id}}}, {{"ObjectLocalID", stale_local_id}}};
    transmit(std::move(return_request));
    wait([&](const auto& packets) {
        return !runtime.world().object(own_object.id) && std::ranges::any_of(packets, [&](const auto& packet) {
            return packet.spec->name == "KillObject" &&
                   std::ranges::any_of(packet.blocks.at("ObjectData"), [&](const auto& row) {
                       return std::get<std::uint64_t>(row.at("ID")) == stale_local_id;
                   });
        });
    }, "Viewer could not complete the multipart object return or clear the stale ID");
    if (runtime.world().object(second_object.id) || !runtime.world().object(other_object.id))
        throw std::runtime_error("Viewer returned an object outside the requested task list");
    const auto lost_and_found = inventory.for_type(circuit.agent_id, 16);
    if (!lost_and_found)
        throw std::runtime_error("Viewer did not have a Lost and Found folder");
    const auto returned = inventory.folder_items(circuit.agent_id, lost_and_found->id);
    constexpr std::uint32_t multiple_objects = 0x200000;
    const auto bundle = std::ranges::find(returned, multiple_objects, &InventoryItem::flags);
    if (returned.size() != 2 || bundle == returned.end() || bundle->creator_id != Uuid{}.str() ||
        (bundle->current_permissions & copy_permission) != 0)
        throw std::runtime_error("Viewer manual return did not deliver one mixed-creator bundle");
    const auto bundle_asset = uploaded_asset(bundle->asset_id);
    const auto coalesced = bundle_asset && bundle_asset->data ? read_object_asset_xml(*bundle_asset->data)
                                                             : std::nullopt;
    if (!coalesced || !coalesced->coalesced || coalesced->linksets.size() != 2 ||
        coalesced->bounds != Vector3{2.5f, .5f, .5f} ||
        std::abs(coalesced->linksets[0].offset.x - coalesced->linksets[1].offset.x) != 2.f)
        throw std::runtime_error("Returned bundle did not preserve linkset bounds and relative positions");
    const auto saved_root = std::ranges::find_if(coalesced->linksets, [&](const auto& linkset) {
        return linkset.parts.front().object.id == own_object.id;
    });
    if (saved_root == coalesced->linksets.end() ||
        saved_root->parts.front().object.rez_start_string != own_object.rez_start_string ||
        saved_root->parts.front().object.linkset_data != own_object.linkset_data ||
        saved_root->parts.front().object.face_media_xml != face_media)
        throw std::runtime_error("Returned bundle did not preserve root metadata");
    const std::string_view bundle_xml(reinterpret_cast<const char*>(bundle_asset->data->data()),
                                      bundle_asset->data->size());
    if (bundle_xml.find("</lnkstdt><StartStr>") == std::string_view::npos)
        throw std::runtime_error("Returned bundle metadata is out of order for the managed reader");
    const auto outside_object = SceneObject::primitive(other_object.owner, {210, 100, 25});
    if (!inventory.create_user_inventory(other_object.owner) || !runtime.world().add(outside_object))
        throw std::runtime_error("Could not add parcel-scoped return fixtures");
    auto owner_return = udp::make_packet("ParcelReturnObjects");
    owner_return.block("AgentData")["AgentID"] = circuit.agent_id;
    owner_return.block("AgentData")["SessionID"] = circuit.session_id;
    owner_return.block("ParcelData")["LocalID"] = std::int64_t{3};
    owner_return.block("ParcelData")["ReturnType"] = std::uint64_t{16};
    owner_return.blocks["OwnerIDs"] = {{{"OwnerID", other_object.owner}}};
    transmit(std::move(owner_return));
    wait([&](const auto&) { return !runtime.world().object(other_object.id); },
         "Estate owner could not return the selected owner's object from the parcel");
    if (!runtime.world().object(outside_object.id))
        throw std::runtime_error("Parcel-scoped return removed an object from a different parcel");
    runtime.world().remove(outside_object.id);
    const auto source_folder = inventory.for_type(circuit.agent_id, 7);
    const auto requested_folder = inventory.for_type(circuit.agent_id, 6);
    if (!source_folder || !requested_folder)
        throw std::runtime_error("Could not find inventory folders for the Take fixture");
    auto take_object = SceneObject::primitive(circuit.agent_id, {160, 105, 25});
    take_object.name = "Taken fixture";
    take_object.from_inventory_folder = source_folder->id;
    take_object.owner_mask &= ~(copy_permission | (1u << 19));
    const auto take_root = runtime.world().add(take_object);
    const auto denied_root = runtime.world().add(SceneObject::primitive(other_object.owner, {162, 105, 25}));
    if (!take_root || !denied_root)
        throw std::runtime_error("Could not add Take fixtures");
    auto take = udp::make_packet("DeRezObject");
    take.block("AgentData")["AgentID"] = circuit.agent_id;
    take.block("AgentData")["SessionID"] = circuit.session_id;
    take.block("AgentBlock")["Destination"] = std::uint64_t{4};
    take.block("AgentBlock")["DestinationID"] = requested_folder->id;
    take.block("AgentBlock")["PacketCount"] = std::uint64_t{1};
    take.blocks["ObjectData"] = {
        {{"ObjectLocalID", std::uint64_t{take_root->local_id}}},
        {{"ObjectLocalID", std::uint64_t{denied_root->local_id}}}};
    transmit(std::move(take));
    wait([&](const auto&) { return !runtime.world().object(take_root->id); },
         "Viewer could not take its own locked object");
    if (!runtime.world().object(denied_root->id))
        throw std::runtime_error("Take granted an estate owner access to another owner's object");
    runtime.world().remove(denied_root->id);
    const auto source_items = inventory.folder_items(circuit.agent_id, source_folder->id);
    if (std::ranges::count(source_items, take_object.name, &InventoryItem::name) != 1)
        throw std::runtime_error("Take did not put the object back in its source folder");
    auto copy_object = SceneObject::primitive(circuit.agent_id, {160, 108, 25});
    copy_object.name = "Take Copy fixture";
    auto restricted_copy = SceneObject::primitive(circuit.agent_id, {162, 108, 25});
    restricted_copy.name = "No-copy fixture";
    restricted_copy.owner_mask &= ~copy_permission;
    auto public_copy = SceneObject::primitive(other_object.owner, {164, 108, 25});
    public_copy.name = "Public copy fixture";
    public_copy.everyone_mask = copy_permission;
    const auto copy_root = runtime.world().add(copy_object);
    const auto restricted_root = runtime.world().add(restricted_copy);
    const auto public_root = runtime.world().add(public_copy);
    if (!copy_root || !restricted_root || !public_root)
        throw std::runtime_error("Could not add Take Copy fixtures");
    auto copy = udp::make_packet("DeRezObject");
    copy.block("AgentData")["AgentID"] = circuit.agent_id;
    copy.block("AgentData")["SessionID"] = circuit.session_id;
    copy.block("AgentBlock")["Destination"] = std::uint64_t{1};
    copy.block("AgentBlock")["DestinationID"] = source_folder->id;
    copy.block("AgentBlock")["PacketCount"] = std::uint64_t{1};
    copy.blocks["ObjectData"] = {
        {{"ObjectLocalID", std::uint64_t{copy_root->local_id}}},
        {{"ObjectLocalID", std::uint64_t{restricted_root->local_id}}},
        {{"ObjectLocalID", std::uint64_t{public_root->local_id}}}};
    transmit(std::move(copy));
    wait([&](const auto&) {
        const auto items = inventory.folder_items(circuit.agent_id, requested_folder->id);
        const bool own_copy = std::ranges::any_of(items, [&](const auto& item) {
            return item.name == copy_object.name && !(item.flags & multiple_objects);
        });
        const bool transferred_copy = std::ranges::any_of(items, [&](const auto& item) {
            return item.name == public_copy.name && item.owner == circuit.agent_id && (item.flags & 0x100);
        });
        return own_copy && transferred_copy;
    }, "Take Copy did not deliver the permitted object to the Objects folder");
    if (!runtime.world().object(copy_root->id) || !runtime.world().object(restricted_root->id) ||
        !runtime.world().object(public_root->id))
        throw std::runtime_error("Take Copy removed an object from the region");
    runtime.world().remove(copy_root->id);
    auto deletion = udp::make_packet("DeRezObject");
    deletion.block("AgentData")["AgentID"] = circuit.agent_id;
    deletion.block("AgentData")["SessionID"] = circuit.session_id;
    deletion.block("AgentBlock")["Destination"] = std::uint64_t{6};
    deletion.block("AgentBlock")["PacketCount"] = std::uint64_t{1};
    deletion.blocks["ObjectData"] = {
        {{"ObjectLocalID", std::uint64_t{restricted_root->local_id}}},
        {{"ObjectLocalID", std::uint64_t{public_root->local_id}}}};
    transmit(std::move(deletion));
    wait([&](const auto&) { return !runtime.world().object(restricted_root->id); },
         "Viewer could not delete its own no-copy object");
    if (!runtime.world().object(public_root->id))
        throw std::runtime_error("Delete granted an estate owner access to another owner's object");
    const auto trash = inventory.for_type(circuit.agent_id, 14);
    if (!trash)
        throw std::runtime_error("Viewer did not have a Trash folder");
    const auto trash_items = inventory.folder_items(circuit.agent_id, trash->id);
    const auto deleted_items = std::ranges::count(trash_items, restricted_copy.name, &InventoryItem::name);
    if (deleted_items != (use_trash_on_delete ? 1 : 0))
        throw std::runtime_error("Delete did not follow the configured Trash setting");
    runtime.world().remove(public_root->id);
    InventoryItem existing;
    existing.id = Uuid::random();
    existing.owner = circuit.agent_id;
    existing.parent = requested_folder->id;
    existing.creator_id = circuit.agent_id.str();
    existing.asset_id = Uuid::random();
    existing.name = "Original inventory name";
    existing.asset_type = existing.inventory_type = 6;
    existing.current_permissions = existing.base_permissions = 0x7fffffff;
    if (!inventory.add_item(existing))
        throw std::runtime_error("Could not seed the existing object inventory item");
    auto edited = SceneObject::primitive(circuit.agent_id, {160, 112, 25});
    edited.name = "Saved existing fixture";
    edited.description = "Edited in the region";
    edited.from_inventory_item = existing.id;
    const auto edited_root = runtime.world().add(edited);
    if (!edited_root)
        throw std::runtime_error("Could not add Save to Existing fixture");
    const auto foreign_folder = inventory.for_type(other_object.owner, 6);
    if (!foreign_folder)
        throw std::runtime_error("Could not find the other owner's Objects folder");
    auto foreign_item = existing;
    foreign_item.id = Uuid::random();
    foreign_item.owner = other_object.owner;
    foreign_item.parent = foreign_folder->id;
    if (!inventory.add_item(foreign_item))
        throw std::runtime_error("Could not seed the other owner's inventory item");
    auto foreign_source = SceneObject::primitive(circuit.agent_id, {162, 112, 25});
    foreign_source.from_inventory_item = foreign_item.id;
    const auto foreign_source_root = runtime.world().add(foreign_source);
    if (!foreign_source_root)
        throw std::runtime_error("Could not add the foreign-source Save fixture");
    auto save = udp::make_packet("DeRezObject");
    save.block("AgentData")["AgentID"] = circuit.agent_id;
    save.block("AgentData")["SessionID"] = circuit.session_id;
    save.block("AgentBlock")["Destination"] = std::uint64_t{0};
    save.block("AgentBlock")["DestinationID"] = source_folder->id;
    save.block("AgentBlock")["PacketCount"] = std::uint64_t{1};
    save.blocks["ObjectData"] = {
        {{"ObjectLocalID", std::uint64_t{edited_root->local_id}}},
        {{"ObjectLocalID", std::uint64_t{foreign_source_root->local_id}}}};
    transmit(std::move(save));
    std::optional<InventoryItem> saved_item;
    wait([&](const auto&) {
        saved_item = inventory.item(circuit.agent_id, existing.id);
        return saved_item && saved_item->asset_id != existing.asset_id;
    }, "Save to Existing did not update the source inventory item");
    const auto saved_asset = uploaded_asset(saved_item->asset_id);
    const auto saved_object = saved_asset && saved_asset->data ? read_object_asset_xml(*saved_asset->data)
                                                               : std::nullopt;
    if (saved_item->parent != existing.parent || saved_item->current_permissions != existing.current_permissions ||
        saved_item->name != edited.name || saved_item->description != edited.description ||
        !saved_object || saved_object->linksets.front().parts.front().object.name != edited.name ||
        !runtime.world().object(edited.id))
        throw std::runtime_error("Save to Existing lost item metadata or removed the scene object");
    runtime.world().remove(edited.id);
    const auto untouched = inventory.item(other_object.owner, foreign_item.id);
    if (!untouched || untouched->asset_id != foreign_item.asset_id || !runtime.world().object(foreign_source.id))
        throw std::runtime_error("Save to Existing overwrote another user's inventory item");
    runtime.world().remove(foreign_source.id);
    auto admin_copy_source = SceneObject::primitive(other_object.owner, {164, 112, 25});
    admin_copy_source.name = "God Take Copy fixture";
    admin_copy_source.owner_mask &= ~copy_permission;
    const auto admin_copy_root = runtime.world().add(admin_copy_source);
    if (!admin_copy_root)
        throw std::runtime_error("Could not add God Take Copy fixture");
    auto admin_copy = udp::make_packet("DeRezObject");
    admin_copy.block("AgentData")["AgentID"] = circuit.agent_id;
    admin_copy.block("AgentData")["SessionID"] = circuit.session_id;
    admin_copy.block("AgentBlock")["Destination"] = std::uint64_t{5};
    admin_copy.block("AgentBlock")["PacketCount"] = std::uint64_t{1};
    constexpr std::uint64_t admin_copy_stale_id = 0xfffffffdu;
    admin_copy.blocks["ObjectData"] = {
        {{"ObjectLocalID", std::uint64_t{admin_copy_root->local_id}}},
        {{"ObjectLocalID", admin_copy_stale_id}}};
    transmit(admin_copy);
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [&](const auto& packet) {
            return packet.spec->name == "KillObject" &&
                   std::ranges::any_of(packet.blocks.at("ObjectData"), [&](const auto& row) {
                       return std::get<std::uint64_t>(row.at("ID")) == admin_copy_stale_id;
                   });
        });
    }, "God Take Copy request did not process the stale object ID");
    const auto owner_copies = [&] {
        const auto items = inventory.folder_items(other_object.owner, foreign_folder->id);
        return std::ranges::count(items, admin_copy_source.name, &InventoryItem::name);
    };
    if (owner_copies() != 0)
        throw std::runtime_error("God Take Copy allowed a viewer without administrator rights");
    grant_viewer_admin = true;
    transmit(std::move(admin_copy));
    wait([&](const auto&) { return owner_copies() == 1; },
         "God Take Copy did not deliver the copy to the original owner");
    grant_viewer_admin = false;
    if (!runtime.world().object(admin_copy_root->id))
        throw std::runtime_error("God Take Copy removed the original scene object");
    runtime.world().remove(admin_copy_root->id);
    auto search_object = SceneObject::primitive(circuit.agent_id, {166, 112, 25});
    search_object.name = "Duplication fixture";
    search_object.face_media_xml = face_media;
    search_object.velocity = {3, 2, 1};
    search_object.shape.profile_curve = 1;
    TextureEntry search_textures(Uuid::random());
    const auto search_image = Uuid::random();
    search_textures.create_face(0)->texture_id(search_image);
    search_textures.create_face(0)->media_flags(true);
    search_object.texture_entry = search_textures.bytes(6);
    const auto search_root = runtime.world().add(search_object);
    if (!search_root)
        throw std::runtime_error("Could not add searchable linkset fixture");
    auto search_child = SceneObject::primitive(circuit.agent_id, {1, 0, 0});
    search_child.parent_id = search_root->local_id;
    const auto child_part = runtime.world().add(search_child);
    if (!child_part)
        throw std::runtime_error("Could not add searchable linkset child");
    const auto media_caps = runtime.capabilities()->capabilities().get_caps_for_user(circuit.circuit_code);
    network::HttpRequest media_request;
    media_request.method = "POST";
    media_request.path = media_caps->local_paths().at("ObjectMedia").value();
    const auto fetch_media = [&](Uuid id) {
        auto input = network::Value::object({{"verb", network::Value("GET")},
                                            {"object_id", network::Value(id.str())}});
        media_request.body = network::llsd_xml(input);
        return listener.dispatch(media_request);
    };
    const auto media_response = fetch_media(search_root->id);
    if (media_response.status != 200)
        throw std::runtime_error("ObjectMedia could not fetch the stored media list");
    const auto media_data = network::parse_llsd_xml(media_response.body);
    const auto& faces = media_data.get("object_media_data").array;
    if (faces.size() != 2 || faces.front().get("current_url").as_string() != "https://example.test/?one=1&two=2" ||
        faces.back().kind != network::Value::Kind::Undefined ||
        media_data.get("object_id").as_string() != search_root->id.str() ||
        media_data.get("object_media_version").as_string() != "x-mv:0000000000/00000000-0000-0000-0000-000000000000")
        throw std::runtime_error("ObjectMedia did not return the enabled face and default version");
    if (fetch_media(Uuid::random()).status != 400)
        throw std::runtime_error("ObjectMedia did not reject a missing object");
    auto media_prim = SceneObject::primitive(circuit.agent_id, {172, 112, 25});
    media_prim.name = "Script media fixture";
    const auto media_part = runtime.world().add(media_prim);
    if (!media_part)
        throw std::runtime_error("Could not add ObjectMedia update fixture");
    network::Value updated_faces;
    updated_faces.kind = network::Value::Kind::Array;
    network::Value whitelist;
    whitelist.kind = network::Value::Kind::Array;
    whitelist.array.emplace_back("example.test:8443/allowed/*");
    whitelist.array.emplace_back("*trusted.test");
    updated_faces.array.push_back(network::Value::object({
        {"current_url", network::Value("https://example.test/new")},
        {"perms_interact", network::Value(1)},
        {"whitelist_enable", network::Value(true)},
        {"whitelist", std::move(whitelist)},
    }));
    const auto update_media = [&](const network::Value& entries) {
        media_request.body = network::llsd_xml(network::Value::object(
            {{"verb", network::Value("UPDATE")}, {"object_id", network::Value(media_part->id.str())},
             {"object_media_data", entries}}));
        return listener.dispatch(media_request);
    };
    if (update_media(updated_faces).status != 200)
        throw std::runtime_error("ObjectMedia rejected a valid update");
    const auto updated_media = network::parse_llsd_xml(fetch_media(media_part->id).body);
    const auto& updated_entries = updated_media.get("object_media_data").array;
    if (updated_entries.size() != 1 ||
        updated_entries.front().get("current_url").as_string() != "https://example.test/new" ||
        updated_media.get("object_media_version").as_string() != "x-mv:0000000001/" + circuit.agent_id.str())
        throw std::runtime_error("ObjectMedia did not return the updated face and version");
    const auto navigate = [&](const char* url, int face = 0) {
        auto request = media_request;
        request.path = media_caps->local_paths().at("ObjectMediaNavigate").value();
        request.body = network::llsd_xml(network::Value::object(
            {{"object_id", network::Value(media_part->id.str())}, {"texture_index", network::Value(face)},
             {"current_url", network::Value(url)}}));
        return listener.dispatch(request);
    };
    const char* allowed_url = "https://viewer@EXAMPLE.test:8443/other/../allowed/index?q=1";
    if (navigate("https://example.test/allowed/index").status != 400 ||
        navigate("https://example.test:8443/blocked").status != 400 ||
        navigate(allowed_url, -1).status != 400)
        throw std::runtime_error("ObjectMediaNavigate accepted a blocked URL or invalid face");
    const auto navigated = navigate(allowed_url);
    if (navigated.status != 200 || network::parse_llsd_xml(navigated.body).kind != network::Value::Kind::Undefined)
        throw std::runtime_error("ObjectMediaNavigate rejected a whitelisted URL");
    if (navigate("https://sub.trusted.test/index").status != 200)
        throw std::runtime_error("ObjectMediaNavigate rejected a wildcard host");
    const auto final_media = network::parse_llsd_xml(fetch_media(media_part->id).body);
    if (final_media.get("object_media_data").array.front().get("current_url").as_string() !=
            "https://sub.trusted.test/index" ||
        final_media.get("object_media_version").as_string() != "x-mv:0000000003/" + circuit.agent_id.str())
        throw std::runtime_error("ObjectMediaNavigate did not update the face and version");
    updated_faces.array.front().set("perms_interact", network::Value(0));
    if (update_media(updated_faces).status != 200 || navigate(allowed_url).status != 400)
        throw std::runtime_error("ObjectMediaNavigate ignored the face interaction permissions");
    updated_faces.array.resize(media_prim.shape.face_count() + 1);
    if (update_media(updated_faces).status != 400)
        throw std::runtime_error("ObjectMedia accepted too many faces");
    const auto duplicate_script = [&](Uuid part, bool running) {
        auto item = std::make_shared<TaskInventoryItem>();
        item->item_id = Uuid::random();
        item->asset_id = duplicate_script_asset;
        item->owner_id = circuit.agent_id;
        item->parent_id = item->parent_part_id = part;
        item->type = item->inventory_type = 10;
        item->script_running = running;
        runtime.restore_task_inventory(part, {item});
    };
    duplicate_script(media_part->id, true);
    wait([&](const auto&) {
        return runtime.world().object(media_part->id)->media_url ==
               "x-mv:0000000005/00000000-0000-0000-0000-000000000000";
    }, "LSL did not update prim media");
    const auto script_media = network::parse_llsd_xml(fetch_media(media_part->id).body);
    if (script_media.get("object_media_data").array.front().get("current_url").as_string() !=
        "https://script.example/path")
        throw std::runtime_error("The viewer did not see the script's media update");
    wait([&](const auto&) {
        return runtime.world().object(media_part->id)->floating_text == "media status 0/0";
    }, "LSL did not clear linked prim media");
    const auto cleared_media = network::parse_llsd_xml(fetch_media(media_part->id).body);
    TextureEntry cleared_textures(runtime.world().object(media_part->id)->texture_entry);
    if (cleared_media.get("object_media_data").array.front().kind != network::Value::Kind::Undefined ||
        cleared_textures.get_face(0)->media_flags() ||
        cleared_media.get("object_media_version").as_string() != "x-mv:0000000006/00000000-0000-0000-0000-000000000000")
        throw std::runtime_error("Clearing media did not remove the media entry and flag");
    runtime.world().remove(media_part->id);
    auto music_prim = SceneObject::primitive(parcel_owner, {172, 112, 25});
    music_prim.name = "Parcel music fixture";
    music_prim.description = circuit.agent_id.str();
    const auto music_part = runtime.world().add(music_prim);
    if (!music_part)
        throw std::runtime_error("Could not add parcel music fixture");
    duplicate_script(music_part->id, true);
    wait([&](const auto&) {
        return runtime.world().object(music_part->id)->floating_text ==
               "https://stream.example/audio|https://video.example/media|video/mp4|640|360|Parcel video|"
               "12345678-1234-4321-8123-123456789abc";
    }, "Parcel owner script could not read media properties");
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [](const auto& packet) {
            if (packet.spec->name != "ParcelMediaCommandMessage")
                return false;
            const auto& command = packet.block("CommandBlock");
            return std::get<std::uint64_t>(command.at("Flags")) == ((1u << 6) | (1u << 3)) &&
                   std::get<std::uint64_t>(command.at("Command")) == 3 &&
                   std::get<double>(command.at("Time")) == 12.5;
        }) && std::ranges::any_of(packets, [](const auto& packet) {
            if (packet.spec->name != "ParcelMediaUpdate")
                return false;
            const auto& extended = packet.block("DataBlockExtended");
            return std::get<std::int64_t>(extended.at("MediaWidth")) == 320 &&
                   std::get<std::uint64_t>(extended.at("MediaLoop")) == 1;
        });
    }, "Targeted parcel media commands did not send the expected viewer packets");
    wait([&](const auto&) {
        return runtime.world().object(music_part->id)->floating_text == "https://global.example/video";
    }, "The targeted command changed the parcel's saved media");
    runtime.world().remove(music_part->id);
    music_prim = SceneObject::primitive(circuit.agent_id, {172, 112, 25});
    music_prim.name = "Parcel music fixture";
    const auto denied_music_part = runtime.world().add(music_prim);
    if (!denied_music_part)
        throw std::runtime_error("Could not add denied parcel music fixture");
    duplicate_script(denied_music_part->id, true);
    wait([&](const auto&) {
        return runtime.world().object(denied_music_part->id)->floating_text == "music denied";
    }, "Script owned by a visitor could access parcel music");
    runtime.world().remove(denied_music_part->id);
    duplicate_script(search_root->id, true);
    duplicate_script(child_part->id, false);
    wait([&](const auto&) { return runtime.world().object(search_root->id)->floating_text == "fresh"; },
         "The source duplication script did not start");
    runtime.world().set_floating_text(search_root->id, "source text", 0xffffffffu);
    runtime.world().set_floating_text(child_part->id, "stopped", 0xffffffffu);
    struct SnapshotCacheFixture {
        std::filesystem::path path = std::filesystem::temp_directory_path() / ("opensim-snapshot-" + Uuid::random().str());
        SnapshotCacheFixture() {
            if (!std::filesystem::create_directory(path))
                throw std::runtime_error("Could not create snapshot cache fixture");
        }
        ~SnapshotCacheFixture() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cache;
    const auto object_uuid = "<uuid>" + search_root->id.str() + "</uuid>";
    const auto child_uuid = "<uuid>" + child_part->id.str() + "</uuid>";
    const auto object_fragment = [&] {
        const auto name = runtime.descriptor().name.value_or("");
        std::ifstream file(cache.path / (name + "_ObjectSnapshot.xml"), std::ios::binary);
        if (!file)
            throw std::runtime_error("Missing DataSnapshot object fragment");
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    runtime.poll();
    runtime.data_snapshot({}, 20, "Fixture", {}, false, cache.path);
    if (object_fragment().find(object_uuid) != std::string::npos)
        throw std::runtime_error("Initial fragment published an unlisted object");
    auto search = udp::make_packet("ObjectIncludeInSearch");
    search.block("AgentData")["AgentID"] = circuit.agent_id;
    search.block("AgentData")["SessionID"] = circuit.session_id;
    search.blocks["ObjectData"] = {
        {{"ObjectLocalID", std::uint64_t{child_part->local_id}}, {"IncludeInSearch", true}}};
    constexpr std::uint32_t searchable_flag = 0x8000u;
    const auto initial_revision = runtime.data_snapshot_revision();
    transmit(search);
    wait([&](const auto&) {
        return runtime.data_snapshot_revision() > initial_revision &&
               (runtime.world().object(search_root->id)->flags & searchable_flag) != 0;
    },
         "Viewer could not set the searchable flag through a child prim");
    if (runtime.world().object(child_part->id)->flags & searchable_flag)
        throw std::runtime_error("Include in Search changed the child flag instead of just the root flag");
    const auto snapshot = runtime.data_snapshot({}, 20, "Fixture", {}, false, cache.path);
    if (snapshot.find(object_uuid) == std::string::npos || snapshot.find(child_uuid) != std::string::npos)
        throw std::runtime_error("DataSnapshot did not publish just the searchable root");
    if (snapshot.find("<image>" + search_image.str() + "</image>") == std::string::npos)
        throw std::runtime_error("DataSnapshot did not select the least-used box texture");
    if (object_fragment().find(object_uuid) == std::string::npos)
        throw std::runtime_error("Listing the object did not update the disk fragment");
    const auto all_parcels = runtime.data_snapshot({}, 20, "Fixture", {}, true);
    if (snapshot.find("<parcel ") != std::string::npos || all_parcels.find("<parcel ") == std::string::npos)
        throw std::runtime_error("DataSnapshot did not honor the parcel exposure setting");
    search.blocks["ObjectData"].front()["IncludeInSearch"] = false;
    const auto listed_revision = runtime.data_snapshot_revision();
    transmit(std::move(search));
    wait([&](const auto&) {
        return runtime.data_snapshot_revision() > listed_revision &&
               (runtime.world().object(search_root->id)->flags & searchable_flag) == 0;
    },
         "Viewer could not clear the searchable flag");
    if (runtime.data_snapshot({}, 20, "Fixture", {}, false, cache.path).find(object_uuid) != std::string::npos ||
        object_fragment().find(object_uuid) != std::string::npos)
        throw std::runtime_error("DataSnapshot still published the root after the flag was cleared");
    const auto find_duplicate = [&](Vector3 position) -> std::optional<SceneObject> {
        for (const auto& object : runtime.world().objects())
            if (!object.parent_id && object.id != search_root->id && object.name == search_object.name &&
                object.position == position)
                return object;
        return std::nullopt;
    };
    auto duplicate = udp::make_packet("ObjectDuplicate");
    duplicate.block("AgentData")["AgentID"] = circuit.agent_id;
    duplicate.block("AgentData")["SessionID"] = circuit.session_id;
    duplicate.block("SharedData")["Offset"] = std::vector<double>{0, 4, 0};
    duplicate.blocks["ObjectData"] = {{{"ObjectLocalID", std::uint64_t{child_part->local_id}}}};
    transmit(std::move(duplicate));
    const Vector3 offset_position{166, 116, 25};
    wait([&](const auto&) {
        const auto copy = find_duplicate(offset_position);
        return copy && copy->floating_text == "fresh";
    }, "Offset duplication did not start the copied script in its initial state");
    const auto offset_copy = *find_duplicate(offset_position);
    if (offset_copy.velocity != Vector3{} || offset_copy.face_media_xml != face_media ||
        runtime.world().object(search_root->id)->floating_text != "source text")
        throw std::runtime_error("Duplication inherited root velocity or restarted the source script");
    bool stopped_child_found = false;
    for (const auto& part : runtime.world().objects()) {
        if (part.parent_id != offset_copy.local_id || part.position != search_child.position)
            continue;
        const auto items = runtime.snapshot_task_inventory(part.id);
        stopped_child_found = part.floating_text == "stopped" && items && items->size() == 1 &&
                              !items->front().script_running;
    }
    if (!stopped_child_found)
        throw std::runtime_error("Offset duplication lost the child transform or restarted a stopped script");
    const auto target = runtime.world().add(SceneObject::primitive(circuit.agent_id, {176, 112, 25}));
    if (!target)
        throw std::runtime_error("Could not add duplication ray target");
    auto ray_duplicate = udp::make_packet("ObjectDuplicateOnRay");
    auto& ray = ray_duplicate.block("AgentData");
    ray["AgentID"] = circuit.agent_id;
    ray["SessionID"] = circuit.session_id;
    ray["RayTargetID"] = target->id;
    ray["RayStart"] = std::vector<double>{170, 112, 25};
    ray["RayEnd"] = std::vector<double>{176, 112, 25};
    ray["CopyCenters"] = true;
    ray_duplicate.blocks["ObjectData"] = {{{"ObjectLocalID", std::uint64_t{search_root->local_id}}}};
    transmit(std::move(ray_duplicate));
    const Vector3 ray_position{175.5f, 112, 25};
    wait([&](const auto&) { return find_duplicate(ray_position).has_value(); },
         "Ray duplication did not place the copy on the target face");
    runtime.world().remove(find_duplicate(ray_position)->id);
    runtime.world().remove(offset_copy.id);
    runtime.world().remove(target->id);
    runtime.world().remove(search_root->id);
    auto public_source = SceneObject::primitive(other_object.owner, {168, 112, 25});
    public_source.name = "Cross-owner duplicate fixture";
    public_source.group = public_source.owner;
    public_source.last_owner = Uuid::random();
    public_source.everyone_mask = copy_permission;
    const auto public_part = runtime.world().add(public_source);
    if (!public_part)
        throw std::runtime_error("Could not add cross-owner duplication fixture");
    auto contents = std::make_shared<TaskInventoryItem>();
    contents->item_id = Uuid::random();
    contents->asset_id = Uuid::random();
    contents->parent_id = contents->parent_part_id = public_part->id;
    contents->owner_id = public_source.owner;
    contents->type = contents->inventory_type = 7;
    contents->base_permissions = contents->current_permissions = 0x9e000u;
    contents->everyone_permissions = copy_permission;
    contents->next_permissions = 0x82000u;
    runtime.restore_task_inventory(public_part->id, {contents});
    auto cross_owner = udp::make_packet("ObjectDuplicate");
    cross_owner.block("AgentData")["AgentID"] = circuit.agent_id;
    cross_owner.block("AgentData")["SessionID"] = circuit.session_id;
    cross_owner.block("SharedData")["Offset"] = std::vector<double>{0, 4, 0};
    cross_owner.blocks["ObjectData"] = {{{"ObjectLocalID", std::uint64_t{public_part->local_id}}}};
    transmit(std::move(cross_owner));
    std::optional<SceneObject> transferred;
    wait([&](const auto&) {
        for (const auto& object : runtime.world().objects()) {
            if (object.id != public_part->id && object.name == public_source.name) {
                transferred = object;
                return true;
            }
        }
        return false;
    }, "Could not duplicate a publicly copyable object");
    const auto transferred_items = runtime.snapshot_task_inventory(transferred->id);
    if (transferred->owner != circuit.agent_id || transferred->last_owner != public_source.last_owner ||
        !transferred->group.is_zero() || transferred->owner_mask != public_source.next_mask ||
        !transferred_items || transferred_items->size() != 1)
        throw std::runtime_error("Cross-owner duplication lost ownership or next-owner permissions");
    const auto& transferred_item = transferred_items->front();
    if (transferred_item.owner_id != circuit.agent_id || transferred_item.last_owner_id != public_source.owner ||
        transferred_item.item_id == contents->item_id || transferred_item.asset_id != contents->asset_id ||
        transferred_item.parent_part_id != transferred->id ||
        transferred_item.current_permissions != contents->next_permissions ||
        runtime.world().object(public_part->id)->owner != public_source.owner)
        throw std::runtime_error("Cross-owner duplication lost task inventory or changed the source");
    runtime.world().remove(transferred->id);
    runtime.world().remove(public_part->id);
    runtime.poll();
    dwell_time += std::chrono::seconds(150);
    runtime.poll();
    if (!dwell_writes.empty())
        throw std::runtime_error("Parcel dwell updated before the source interval elapsed");
    dwell_time += std::chrono::seconds(1);
    runtime.poll();
    constexpr auto expected_dwell = static_cast<float>(40. * (1. - 1.5e-8 * 151000.) + 151. / 60.);
    if (dwell_writes != std::vector<std::pair<Uuid, float>>{{dwell_parcel, expected_dwell}})
        throw std::runtime_error("Parcel dwell must accumulate occupancy, decay and persist significant changes");
    auto dwell_request = udp::make_packet("ParcelDwellRequest");
    dwell_request.block("AgentData")["AgentID"] = circuit.agent_id;
    dwell_request.block("AgentData")["SessionID"] = circuit.session_id;
    dwell_request.block("Data")["LocalID"] = std::int64_t{2};
    transmit(std::move(dwell_request));
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [&](const auto& packet) {
            return packet.spec->name == "ParcelDwellReply" &&
                   std::get<double>(packet.block("Data").at("Dwell")) == expected_dwell;
        });
    }, "Viewer did not receive the accumulated parcel dwell");
    if (!runtime.world().remove(hub.id))
        throw std::runtime_error("Could not remove the arrival fixture telehub");

    std::map<Uuid, std::uint32_t> parcel_objects;
    for (int index = 0; index < 257; ++index) {
        const auto owner = Uuid::random();
        const auto object = runtime.world().add(SceneObject::primitive(owner, {100, 64, 25}), false, false);
        if (!object)
            throw std::runtime_error("Could not add the parcel packet fixture object");
        parcel_objects.emplace(owner, object->local_id);
    }
    auto select = udp::make_packet("ParcelSelectObjects");
    select.block("AgentData")["AgentID"] = circuit.agent_id;
    select.block("AgentData")["SessionID"] = circuit.session_id;
    select.block("ParcelData")["LocalID"] = std::int64_t{2};
    select.block("ParcelData")["ReturnType"] = std::uint64_t{8};
    transmit(std::move(select));
    wait([&](const auto& packets) {
        std::set<std::uint32_t> selected;
        std::set<std::uint32_t> sequences;
        for (const auto& reply : packets) {
            if (reply.spec->name != "ForceObjectSelect" || !sequences.insert(reply.sequence).second)
                continue;
            const auto first = selected.empty();
            const auto& rows = reply.blocks.at("Data");
            if (std::get<bool>(reply.block("_Header").at("ResetList")) != first ||
                rows.size() != (first ? 251u : 6u))
                throw std::runtime_error("Parcel selection split did not preserve the reset flag or block limit");
            for (const auto& row : rows)
                selected.insert(static_cast<std::uint32_t>(std::get<std::uint64_t>(row.at("LocalID"))));
        }
        return selected.size() == parcel_objects.size() &&
               std::ranges::all_of(parcel_objects, [&](const auto& entry) { return selected.contains(entry.second); });
    }, "Viewer did not receive the complete multi-packet parcel selection");

    auto owner_request = udp::make_packet("ParcelObjectOwnersRequest");
    owner_request.block("AgentData")["AgentID"] = circuit.agent_id;
    owner_request.block("AgentData")["SessionID"] = circuit.session_id;
    owner_request.block("ParcelData")["LocalID"] = std::int64_t{2};
    transmit(std::move(owner_request));
    wait([&](const auto& packets) {
        std::set<Uuid> owners;
        std::set<std::uint32_t> sequences;
        for (const auto& reply : packets) {
            if (reply.spec->name != "ParcelObjectOwnersReply" || !sequences.insert(reply.sequence).second)
                continue;
            for (const auto& row : reply.blocks.at("Data")) {
                const auto id = std::get<Uuid>(row.at("OwnerID"));
                if (!parcel_objects.contains(id) || !owners.insert(id).second ||
                    std::get<std::int64_t>(row.at("Count")) != 1)
                    throw std::runtime_error("Parcel owner packets contain a missing, duplicate or invalid owner row");
            }
        }
        return owners.size() == parcel_objects.size() && sequences.size() > 1;
    }, "Viewer did not receive the complete multi-packet parcel owner summary");
    for (const auto& [id, local_id] : parcel_objects) {
        const auto object = runtime.world().object(local_id);
        if (!object || !runtime.world().remove(object->id, false))
            throw std::runtime_error("Could not remove a parcel packet fixture object");
    }

    std::vector<std::uint32_t> home_alert_sequences;
    const auto set_home = [&](double x, std::string_view expected) {
        auto request = udp::make_packet("SetStartLocationRequest");
        auto& agent = request.block("AgentData");
        agent["AgentID"] = circuit.agent_id;
        agent["SessionID"] = circuit.session_id;
        auto& location = request.block("StartLocationData");
        location["LocationID"] = std::uint64_t{1};
        location["LocationPos"] = std::vector<double>{x, 64, 60};
        location["LocationLookAt"] = std::vector<double>{0, 1, 0};
        transmit(std::move(request));
        wait([&](const auto& packets) {
            for (const auto& reply : packets) {
                if (reply.spec->name != "AgentAlertMessage" ||
                    std::find(home_alert_sequences.begin(), home_alert_sequences.end(), reply.sequence) !=
                        home_alert_sequences.end())
                    continue;
                const auto& alert = reply.block("AlertData");
                const auto& bytes = std::get<std::vector<std::uint8_t>>(alert.at("Message"));
                if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) == expected) {
                    home_alert_sequences.push_back(reply.sequence);
                    return !std::get<bool>(alert.at("Modal"));
                }
            }
            return false;
        }, "Viewer did not receive the expected Set Home alert");
    };
    constexpr auto denied = "/You are not allowed to set your home location in this parcel.";
    set_home(96, denied); // A saved telehub UUID alone does not grant permission.
    if (!runtime.world().add(hub))
        throw std::runtime_error("Could not add the Set Home fixture telehub");
    set_home(32, "Home position set."); // Group role grants permission in this parcel.
    set_home(96, "Home position set."); // The actual telehub grants permission in its parcel.
    set_home(160, denied); // Neither a neighboring telehub nor another group's role grants permission.

    const auto placed_hub = runtime.world().object(hub.id);
    if (!placed_hub)
        throw std::runtime_error("Set Home fixture telehub disappeared");
    std::vector<std::uint32_t> telehub_replies;
    const auto manage_telehub = [&](std::string_view command, std::uint32_t parameter,
                                   Uuid expected_hub, std::size_t expected_points) {
        auto request = udp::make_packet("EstateOwnerMessage");
        auto& agent = request.block("AgentData");
        agent["AgentID"] = circuit.agent_id;
        agent["SessionID"] = circuit.session_id;
        request.block("MethodData")["Method"] = std::vector<std::uint8_t>{'t','e','l','e','h','u','b',0};
        for (const auto& argument : {std::string(command), std::to_string(parameter)}) {
            auto block = udp::make_block(request.spec->blocks.at(2));
            std::vector<std::uint8_t> bytes(argument.begin(), argument.end());
            bytes.push_back(0);
            block["Parameter"] = std::move(bytes);
            request.blocks["ParamList"].push_back(std::move(block));
        }
        transmit(std::move(request));
        wait([&](const auto& packets) {
            for (const auto& reply : packets) {
                if (reply.spec->name != "TelehubInfo" ||
                    std::find(telehub_replies.begin(), telehub_replies.end(), reply.sequence) != telehub_replies.end())
                    continue;
                telehub_replies.push_back(reply.sequence);
                const auto& points = reply.blocks.at("SpawnPointBlock");
                if (std::get<Uuid>(reply.block("TelehubBlock").at("ObjectID")) != expected_hub ||
                    points.size() != expected_points)
                    throw std::runtime_error("Wrong telehub object or spawn point count");
                if (command == "spawnpoint add") {
                    const auto& offset = std::get<std::vector<double>>(points.front().at("SpawnPointPos"));
                    if (std::abs(offset[0] - 32) > .001 || std::abs(offset[1] - 64) > .001 ||
                        std::abs(offset[2]) > .001)
                        throw std::runtime_error("Telehub did not return the relative spawn offset");
                }
                return true;
            }
            return false;
        }, "Viewer did not receive TelehubInfo");
    };
    manage_telehub("info ui", 0, hub.id, 3);
    manage_telehub("connect", placed_hub->local_id, hub.id, 0);
    manage_telehub("spawnpoint add", prim->local_id, hub.id, 1);
    manage_telehub("spawnpoint remove", 0, hub.id, 0);
    manage_telehub("delete", 0, {}, 0);
    set_home(96, denied);

    runtime.world().set_sit_target(prim->id, {.5f, 0, 1}, {0, 0, 0, 1});
    auto sit = udp::make_packet("AgentRequestSit");
    sit.block("AgentData")["AgentID"] = circuit.agent_id;
    sit.block("AgentData")["SessionID"] = circuit.session_id;
    sit.block("TargetObject")["TargetID"] = prim->id;
    transmit(std::move(sit));
    wait([&](const auto& packets) {
        return std::any_of(packets.begin(), packets.end(), [&](const auto& packet) {
            return packet.spec->name == "AvatarSitResponse" &&
                   std::get<Uuid>(packet.block("SitObject").at("ID")) == prim->id;
        });
    }, "Viewer did not receive its sit response");
    const auto avatar_parent = [&](const udp::Packet& packet, std::uint32_t parent) {
        if (packet.spec->name != "ObjectUpdate")
            return false;
        return std::ranges::any_of(packet.blocks.at("ObjectData"), [&](const auto& row) {
            return std::get<Uuid>(row.at("FullID")) == circuit.agent_id &&
                   std::get<std::uint64_t>(row.at("ParentID")) == parent;
        });
    };
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [&](const auto& packet) { return avatar_parent(packet, prim->local_id); });
    }, "Viewer did not receive its parented avatar update");
    const Uuid sit_animation("1a5fe8ac-a804-8a5d-7cbd-56bd83184568");
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [&](const auto& packet) {
            if (packet.spec->name != "AvatarAnimation" ||
                std::get<Uuid>(packet.block("Sender").at("ID")) != circuit.agent_id)
                return false;
            const auto animations = packet.blocks.find("AnimationList");
            return animations != packet.blocks.end() &&
                   std::ranges::any_of(animations->second, [&](const auto& animation) {
                       return std::get<Uuid>(animation.at("AnimID")) == sit_animation;
                   });
        });
    }, "Viewer did not receive the default object-sit animation");
    const auto seated = runtime.viewers().agent(circuit.agent_id);
    if (!seated || !seated->seat)
        throw std::runtime_error("Viewer did not enter the object seat");
    const auto seat_order = seated->seat->order;
    const float turn = std::sqrt(.5f);
    if (!runtime.world().patch_transform(prim->id, Vector3{132, 126, 26},
                                         Quaternion{0, 0, turn, turn}, std::nullopt))
        throw std::runtime_error("Could not move the occupied seat");
    wait([&](const auto&) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->seat && avatar->seat->order == seat_order &&
               std::abs(avatar->position.x - 132.f) < .001f &&
               std::abs(avatar->position.y - 126.5f) < .001f &&
               std::abs(avatar->position.z - 27.35f) < .001f;
    }, "Seated avatar did not follow the translated and rotated linkset");
    const auto new_root = runtime.world().add(SceneObject::primitive(circuit.agent_id, {136, 126, 26}));
    if (!new_root || !runtime.world().link_prim(prim->id, new_root->id))
        throw std::runtime_error("Could not link the occupied prim to a new root");
    wait([&](const auto& packets) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->seat && avatar->seat->order == seat_order &&
               avatar->seat->parent_local == new_root->local_id &&
               std::ranges::any_of(packets, [&](const auto& packet) {
                   return avatar_parent(packet, new_root->local_id);
               });
    }, "Linking the occupied prim did not reparent its sitter");
    std::size_t before_delink;
    {
        std::lock_guard lock(mutex);
        before_delink = received.size();
    }
    if (!runtime.world().delink_child(prim->id, new_root->id))
        throw std::runtime_error("Could not delink the occupied prim");
    wait([&](const auto& packets) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->seat && avatar->seat->order == seat_order &&
               avatar->seat->parent_local == prim->local_id &&
               std::ranges::any_of(std::span(packets).subspan(before_delink), [&](const auto& packet) {
                   return avatar_parent(packet, prim->local_id);
               });
    }, "Delinking the occupied prim did not reparent its sitter");
    std::size_t before_teleport;
    {
        std::lock_guard lock(mutex);
        before_teleport = received.size();
    }
    auto teleport = udp::make_packet("TeleportLocationRequest");
    teleport.block("AgentData")["AgentID"] = circuit.agent_id;
    teleport.block("AgentData")["SessionID"] = circuit.session_id;
    auto& destination = teleport.block("Info");
    destination["RegionHandle"] = runtime.descriptor().handle();
    destination["Position"] = std::vector<double>{134, 128, 40};
    destination["LookAt"] = std::vector<double>{1, 0, 0};
    transmit(std::move(teleport));
    wait([&](const auto& packets) {
        bool cleared_camera = false, arrived = false, unparented = false;
        for (const auto& packet : std::span(packets).subspan(before_teleport)) {
            if (packet.spec->name == "ClearFollowCamProperties")
                cleared_camera |= std::get<Uuid>(packet.block("ObjectData").at("ObjectID")) == prim->id;
            if (packet.spec->name == "TeleportLocal")
                arrived |= std::get<std::vector<double>>(packet.block("Info").at("Position")) ==
                           std::vector<double>{134, 128, 40};
            unparented |= avatar_parent(packet, 0);
        }
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return cleared_camera && arrived && unparented && avatar && !avatar->seat;
    }, "Local teleport did not clear the seat and camera and deliver the destination");
    auto reseat = udp::make_packet("AgentRequestSit");
    reseat.block("AgentData")["AgentID"] = circuit.agent_id;
    reseat.block("AgentData")["SessionID"] = circuit.session_id;
    reseat.block("TargetObject")["TargetID"] = prim->id;
    transmit(std::move(reseat));
    wait([&](const auto&) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->seat && avatar->seat->order != seat_order;
    }, "Viewer did not sit again after local teleport");
    std::size_t before_removal;
    {
        std::lock_guard lock(mutex);
        before_removal = received.size();
    }
    if (!runtime.world().remove(prim->id))
        throw std::runtime_error("Could not remove the occupied seat");
    wait([&](const auto& packets) {
        bool cleared_camera = false, unparented = false;
        for (auto i = before_removal; i < packets.size(); ++i) {
            const auto& packet = packets[i];
            if (packet.spec->name == "ClearFollowCamProperties")
                cleared_camera |= std::get<Uuid>(packet.block("ObjectData").at("ObjectID")) == prim->id;
            unparented |= avatar_parent(packet, 0);
        }
        return cleared_camera && unparented;
    }, "Removing the occupied seat did not clear its camera and unparent the avatar");
    const auto standing = runtime.viewers().agent(circuit.agent_id);
    if (!standing || standing->seat)
        throw std::runtime_error("Removing the occupied seat did not clear the avatar's seat state");

    const auto send_fov = [&](std::uint32_t generation, double angle) {
        auto packet = udp::make_packet("AgentFOV");
        auto& agent = packet.block("AgentData");
        agent["AgentID"] = circuit.agent_id;
        agent["SessionID"] = circuit.session_id;
        agent["CircuitCode"] = std::uint64_t(circuit.circuit_code);
        auto& fov = packet.block("FOVBlock");
        fov["GenCounter"] = std::uint64_t(generation);
        fov["VerticalAngle"] = angle;
        transmit(std::move(packet));
    };
    const auto wait_for_fov = [&](float angle, std::uint32_t generation) {
        wait([&](const auto&) {
            const auto avatar = runtime.viewers().agent(circuit.agent_id);
            return avatar && avatar->camera_fov == angle && avatar->camera_fov_generation == generation;
        }, "Viewer FOV did not match the accepted generation");
    };
    send_fov(2, 1.25);
    wait_for_fov(1.25f, 2);
    send_fov(1, 1.5);

    auto viewport_packet = udp::make_packet("AgentHeightWidth");
    auto& viewport_agent = viewport_packet.block("AgentData");
    viewport_agent["AgentID"] = circuit.agent_id;
    viewport_agent["SessionID"] = circuit.session_id;
    viewport_agent["CircuitCode"] = std::uint64_t(circuit.circuit_code);
    auto& viewport = viewport_packet.block("HeightWidthBlock");
    viewport["Width"] = std::uint64_t{1920};
    viewport["Height"] = std::uint64_t{1080};
    transmit(std::move(viewport_packet));
    wait([&](const auto&) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->view_width == 1920 && avatar->view_height == 1080;
    }, "Viewer viewport dimensions were not stored");
    wait_for_fov(1.25f, 2);
    send_fov(0, 1.75);
    wait_for_fov(1.75f, 0);

    auto camera_packet = udp::make_packet("AgentUpdate");
    auto& camera = camera_packet.block("AgentData");
    camera["AgentID"] = circuit.agent_id;
    camera["SessionID"] = circuit.session_id;
    camera["CameraCenter"] = std::vector<double>{10, 20, 30};
    camera["CameraAtAxis"] = std::vector<double>{-2, 0, 0};
    camera["CameraLeftAxis"] = std::vector<double>{0, -3, 0};
    camera["CameraUpAxis"] = std::vector<double>{0, 0, 4};
    transmit(std::move(camera_packet));
    wait([&](const auto&) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->camera_position == Vector3{10, 20, 30} &&
               avatar->camera_rotation == opensim::framework::Quaternion{0, 0, 1, 0};
    }, "Viewer camera did not use the camera center and normalized axes");

    const auto ground_control = [&](std::uint32_t control) {
        auto update = udp::make_packet("AgentUpdate");
        auto& data = update.block("AgentData");
        data["AgentID"] = circuit.agent_id;
        data["SessionID"] = circuit.session_id;
        data["ControlFlags"] = std::uint64_t(control);
        transmit(std::move(update));
    };
    ground_control(0x20000u);
    const Uuid ground_animation("1a2bd58e-87ff-0df8-0b4c-53e047b0bb6e");
    wait([&](const auto& packets) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        if (!avatar || !avatar->ground_sitting || avatar->seat || avatar->velocity != Vector3{})
            return false;
        return std::ranges::any_of(packets, [&](const auto& packet) {
            if (packet.spec->name != "AvatarAnimation" ||
                std::get<Uuid>(packet.block("Sender").at("ID")) != circuit.agent_id)
                return false;
            const auto animations = packet.blocks.find("AnimationList");
            return animations != packet.blocks.end() &&
                   std::ranges::any_of(animations->second, [&](const auto& animation) {
                       return std::get<Uuid>(animation.at("AnimID")) == ground_animation;
                   });
        });
    }, "Ground sit did not stop movement and send the constrained animation");
    ground_control(0x10000u);
    wait([&](const auto&) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && !avatar->ground_sitting && !avatar->seat;
    }, "Stand control did not clear ground seating");
    const auto check_external_unsit = [&](Uuid owner, bool permitted, Uuid group = {}) {
        ground_control(0x20000u);
        wait([&](const auto&) {
            const auto avatar = runtime.viewers().agent(circuit.agent_id);
            return avatar && avatar->ground_sitting;
        }, "Viewer did not sit on the ground before the permission check");
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        auto fixture = SceneObject::primitive(owner, avatar->position);
        fixture.group = group;
        const auto object = runtime.world().add(std::move(fixture));
        if (!object)
            throw std::runtime_error("Could not create the external unsit fixture prim");
        auto script = std::make_shared<TaskInventoryItem>();
        script->item_id = Uuid::random();
        script->asset_id = unsit_asset;
        script->owner_id = script->creator_id = owner;
        script->parent_id = script->parent_part_id = object->id;
        script->inventory_type = script->type = 10;
        script->name = "External unsit permission fixture";
        script->script_running = true;
        auto settings = std::make_shared<TaskInventoryItem>();
        settings->item_id = Uuid::random();
        settings->asset_id = Uuid("9a848f0e-f7df-49c0-9d79-99341ce1d676");
        settings->owner_id = settings->creator_id = owner;
        settings->parent_id = settings->parent_part_id = object->id;
        settings->inventory_type = 25;
        settings->type = 56;
        settings->name = "Parcel day cycle";
        runtime.restore_task_inventory(object->id, {script, settings});
        const std::string expected_message = owner == administrator
            ? "External unsit complete 127.0.0.1" : "External unsit complete ";
        wait([&](const auto& packets) {
            return std::ranges::any_of(packets, [&](const auto& packet) {
                if (packet.spec->name != "ChatFromSimulator")
                    return false;
                const auto& chat = packet.block("ChatData");
                const auto& message = std::get<std::vector<std::uint8_t>>(chat.at("Message"));
                const std::string line(message.begin(), std::find(message.begin(), message.end(), 0));
                if (std::get<Uuid>(chat.at("SourceID")) != object->id)
                    return false;
                if (line.starts_with("Environment fixture failed"))
                    throw std::runtime_error(line);
                return line == expected_message;
            });
        }, "External unsit script did not complete");
        const auto result = runtime.viewers().agent(circuit.agent_id);
        if (!result || result->ground_sitting == permitted) {
            const std::string authority = owner == administrator ? "administrator" :
                owner == parcel_owner ? "parcel owner" : owner == parcel_group ? "parcel group" : "stranger";
            throw std::runtime_error("External unsit did not honor the parcel authority for " + authority +
                                     " at " + vector3_string(avatar->position));
        }
        runtime.world().remove(object->id);
        if (!permitted) {
            ground_control(0x10000u);
            wait([&](const auto&) {
                const auto standing = runtime.viewers().agent(circuit.agent_id);
                return standing && !standing->ground_sitting;
            }, "Viewer did not stand after the denied unsit");
        }
    };
    check_external_unsit(Uuid::random(), false);
    check_external_unsit(parcel_owner, true);
    check_external_unsit(parcel_group, true, parcel_group);
    check_external_unsit(administrator, true);
    const auto caps = runtime.capabilities()->capabilities().get_caps_for_user(circuit.circuit_code);
    const auto paths = caps->local_paths();
    network::HttpRequest environment_request;
    environment_request.method = "GET";
    environment_request.path = paths.at("ExtEnvironment").value();
    environment_request.raw_target = environment_request.path + "?parcelid=99";
    const auto environment_reply = listener.dispatch(environment_request);
    const auto overridden = network::parse_llsd_xml(environment_reply.body).get("environment");
    if (environment_reply.status != 200 || overridden.get("day_length").integer != 14400 ||
        overridden.get("day_cycle").get("name").as_string() != "Script fixture")
        throw std::runtime_error("Agent override did not take precedence over the requested parcel and newer region timing");
    runtime.world().set_sit_target(new_root->id, {0, 0, 1}, {0, 0, 0, 1});
    auto seat_script = std::make_shared<TaskInventoryItem>();
    seat_script->item_id = Uuid::random();
    seat_script->asset_id = force_sit_asset;
    seat_script->owner_id = seat_script->creator_id = circuit.agent_id;
    seat_script->parent_id = seat_script->parent_part_id = new_root->id;
    seat_script->inventory_type = seat_script->type = 10;
    seat_script->name = "Forced sit fixture";
    seat_script->script_running = true;
    const Uuid animation_id("37aec7d2-5518-4217-9e0c-b6dacb7bc7e6");
    auto animation_item = std::make_shared<TaskInventoryItem>();
    animation_item->item_id = Uuid::random();
    animation_item->asset_id = animation_id;
    animation_item->owner_id = animation_item->creator_id = circuit.agent_id;
    animation_item->parent_id = animation_item->parent_part_id = new_root->id;
    animation_item->inventory_type = 19;
    animation_item->type = 20;
    animation_item->name = "Custom gesture";
    runtime.restore_task_inventory(new_root->id, {seat_script, animation_item});
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [](const auto& packet) {
            if (packet.spec->name != "RegionInfo")
                return false;
            const auto& info = packet.block("RegionInfo");
            return std::get<double>(info.at("WaterHeight")) == 27.125 &&
                   std::get<double>(info.at("TerrainRaiseLimit")) == 56 &&
                   std::get<double>(info.at("TerrainLowerLimit")) == -34;
        });
    }, "Script water update did not broadcast the new water height and existing terrain limits");
    wait([&](const auto& packets) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->seat && avatar->seat->part == new_root->id &&
               std::ranges::any_of(packets, [&](const auto& packet) {
                   return avatar_parent(packet, new_root->local_id);
               });
    }, "Authorized osForceOtherSit did not seat the viewer before logout");
    wait([&](const auto& packets) {
        bool started = false;
        for (const auto& packet : packets) {
            if (packet.spec->name != "AvatarAnimation" ||
                std::get<Uuid>(packet.block("Sender").at("ID")) != circuit.agent_id)
                continue;
            const bool playing = std::ranges::any_of(packet.blocks.at("AnimationList"), [&](const auto& row) {
                return std::get<Uuid>(row.at("AnimID")) == animation_id;
            });
            if (playing) {
                started = std::ranges::any_of(packet.blocks.at("AnimationSourceList"), [&](const auto& row) {
                    return std::get<Uuid>(row.at("ObjectID")) == new_root->id;
                });
            } else if (started) {
                return true;
            }
        }
        return false;
    }, "OSSL animation did not play from prim inventory and stop by asset UUID");
    auto bundled_object = SceneObject::primitive(parcel_owner, {192, 112, 25});
    bundled_object.sale_type = 0;
    bundled_object.sale_price = 125;
    bundled_object.click_action = 2;
    const auto bundled = runtime.world().add(bundled_object);
    if (!bundled)
        throw std::runtime_error("Could not add parcel sale object");
    auto bundled_child = SceneObject::primitive(parcel_owner, {1, 0, 0});
    bundled_child.parent_id = bundled->local_id;
    const auto child = runtime.world().add(bundled_child);
    auto restricted_object = SceneObject::primitive(parcel_owner, {193, 112, 25});
    restricted_object.owner_mask &= ~(1u << 13);
    const auto no_transfer = runtime.world().add(restricted_object);
    const auto outsider = Uuid::random();
    const auto outside_owner = runtime.world().add(SceneObject::primitive(outsider, {194, 112, 25}));
    if (!child || !no_transfer || !outside_owner)
        throw std::runtime_error("Could not add parcel sale comparison objects");
    auto bundled_item = std::make_shared<TaskInventoryItem>();
    bundled_item->item_id = Uuid::random();
    bundled_item->owner_id = parcel_owner;
    bundled_item->parent_id = bundled_item->parent_part_id = child->id;
    bundled_item->type = bundled_item->inventory_type = 7;
    bundled_item->current_permissions = 0x9e000;
    bundled_item->next_permissions = 0x82000;
    runtime.restore_task_inventory(child->id, {bundled_item});
    auto purchase = udp::make_packet("ParcelBuy");
    purchase.block("AgentData")["AgentID"] = circuit.agent_id;
    purchase.block("AgentData")["SessionID"] = circuit.session_id;
    purchase.block("Data")["LocalID"] = std::int64_t{5};
    purchase.block("Data")["Final"] = true;
    transmit(std::move(purchase));
    wait([&](const auto&) {
        return runtime.world().object(bundled->id)->owner == circuit.agent_id;
    }, "Parcel purchase did not transfer the unlisted object");
    const auto sold = runtime.world().object(bundled->id);
    const auto contents_after_sale = runtime.snapshot_task_inventory(child->id);
    if (sold->sale_type != 0 || sold->sale_price != 10 || sold->click_action != 0 ||
        runtime.world().object(child->id)->owner != circuit.agent_id ||
        !contents_after_sale || contents_after_sale->size() != 1 ||
        contents_after_sale->front().owner_id != circuit.agent_id ||
        runtime.world().object(no_transfer->id)->owner != parcel_owner ||
        runtime.world().object(outside_owner->id)->owner != outsider)
        throw std::runtime_error("Parcel purchase did not preserve linkset and object transfer permissions");
    if (!inventory.create_user_inventory(outsider))
        throw std::runtime_error("Could not create the region-return owner's inventory");
    auto scripted_return = SceneObject::primitive(outsider, {224, 112, 25});
    scripted_return.name = "Region scripted return";
    const auto scripted_root = runtime.world().add(scripted_return);
    if (!scripted_root)
        throw std::runtime_error("Could not create the scripted return linkset");
    auto script_child = SceneObject::primitive(outsider, {1, 0, 0});
    script_child.parent_id = scripted_root->local_id;
    const auto scripted_child = runtime.world().add(script_child);
    if (!scripted_child)
        throw std::runtime_error("Could not create the scripted return child");
    duplicate_script(scripted_child->id, false);
    const auto return_region_objects = [&](std::uint64_t flags) {
        auto request = udp::make_packet("SimWideDeletes");
        request.block("AgentData")["AgentID"] = circuit.agent_id;
        request.block("AgentData")["SessionID"] = circuit.session_id;
        request.block("DataBlock")["TargetID"] = outsider;
        request.block("DataBlock")["Flags"] = flags;
        transmit(std::move(request));
    };
    grant_viewer_admin = true;
    return_region_objects(4);
    wait([&](const auto&) { return !runtime.world().object(scripted_root->id); },
         "Region-wide scripted return missed a linkset with a stopped child script");
    if (runtime.world().object(scripted_child->id) || !runtime.world().object(outside_owner->id))
        throw std::runtime_error("Region-wide scripted return selected the wrong objects");
    return_region_objects(0);
    wait([&](const auto&) { return !runtime.world().object(outside_owner->id); },
         "Region-wide return missed the unscripted object");
    grant_viewer_admin = false;
    const auto returned_folder = inventory.for_type(outsider, 16);
    if (!returned_folder || inventory.folder_items(outsider, returned_folder->id).size() != 2)
        throw std::runtime_error("Region-wide return did not deliver both objects to their owner");
    InventoryItem gesture;
    gesture.id = Uuid::random();
    gesture.owner = circuit.agent_id;
    gesture.parent = requested_folder->id;
    gesture.asset_id = Uuid::random();
    gesture.name = "Viewer gesture fixture";
    gesture.asset_type = gesture.inventory_type = 21;
    auto flagged_gesture = gesture;
    flagged_gesture.id = Uuid::random();
    flagged_gesture.flags = 4;
    auto foreign_gesture = gesture;
    foreign_gesture.id = Uuid::random();
    foreign_gesture.owner = outsider;
    foreign_gesture.parent = returned_folder->id;
    if (!inventory.add_item(gesture) || !inventory.add_item(flagged_gesture) ||
        !inventory.add_item(foreign_gesture))
        throw std::runtime_error("Could not add viewer gesture fixtures");
    auto activate = udp::make_packet("ActivateGestures");
    activate.block("AgentData")["AgentID"] = circuit.agent_id;
    activate.block("AgentData")["SessionID"] = circuit.session_id;
    for (const auto id : {gesture.id, flagged_gesture.id, foreign_gesture.id}) {
        auto row = udp::make_block(activate.spec->blocks.at(1));
        row["ItemID"] = id;
        row["AssetID"] = Uuid::random();
        activate.blocks["Data"].push_back(std::move(row));
    }
    transmit(std::move(activate));
    wait([&](const auto&) {
        return inventory.item(circuit.agent_id, flagged_gesture.id)->flags == 5;
    }, "Gesture activation did not preserve existing flags");
    const auto active_gestures = inventory.active_gestures(circuit.agent_id);
    if (std::ranges::find(active_gestures, gesture.id, &InventoryItem::id) == active_gestures.end() ||
        inventory.item(circuit.agent_id, gesture.id)->asset_id != gesture.asset_id ||
        inventory.item(outsider, foreign_gesture.id)->flags != 0)
        throw std::runtime_error("Gesture activation changed the asset or another owner's inventory");
    auto deactivate = udp::make_packet("DeactivateGestures");
    deactivate.block("AgentData")["AgentID"] = circuit.agent_id;
    deactivate.block("AgentData")["SessionID"] = circuit.session_id;
    for (const auto id : {gesture.id, flagged_gesture.id}) {
        auto row = udp::make_block(deactivate.spec->blocks.at(1));
        row["ItemID"] = id;
        deactivate.blocks["Data"].push_back(std::move(row));
    }
    transmit(std::move(deactivate));
    wait([&](const auto&) {
        return inventory.item(circuit.agent_id, gesture.id)->flags == 0 &&
               inventory.item(circuit.agent_id, flagged_gesture.id)->flags == 4;
    }, "Gesture deactivation did not clear only the active flag");
    const auto viewer_folder = Uuid::random();
    const auto viewer_child_folder = Uuid::random();
    const auto folder_packet = [&](std::string_view name) {
        auto packet = udp::make_packet(name);
        packet.block("AgentData")["AgentID"] = circuit.agent_id;
        packet.block("AgentData")["SessionID"] = circuit.session_id;
        return packet;
    };
    const auto folder_data = [](Uuid id, Uuid parent, std::string_view name) -> udp::Block {
        std::vector<std::uint8_t> bytes(name.begin(), name.end());
        bytes.push_back(0);
        return {{"FolderID", id}, {"ParentID", parent}, {"Type", std::int64_t{-1}}, {"Name", std::move(bytes)}};
    };
    auto create_folder = folder_packet("CreateInventoryFolder");
    create_folder.block("FolderData") = folder_data(viewer_folder, requested_folder->id, "Viewer Folder");
    transmit(std::move(create_folder));
    wait([&](const auto&) {
        const auto folder = inventory.folder(circuit.agent_id, viewer_folder);
        return folder && folder->name == "Viewer Folder" && folder->version == 1;
    }, "Viewer folder creation did not reach the inventory store");
    auto rename_folder = folder_packet("UpdateInventoryFolder");
    rename_folder.blocks["FolderData"].push_back(
        folder_data(viewer_folder, requested_folder->id, "Renamed Folder\rignored"));
    transmit(std::move(rename_folder));
    auto move_folder = folder_packet("MoveInventoryFolder");
    move_folder.blocks["InventoryData"] = {
        {{"FolderID", returned_folder->id}, {"ParentID", trash->id}},
        {{"FolderID", viewer_folder}, {"ParentID", trash->id}},
    };
    transmit(std::move(move_folder));
    wait([&](const auto&) {
        const auto folder = inventory.folder(circuit.agent_id, viewer_folder);
        return folder && folder->parent == trash->id && folder->name == "Renamed Folder";
    }, "Viewer folder rename or move did not persist");
    if (inventory.folder(outsider, returned_folder->id)->parent != returned_folder->parent)
        throw std::runtime_error("Viewer moved a folder owned by another account");
    auto create_child_folder = folder_packet("CreateInventoryFolder");
    create_child_folder.block("FolderData") = folder_data(viewer_child_folder, viewer_folder, "Child Folder");
    transmit(std::move(create_child_folder));
    wait([&](const auto&) { return inventory.folder(circuit.agent_id, viewer_child_folder).has_value(); },
         "Viewer folder child was not created");
    auto cyclic_move = folder_packet("MoveInventoryFolder");
    cyclic_move.blocks["InventoryData"] = {{{"FolderID", viewer_folder}, {"ParentID", viewer_child_folder}}};
    transmit(std::move(cyclic_move));
    auto purge_folder = folder_packet("PurgeInventoryDescendents");
    purge_folder.block("InventoryData")["FolderID"] = viewer_folder;
    transmit(std::move(purge_folder));
    wait([&](const auto&) { return !inventory.folder(circuit.agent_id, viewer_child_folder); },
         "Viewer folder purge did not remove descendants");
    if (!inventory.folder(circuit.agent_id, viewer_folder))
        throw std::runtime_error("Viewer folder purge removed its parent");
    std::size_t before_cache_request;
    {
        std::lock_guard lock(mutex);
        before_cache_request = received.size();
    }
    auto cache_miss = folder_packet("RequestMultipleObjects");
    for (const auto local_id : {bundled->local_id, child->local_id, std::uint32_t{0xabcdef}}) {
        auto row = udp::make_block(cache_miss.spec->blocks.at(1));
        row["ID"] = std::uint64_t(local_id);
        cache_miss.blocks["ObjectData"].push_back(std::move(row));
    }
    transmit(std::move(cache_miss));
    wait([&](const auto& packets) {
        bool root_update = false, child_update = false;
        for (std::size_t i = before_cache_request; i < packets.size(); ++i) {
            if (packets[i].spec->name != "ObjectUpdate")
                continue;
            for (const auto& row : packets[i].blocks.at("ObjectData")) {
                const auto id = std::get<Uuid>(row.at("FullID"));
                root_update |= id == bundled->id;
                child_update |= id == child->id && std::get<std::uint64_t>(row.at("ParentID")) == bundled->local_id;
            }
        }
        return root_update && child_update;
    }, "Viewer cache miss did not receive full updates for both requested prims");
    const auto mesh_animation_id = Uuid::random();
    auto animated_object = SceneObject::primitive(circuit.agent_id, {128, 128, 26});
    animated_object.extra_params = {1, 0x70, 0, 4, 0, 0, 0, 1, 0, 0, 0};
    animated_object.object_animations = {1, 0};
    animated_object.object_animations.insert(animated_object.object_animations.end(),
                                             mesh_animation_id.bytes().begin(), mesh_animation_id.bytes().end());
    animated_object.object_animations.insert(animated_object.object_animations.end(), {3, 'r', 'u', 'n'});
    const std::string material_xml = "<llsd><map><key>x</key><integer>3</integer></map></llsd>";
    animated_object.material_overrides = {1, 2, static_cast<std::uint8_t>(material_xml.size()), 0};
    animated_object.material_overrides.insert(animated_object.material_overrides.end(),
                                              material_xml.begin(), material_xml.end());
    TextureEntry material_textures(TextureEntry::white_texture);
    material_textures.default_texture->material_id(legacy_material_id);
    material_textures.create_face(4)->material_id(empty_legacy_material_id);
    const auto missing_face_material = Uuid::random();
    material_textures.create_face(5)->material_id(missing_face_material);
    animated_object.texture_entry = material_textures.bytes(9);
    animated_object.dynamic_attributes_xml =
        "<llsd><map><key>OpenSim</key><map><key>OtherStore</key><map><key>note</key><string>keep</string></map>"
        "<key>Materials</key><map><key>Materials</key><array><map><key>ID</key><uuid>" +
        legacy_material_id.str() + "</uuid><key>Material</key><map><key>DiffuseAlphaMode</key><integer>2</integer>"
        "<key>NormRepeatX</key><integer>10000</integer><key>SpecColor</key><array>"
        "<integer>32</integer><integer>64</integer><integer>128</integer><integer>255</integer>"
        "</array></map></map></array></map></map></map></llsd>";
    const auto animated_part = runtime.world().add(animated_object, true, false);
    if (!animated_part)
        throw std::runtime_error("Animated cache miss fixture could not be added");
    const auto animation_caps = std::dynamic_pointer_cast<Caps>(caps);
    if (!animation_caps)
        throw std::runtime_error("Animated cache miss fixture has no viewer capabilities");
    animation_caps->flags |= Caps::ObjectAnim | Caps::PBR;

    const auto receive_animated_prim = [&](const auto& trigger) {
        std::size_t first_reply;
        {
            std::lock_guard lock(mutex);
            first_reply = received.size();
        }
        trigger();

        std::int64_t sequence = 0;
        const auto expected_material = "{'id':i" + std::to_string(animated_part->local_id) +
                                       ",'te':[i2],'od':[" + material_xml + "]}";
        wait([&](const auto& packets) {
            bool material_received = false;
            bool object_received = false;
            for (std::size_t i = first_reply; i < packets.size(); ++i) {
                const auto& reply = packets[i];
                if (reply.spec->name == "ObjectUpdate") {
                    for (const auto& row : reply.blocks.at("ObjectData"))
                        object_received |= std::get<Uuid>(row.at("FullID")) == animated_part->id;
                    continue;
                }
                if (reply.spec->name == "GenericStreamingMessage") {
                    const auto& bytes = std::get<std::vector<std::uint8_t>>(reply.block("DataBlock").at("Data"));
                    material_received |= reply.reliable &&
                                         std::get<std::uint64_t>(reply.block("MethodData").at("Method")) == 16757 &&
                                         std::string(bytes.begin(), bytes.end()) == expected_material;
                    continue;
                }
                if (reply.spec->name != "ObjectAnimation" ||
                    std::get<Uuid>(reply.block("Sender").at("ID")) != animated_part->id)
                    continue;
                const auto& animations = reply.blocks.at("AnimationList");
                if (!reply.reliable || animations.size() != 1 ||
                    std::get<Uuid>(animations.front().at("AnimID")) != mesh_animation_id)
                    throw std::runtime_error("Animated cache miss returned incorrect animation data");
                sequence = std::get<std::int64_t>(animations.front().at("AnimSequenceID"));
            }
            return object_received && sequence > 0 && material_received;
        }, "Viewer did not receive the full prim, animation and material override updates");
        return sequence;
    };
    const auto published_sequence =
        receive_animated_prim([&] { runtime.world().publish_added_linkset(animated_part->id); });
    const auto migrated_part = runtime.world().object(animated_part->id);
    if (!migrated_part)
        throw std::runtime_error("Embedded material fixture disappeared");
    TextureEntry migrated_textures(migrated_part->texture_entry);
    if (!migrated_textures.get_face(4)->material_id().is_zero() ||
        migrated_textures.get_face(5)->material_id() != missing_face_material)
        throw std::runtime_error("Object load did not distinguish empty from missing legacy materials");
    const auto remaining_stores = network::parse_llsd_xml(migrated_part->dynamic_attributes_xml).get("OpenSim");
    if (!remaining_stores.contains("Materials") || remaining_stores.get("OtherStore").get("note").text != "keep" ||
        uploaded_asset(legacy_material_id))
        throw std::runtime_error("Embedded source data did not survive while material storage was pending");
    const auto request_cached_prim = [&] {
        auto request = folder_packet("RequestMultipleObjects");
        auto row = udp::make_block(request.spec->blocks.at(1));
        row["ID"] = std::uint64_t(animated_part->local_id);
        request.blocks["ObjectData"].push_back(std::move(row));
        transmit(std::move(request));
    };
    if (receive_animated_prim(request_cached_prim) != published_sequence ||
        receive_animated_prim(request_cached_prim) != published_sequence)
        throw std::runtime_error("Cache misses changed the published animation sequence");

    auto animation_script = std::make_shared<TaskInventoryItem>();
    animation_script->item_id = Uuid::random();
    animation_script->asset_id = object_animation_script;
    animation_script->owner_id = animation_script->creator_id = circuit.agent_id;
    animation_script->parent_id = animation_script->parent_part_id = animated_part->id;
    animation_script->inventory_type = animation_script->type = 10;
    animation_script->name = "Object animation script";
    animation_script->script_running = true;
    auto scripted_animation = std::make_shared<TaskInventoryItem>(*animation_script);
    scripted_animation->item_id = Uuid::random();
    scripted_animation->asset_id = Uuid::random();
    scripted_animation->inventory_type = 19;
    scripted_animation->type = 20;
    scripted_animation->name = "Scripted mesh animation";
    runtime.restore_task_inventory(animated_part->id, {animation_script, scripted_animation});
    wait([&](const auto& packets) {
        bool started = false, stopped = false, names_reported = false;
        for (const auto& packet : packets) {
            if (packet.spec->name == "ChatFromSimulator") {
                const auto& message = packet.block("ChatData").at("Message");
                const auto& bytes = std::get<std::vector<std::uint8_t>>(message);
                names_reported |= framework::sdk_bytes_to_string(bytes) == u"Scripted mesh animation";
            }
            if (packet.spec->name != "ObjectAnimation" ||
                std::get<Uuid>(packet.block("Sender").at("ID")) != animated_part->id)
                continue;
            bool playing = false;
            for (const auto& row : packet.blocks.at("AnimationList")) {
                const auto id = std::get<Uuid>(row.at("AnimID"));
                if (id == mesh_animation_id && std::get<std::int64_t>(row.at("AnimSequenceID")) != published_sequence)
                    throw std::runtime_error("Script edit changed the sequence of an existing animation");
                playing |= id == scripted_animation->asset_id;
            }
            if (playing)
                started = true;
            else if (started)
                stopped = true;
        }
        return started && stopped && names_reported;
    }, "Object animation script did not start, list and stop the inventory animation");

    auto falling_box = SceneObject::primitive(circuit.agent_id, {145, 123, 28});
    falling_box.flags = 1u;
    const auto falling_part = runtime.world().add(falling_box);
    if (!falling_part)
        throw std::runtime_error("Could not rez the viewer physics fixture");
    wait([&](const auto& packets) {
        const auto read_u32 = [](const auto& bytes, std::size_t offset) {
            return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
                   (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
        };
        return std::ranges::any_of(packets, [&](const auto& packet) {
            if (packet.spec->name != "ImprovedTerseObjectUpdate")
                return false;
            return std::ranges::any_of(packet.blocks.at("ObjectData"), [&](const auto& row) {
                const auto& bytes = std::get<std::vector<std::uint8_t>>(row.at("Data"));
                return bytes.size() >= 18 && read_u32(bytes, 0) == falling_part->local_id &&
                       std::bit_cast<float>(read_u32(bytes, 14)) < falling_box.position.z - .1f;
            });
        });
    }, "Viewer did not receive the solver-driven box position");
    runtime.world().remove(falling_box.id);

    auto crossing_root = SceneObject::primitive(circuit.agent_id, {250, 120, 26});
    crossing_root.name = "Crossing accepted";
    crossing_root.object_animations = animated_object.object_animations;
    const auto crossing_part = runtime.world().add(crossing_root);
    if (!crossing_part)
        throw std::runtime_error("Could not create outgoing crossing fixture");
    auto crossing_child = SceneObject::primitive(circuit.agent_id, {1, 0, 0});
    crossing_child.parent_id = crossing_part->local_id;
    if (!runtime.world().add(crossing_child))
        throw std::runtime_error("Could not create outgoing crossing child");
    auto stopped_crossing_script = std::make_shared<TaskInventoryItem>(*animation_script);
    stopped_crossing_script->item_id = Uuid::random();
    stopped_crossing_script->parent_id = stopped_crossing_script->parent_part_id = crossing_child.id;
    stopped_crossing_script->script_running = false;
    runtime.restore_task_inventory(crossing_child.id, {stopped_crossing_script});
    auto rejected_root = SceneObject::primitive(circuit.agent_id, {249, 120, 26});
    rejected_root.name = "Crossing rejected";
    const auto rejected_part = runtime.world().add(rejected_root);
    if (!rejected_part)
        throw std::runtime_error("Could not create rejected crossing fixture");
    const auto move_across_border = [&](std::uint32_t local_id) {
        auto request = folder_packet("ObjectPosition");
        request.blocks["ObjectData"].push_back(
            {{"ObjectLocalID", std::uint64_t(local_id)}, {"Position", std::vector<double>{260, 120, 26}}});
        transmit(std::move(request));
    };
    move_across_border(rejected_part->local_id);
    move_across_border(crossing_part->local_id);
    wait([&](const auto& packets) {
        return !runtime.world().object(crossing_root.id) && !runtime.world().object(crossing_child.id) &&
               std::ranges::any_of(packets, [&](const auto& packet) {
                   if (packet.spec->name != "ImprovedTerseObjectUpdate")
                       return false;
                   return std::ranges::any_of(packet.blocks.at("ObjectData"), [&](const auto& row) {
                       const auto& bytes = std::get<std::vector<std::uint8_t>>(row.at("Data"));
                       const auto local = rejected_part->local_id;
                       return bytes.size() >= 4 && bytes[0] == (local & 255) &&
                              bytes[1] == ((local >> 8) & 255) && bytes[2] == ((local >> 16) & 255) &&
                              bytes[3] == (local >> 24);
                   });
               });
    }, "Object crossing did not remove the accepted linkset and correct the rejected viewer position");
    const auto rejected = runtime.world().object(rejected_root.id);
    if (!rejected || rejected->position != rejected_root.position)
        throw std::runtime_error("Rejected outgoing crossing changed the source object");
    runtime.world().remove(rejected_root.id);

    const auto incoming_id = Uuid::random();
    const auto incoming_item = Uuid::random();
    const auto incoming_xml = "<SceneObjectGroup><SceneObjectPart><UUID><UUID>" + incoming_id.str() +
        "</UUID></UUID><OwnerID><UUID>" + circuit.agent_id.str() +
        "</UUID></OwnerID><TaskInventory><TaskInventoryItem><ItemID><UUID>" + incoming_item.str() +
        "</UUID></ItemID><AssetID><UUID>" + object_animation_script.str() +
        "</UUID></AssetID><InvType>10</InvType><Type>10</Type></TaskInventoryItem></TaskInventory>"
        "</SceneObjectPart><OtherParts /></SceneObjectGroup>";
    const auto incoming_state = [&](Uuid asset, std::string_view running) {
        return "<State Engine=\"YEngine\" UUID=\"" + incoming_item.str() + "\" Asset=\"" + asset.str() +
               "\"><ScriptState><Running>" + std::string(running) + "</Running></ScriptState></State>";
    };
    network::HttpResponse crossing_reply;
    server::ObjectHandler(&runtime).post_object(network::Value::object({
        {"destination_uuid", runtime.descriptor().id.str()},
        {"sog", incoming_xml},
        {"new_position", "<260,120,26>"},
        {"state", "<ScriptData><ScriptStates>" + incoming_state(object_animation_script, "False") +
                  incoming_state(Uuid::random(), "True") + "</ScriptStates></ScriptData>"},
    }), crossing_reply);
    const auto incoming_inventory = runtime.snapshot_task_inventory(incoming_id);
    if (crossing_reply.body != "True" || !incoming_inventory || incoming_inventory->size() != 1 ||
        incoming_inventory->front().script_running)
        throw std::runtime_error("Incoming script crossing did not retain the stopped state for the matching asset");
    runtime.world().remove(incoming_id);

    auto script_crossing = SceneObject::primitive(circuit.agent_id, {246, 120, 26});
    script_crossing.name = "Crossing script";
    const auto script_crossing_part = runtime.world().add(script_crossing);
    if (!script_crossing_part)
        throw std::runtime_error("Could not create scripted crossing fixture");
    auto crossing_script_item = std::make_shared<TaskInventoryItem>(*animation_script);
    crossing_script_item->item_id = Uuid::random();
    crossing_script_item->parent_id = crossing_script_item->parent_part_id = script_crossing_part->id;
    runtime.restore_task_inventory(script_crossing_part->id, {crossing_script_item});
    wait([&](const auto&) { return !runtime.world().object(script_crossing.id); },
         "llSetPos did not cross after the capped move and llSetRegionPos checks");

    network::HttpRequest material_request;
    material_request.method = "POST";
    material_request.path = paths.at("ModifyMaterialParams").value();
    const auto edited_asset = Uuid::random();
    material_request.body = "<llsd><array><map><key>object_id</key><uuid>" + animated_part->id.str() +
                            "</uuid><key>side</key><integer>3</integer><key>asset_id</key><uuid>" +
                            edited_asset.str() + "</uuid><key>gltf_json</key>"
                            "<string>{\"materials\":[{\"alphaMode\":\"MASK\",\"doubleSided\":true}]}</string>"
                            "</map></array></llsd>";
    const auto material_reply = listener.dispatch(material_request);
    if (material_reply.status != 200 || network::parse_llsd_xml(material_reply.body).get("success").integer != 1)
        throw std::runtime_error("Live material edit was rejected");
    const auto edited_part = runtime.world().object(animated_part->id);
    if (!edited_part || edited_part->material_overrides.empty() || edited_part->material_overrides.front() != 2 ||
        std::search(edited_part->extra_params.begin(), edited_part->extra_params.end(),
                    edited_asset.bytes().begin(), edited_asset.bytes().end()) == edited_part->extra_params.end())
        throw std::runtime_error("Live material edit did not update the stored face asset and override");
    const auto edited_notation = "{'id':i" + std::to_string(animated_part->local_id) + ",'te':[i2,i3],'od':[" +
                                 material_xml + ",{'am':i2,'ds':t}]}";
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [&](const auto& packet) {
            if (packet.spec->name != "GenericStreamingMessage")
                return false;
            const auto& bytes = std::get<std::vector<std::uint8_t>>(packet.block("DataBlock").at("Data"));
            return std::string(bytes.begin(), bytes.end()) == edited_notation;
        });
    }, "Live material edit did not broadcast both face overrides");

    network::HttpRequest legacy_request;
    legacy_request.path = paths.at("RenderMaterials").value();
    const auto query_legacy_materials = [&] {
        const auto reply = listener.dispatch(legacy_request);
        if (reply.status != 200)
            throw std::runtime_error("Legacy material capability query failed");
        const auto wrapper = network::parse_llsd_xml(reply.body);
        const auto zipped = network::osd_binary(wrapper.get("Zipped"));
        return network::deserialize_zipped_llsd_binary(
            {reinterpret_cast<const char*>(zipped->data()), zipped->size()});
    };
    const auto check_material = [](const network::Value& row) {
        const auto id = network::osd_binary(row.get("ID"));
        const auto& material = row.get("Material");
        return std::ranges::equal(*id, legacy_material_id.bytes()) &&
               material.get("DiffuseAlphaMode").integer == 2 &&
               material.get("NormRepeatX").integer == 10000 &&
               material.get("SpecColor").array.size() == 4 &&
               material.get("SpecColor").array[0].integer == 32;
    };
    legacy_request.method = "GET";
    const auto listed = query_legacy_materials();
    if (listed.array.size() != 1 || !check_material(listed.array.front()))
        throw std::runtime_error("Legacy GET did not list the region material");
    legacy_request.method = "POST";
    // Bundled SDK binary LLSD for [known ID, unknown ID, known ID], compressed with zlib.
    legacy_request.body = "<llsd><map><key>Zipped</key><binary>"
                          "eJyLZmBgYE5iYGAQEDIJqxAygcCwill7wIKr0ABWlbEAjewVUg=="
                          "</binary></map></llsd>";
    const auto selected = query_legacy_materials();
    if (selected.array.size() != 2 || !std::ranges::all_of(selected.array, check_material))
        throw std::runtime_error("Legacy POST did not preserve known requests and skip the unknown ID");
    legacy_request.body = "<llsd><map><key>Zipped</key><binary>AA==</binary></map></llsd>";
    const auto malformed_reply = listener.dispatch(legacy_request);
    if (malformed_reply.status != 200 || !malformed_reply.body.empty())
        throw std::runtime_error("Legacy material dispatcher did not preserve its malformed request response");

    const auto put_legacy_material = [&](network::Value material) {
        auto face = network::Value::object({
            {"ID", network::Value(static_cast<std::int32_t>(animated_part->local_id))},
            {"Face", network::Value(std::int32_t{3})},
            {"Material", std::move(material)},
        });
        network::Value edits;
        edits.kind = network::Value::Kind::Array;
        edits.array.push_back(std::move(face));
        const auto packed = network::serialize_zipped_llsd_binary(
            network::Value::object({{"FullMaterialsPerFace", std::move(edits)}}));
        const auto zipped = network::osd_from_binary(
            std::make_shared<std::vector<std::uint8_t>>(packed.begin(), packed.end()));
        legacy_request.method = "PUT";
        legacy_request.body = network::llsd_xml(network::Value::object({{"Zipped", zipped}}));
        const auto response = listener.dispatch(legacy_request);
        if (response.status != 200 ||
            response.body != "<llsd><map><key>Zipped</key><binary>eNqLZgCCWAAChQC5</binary></map></llsd>")
            throw std::runtime_error("Legacy material PUT did not return the original empty result");
    };
    put_legacy_material(listed.array.front().get("Material"));
    const auto assigned_part = runtime.world().object(animated_part->id);
    if (uploaded_asset(generated_material) || !assigned_part ||
        TextureEntry(assigned_part->texture_entry).get_face(3)->material_id() != generated_material)
        throw std::runtime_error("Legacy material PUT did not assign the SDK UUID with deferred storage");
    network::Value pending_query;
    pending_query.kind = network::Value::Kind::Array;
    pending_query.array.push_back(network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(
        generated_material.bytes().begin(), generated_material.bytes().end())));
    const auto pending_bytes = network::serialize_zipped_llsd_binary(pending_query);
    const auto pending_zip = network::osd_from_binary(
        std::make_shared<std::vector<std::uint8_t>>(pending_bytes.begin(), pending_bytes.end()));
    legacy_request.method = "POST";
    legacy_request.body = network::llsd_xml(network::Value::object({{"Zipped", pending_zip}}));
    const auto pending = query_legacy_materials();
    if (pending.array.size() != 1 ||
        !std::ranges::equal(*network::osd_binary(pending.array.front().get("ID")), generated_material.bytes()))
        throw std::runtime_error("Legacy POST did not serve a pending material");
    put_legacy_material(network::Value{});
    const auto cleared_part = runtime.world().object(animated_part->id);
    if (!cleared_part || !TextureEntry(cleared_part->texture_entry).get_face(3)->material_id().is_zero())
        throw std::runtime_error("Legacy material PUT did not clear the face material");

    const auto missing_group = Uuid::random();
    auto group_names = udp::make_packet("UUIDGroupNameRequest");
    group_names.blocks["UUIDNameBlock"] = {{{"ID", parcel_group}}, {{"ID", missing_group}}};
    transmit(std::move(group_names));
    wait([&](const auto& packets) {
        bool known = false, unknown = false;
        for (const auto& packet : packets) {
            if (packet.spec->name != "UUIDGroupNameReply")
                continue;
            const auto& row = packet.block("UUIDNameBlock");
            const auto id = std::get<Uuid>(row.at("ID"));
            const auto name = framework::sdk_bytes_to_string(std::get<std::vector<std::uint8_t>>(row.at("GroupName")));
            known |= id == parcel_group && name == u"Fixture Group";
            unknown |= id == missing_group && name == u"Unknown";
        }
        return known && unknown;
    }, "Viewer group-name requests did not distinguish existing and missing records");
    const auto before_logout = parcel_refreshes.load();
    auto logout = udp::make_packet("LogoutRequest");
    logout.block("AgentData")["AgentID"] = circuit.agent_id;
    logout.block("AgentData")["SessionID"] = circuit.session_id;
    transmit(std::move(logout));
    wait([&](const auto&) {
        return !runtime.viewers().agent(circuit.agent_id) && parcel_refreshes.load() > before_logout;
    }, "Seated logout did not release the viewer and refresh the parcel count");
    return animated_part->id;
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 4)
            throw std::runtime_error("Expected original ubODE library and animation catalog paths");
        FailingPresence presence;
        auto listener = std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")});
        GridRegion region;
        region.id = Uuid::random();
        region.name = "Cleanup";
        region.estate_owner = Uuid::random();
        region.external_host = "127.0.0.1";
        region.internal_endpoint = RegionEndpoint{"127.0.0.1", 0};
        RegionPolicies policies;
        policies.environment_enabled = true;
        policies.estate_menu_flags |= 0x200u;
        std::vector<std::pair<std::optional<Uuid>, std::string>> environment_writes;
        policies.persist_environment = [&](auto parcel, auto settings) {
            if (!settings)
                throw std::runtime_error("Unexpected environment write");
            environment_writes.emplace_back(parcel, std::move(*settings));
        };
        std::vector<std::array<float, 3>> terrain_settings_writes;
        policies.persist_region_terrain_settings = [&](float water, float raise, float lower) {
            terrain_settings_writes.push_back({water, raise, lower});
        };
        std::atomic<unsigned> parcel_refreshes = 0;
        policies.parcel_prim_count_tainted = [&] { ++parcel_refreshes; };
        policies.physics_library = argv[1];
        policies.animations = std::make_shared<DefaultAnimations>(argv[2]);
        if (argc == 4)
            policies.spawn_point_routing = argv[3];
        policies.use_trash_on_delete = policies.spawn_point_routing != "sequence";
        policies.access = [routing = policies.spawn_point_routing](auto, const auto& position, auto& reason) {
            if (position.x == 128 || (position.x == 96 && routing == "random")) {
                reason = "Fixture spawn position denied";
                return false;
            }
            return true;
        };
        policies.authorize = [](const auto&, auto&) { return true; };
        const auto covenant = std::make_shared<AssetBase>(Uuid::random(), "Fixture covenant",
                                                          static_cast<std::int8_t>(AssetType::Notecard), "");
        covenant->data = std::make_shared<std::vector<std::uint8_t>>(785, 0);
        for (std::size_t i = 0; i < covenant->data->size(); ++i)
            (*covenant->data)[i] = static_cast<std::uint8_t>(i % 251);
        const auto library_item = Uuid::random();
        InventoryFolderStore inventory_store(":memory:", true);
        InventoryService inventory(inventory_store);
        policies.inventory_service = &inventory;
        policies.can_edit = [](Uuid agent, const SceneObject& object) { return agent == object.owner; };
        policies.can_move = policies.can_edit;
        auto crossing_destination = region;
        crossing_destination.id = Uuid::random();
        crossing_destination.x += 256;
        policies.teleport_destination = [crossing_destination](std::uint64_t) {
            return std::optional{crossing_destination};
        };
        policies.send_crossing_object = [crossing_destination](GridRegion& destination, Vector3 position,
                                                               std::shared_ptr<SimulationObject> transfer) {
            const auto xml = transfer->to_xml2();
            const auto asset = read_object_asset_xml(
                {reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size()});
            if (!asset || destination.id != crossing_destination.id)
                return false;
            const auto& parts = asset->linksets.front().parts;
            const auto state = transfer->state_snapshot();
            if (parts.size() == 1 && parts.front().object.name == "Crossing script")
                return position == Vector3{5, 120, 26} && parts.front().object.position == Vector3{251, 120, 26} &&
                       state && state->find("<NativeCheckpoint>") != std::string::npos;
            return xml.starts_with("<SceneObjectGroup><SceneObjectPart>") &&
                   position == Vector3{4, 120, 26} && parts.size() == 2 &&
                   parts.front().object.name == "Crossing accepted" &&
                   !parts.front().object.object_animations.empty() && parts.back().inventory.size() == 1 &&
                   state && state->find(parts.back().inventory.front().item_id.str()) != std::string::npos &&
                   state->find("<Running>False</Running>") != std::string::npos;
        };
        policies.can_rez = [](Uuid, const Vector3&, std::string&) { return true; };
        policies.minimum_asset_upload_level = 50;
        policies.asset_upload_user_level = [](Uuid) { return 10; };
        const auto group = Uuid::random();
        const auto hub_group = Uuid::random();
        const auto other_group = Uuid::random();
        policies.group_record_name = [other_group](Uuid id) -> std::optional<std::string> {
            if (id == other_group)
                return "Fixture Group";
            return std::nullopt;
        };
        auto group_parcel = std::make_shared<ScriptParcel>();
        group_parcel->id = Uuid::random();
        group_parcel->group = group;
        auto hub_parcel = std::make_shared<ScriptParcel>();
        hub_parcel->id = Uuid::random();
        hub_parcel->group = hub_group;
        hub_parcel->group_owned = true;
        hub_parcel->local_id = 2;
        hub_parcel->dwell = 40;
        auto other_parcel = std::make_shared<ScriptParcel>();
        other_parcel->id = Uuid::random();
        other_parcel->group = other_group;
        other_parcel->owner = Uuid::random();
        other_parcel->group_owned = true;
        other_parcel->local_id = 3;
        other_parcel->media_url = "https://video.example/media";
        other_parcel->media_type = "video/mp4";
        other_parcel->media_description = "Parcel video";
        other_parcel->media_width = 640;
        other_parcel->media_height = 360;
        other_parcel->media_id = Uuid("12345678-1234-4321-8123-123456789abc");
        std::atomic<std::shared_ptr<const ScriptParcel>> current_other_parcel{other_parcel};
        policies.update_parcel_music = [&](std::shared_ptr<const ScriptParcel> parcel) {
            if (parcel->id == other_parcel->id)
                current_other_parcel.store(std::move(parcel));
        };
        policies.update_parcel_media = policies.update_parcel_music;
        auto sale_parcel = std::make_shared<ScriptParcel>();
        sale_parcel->id = Uuid::random();
        sale_parcel->local_id = 5;
        sale_parcel->owner = other_parcel->owner;
        sale_parcel->flags = 1u << 16;
        std::atomic<std::shared_ptr<const ScriptParcel>> current_sale_parcel{sale_parcel};
        policies.update_parcel_purchase = [&](std::shared_ptr<const ScriptParcel> parcel) {
            if (parcel->id == sale_parcel->id)
                current_sale_parcel.store(std::move(parcel));
        };
        policies.sample_economy_data = network::lludp::make_packet("EconomyData");
        auto return_parcel = std::make_shared<ScriptParcel>();
        return_parcel->id = Uuid::random();
        return_parcel->group = Uuid::random();
        return_parcel->owner = Uuid::random();
        return_parcel->local_id = 4;
        return_parcel->other_clean_time = 1;
        auto dwell_time = std::chrono::steady_clock::time_point{};
        std::vector<std::pair<Uuid, float>> dwell_writes;
        policies.parcel_dwell_enabled = true;
        policies.parcel_dwell_clock = [&] { return dwell_time; };
        policies.persist_parcel_dwell = [&](Uuid parcel, float dwell) { dwell_writes.emplace_back(parcel, dwell); };
        policies.list_parcels = [=, &current_other_parcel, &current_sale_parcel] {
            return std::vector<std::shared_ptr<const ScriptParcel>>{
                group_parcel, hub_parcel, current_other_parcel.load(), return_parcel, current_sale_parcel.load()};
        };
        const auto blocked_visitor = Uuid::random();
        const auto restricted_crosser = Uuid::random();
        constexpr std::uint32_t use_ban_list = 1u << 10;
        for (const auto& parcel : {hub_parcel, other_parcel}) {
            parcel->flags |= use_ban_list;
            parcel->access_entries.push_back({blocked_visitor, 2, 0});
        }
        hub_parcel->access_entries.push_back({restricted_crosser, 2, 0});
        std::atomic<bool> grant_viewer_admin = false;
        policies.god_rights_level = [blocked_visitor, estate_owner = region.estate_owner, &grant_viewer_admin](Uuid agent) {
            return agent == blocked_visitor || (agent == estate_owner && grant_viewer_admin) ? 200 : 0;
        };
        policies.script_account = [estate_owner = region.estate_owner, &grant_viewer_admin](Uuid agent)
            -> std::optional<UserAccount> {
            if (agent != estate_owner || !grant_viewer_admin)
                return std::nullopt;
            UserAccount account(agent);
            account.user_level = 200;
            return account;
        };
        const auto hub = SceneObject::primitive(Uuid::random(), {96, 64, 25});
        policies.script_parcel_at = [=, &current_other_parcel, &current_sale_parcel](Vector3 position)
            -> std::shared_ptr<const ScriptParcel> {
            if (position.x < 64)
                return group_parcel;
            if (position.x < 128)
                return hub_parcel;
            if (position.x >= 200)
                return return_parcel;
            if (position.x >= 192)
                return current_sale_parcel.load();
            return current_other_parcel.load();
        };
        policies.inventory_group_powers = [group, hub_group](Uuid, Uuid parcel_group) {
            if (parcel_group == hub_group)
                return 1ULL << 22; // LandOptions, without AllowSetHome.
            return parcel_group == group ? 1ULL << 28 : 0ULL;
        };
        std::mutex home_mutex;
        std::vector<Vector3> home_writes;
        std::vector<std::pair<Uuid, std::size_t>> telehub_writes;
        policies.persist_telehub = [&](Uuid id, const auto& points) {
            std::lock_guard lock(home_mutex);
            telehub_writes.emplace_back(id, points.size());
        };
        policies.set_home = [&](Uuid, Uuid region_id, Vector3 position, Vector3 look) {
            if (region_id != region.id || look != Vector3{0, 1, 0})
                return false;
            std::lock_guard lock(home_mutex);
            home_writes.push_back(position);
            return true;
        };
        std::mutex uploaded_mutex;
        std::map<Uuid, std::shared_ptr<AssetBase>> uploads;
        const auto region_thread = std::this_thread::get_id();
        std::atomic<bool> material_stored_in_background = false;
        std::atomic<bool> embedded_store_rejected = false;
        std::atomic<bool> unused_material_expired = false;
        policies.expire_material_asset_cache = [&](Uuid id) {
            if (id == generated_material && material_stored_in_background)
                unused_material_expired = true;
        };
        std::vector<std::pair<InventoryItem, int>> upload_events;
        bool reject_return_asset = true;
        bool return_asset_rejected = false;
        policies.inventory_upload_complete = [&](const InventoryItem& item, int level) {
            upload_events.emplace_back(item, level);
        };
        policies.store_inventory_asset = [&](auto asset) -> std::optional<std::string> {
            std::lock_guard lock(uploaded_mutex);
            if (reject_return_asset && asset->name() == "Expired fixture linkset") {
                return_asset_rejected = true;
                return std::nullopt;
            }
            const auto id = asset->full_id();
            if (id == legacy_material_id && policies.spawn_point_routing == "sequence" &&
                !embedded_store_rejected.exchange(true))
                return std::nullopt;
            if (id == generated_material)
                material_stored_in_background = std::this_thread::get_id() != region_thread;
            uploads.emplace(id, std::move(asset));
            return id.str();
        };
        const auto uploaded_asset = [&](Uuid id) -> std::shared_ptr<AssetBase> {
            std::lock_guard lock(uploaded_mutex);
            const auto entry = uploads.find(id);
            return entry == uploads.end() ? nullptr : entry->second;
        };
        policies.store_asset = [&](auto asset) {
            std::lock_guard lock(uploaded_mutex);
            const auto id = asset->full_id();
            uploads.try_emplace(id, std::move(asset));
        };
        const auto unsit_script = std::make_shared<AssetBase>(Uuid::random(), "External unsit",
                                                              static_cast<std::int8_t>(AssetType::LSLText), "");
        const auto force_sit_script = std::make_shared<AssetBase>(
            Uuid::random(), "Forced sit", static_cast<std::int8_t>(AssetType::LSLText), "");
        const auto script_day_cycle = std::make_shared<AssetBase>(
            Uuid("9a848f0e-f7df-49c0-9d79-99341ce1d676"), "Script day cycle",
            static_cast<std::int8_t>(AssetType::Settings), "");
        const std::string_view day_cycle_data = "<? llsd/notation ?>\n"
            "{'type':'daycycle','name':'Script fixture','frames':{'Sky':{'type':'sky'}},"
            "'tracks':[[],[{'key_keyframe':r0,'key_name':'Sky'}],[],[],[]]}";
        script_day_cycle->data = std::make_shared<std::vector<std::uint8_t>>(
            day_cycle_data.begin(), day_cycle_data.end());
        policies.script_ossl_threat_level = "None";
        policies.script_ossl_rules["osForceOtherSit"].owners = "true";
        policies.script_ossl_rules["osGetAgentIP"].owners = "true";
        policies.script_ossl_rules["osAvatarPlayAnimation"].owners = "true";
        policies.script_ossl_rules["osAvatarStopAnimation"].owners = "true";
        policies.script_ossl_rules["osSetRegionWaterHeight"].owners = "true";
        policies.script_ossl_rules["osSetRegionSunSettings"].owners = "true";
        policies.script_ossl_rules["osReplaceAgentEnvironment"].owners = blocked_visitor.str();
        const auto duplicate_script_asset = std::make_shared<AssetBase>(
            Uuid::random(), "Duplication script", static_cast<std::int8_t>(AssetType::LSLText), "");
        const std::string duplicate_script_source = R"LSL(
            default {
                state_entry() {
                    if (llGetObjectName() == "Script media fixture") {
                        integer setResult = llSetPrimMediaParams(0,
                            [PRIM_MEDIA_CURRENT_URL, "https://script.example/path"]);
                        integer clearResult = llClearLinkMedia(LINK_THIS, 0);
                        llSetText("media status " + (string)setResult + "/" + (string)clearResult, <1,1,1>, 1);
                    } else if (llGetObjectName() == "Parcel music fixture") {
                        llSetParcelMusicURL("https://stream.example/audio");
                        string url = llGetParcelMusicURL();
                        list media = llParcelMediaQuery([PARCEL_MEDIA_COMMAND_URL, PARCEL_MEDIA_COMMAND_TYPE,
                            PARCEL_MEDIA_COMMAND_SIZE, PARCEL_MEDIA_COMMAND_DESC, PARCEL_MEDIA_COMMAND_TEXTURE]);
                        if (url == "" && llGetListLength(media) == 0) {
                            url = "music denied";
                        } else if (llGetListEntryType(media, 2) == TYPE_STRING) {
                            url += "|" + llDumpList2String(media, "|");
                        }
                        llSetText(url, <1,1,1>, 1);
                        if (url != "music denied") {
                            llParcelMediaCommandList([PARCEL_MEDIA_COMMAND_URL, "https://global.example/video"]);
                            llParcelMediaCommandList([PARCEL_MEDIA_COMMAND_AGENT, llGetObjectDesc(),
                                PARCEL_MEDIA_COMMAND_URL, "https://private.example/video",
                                PARCEL_MEDIA_COMMAND_SIZE, 320, 240, PARCEL_MEDIA_COMMAND_TIME, 12.5,
                                PARCEL_MEDIA_COMMAND_LOOP]);
                            llSetText(llList2String(llParcelMediaQuery([PARCEL_MEDIA_COMMAND_URL]), 0), <1,1,1>, 1);
                        }
                    } else {
                        llSetText("fresh", <1,1,1>, 1);
                    }
                }
                on_rez(integer parameter) { llSetText("unexpected on_rez", <1,1,1>, 1); }
            }
        )LSL";
        duplicate_script_asset->data = std::make_shared<std::vector<std::uint8_t>>(
            duplicate_script_source.begin(), duplicate_script_source.end());
        auto empty_legacy_material = std::make_shared<AssetBase>(
            empty_legacy_material_id, "Empty material", static_cast<std::int8_t>(AssetType::OSMaterial), "");
        const std::string empty_material_xml =
            "<llsd><map><key>DiffuseAlphaMode</key><integer>1</integer></map></llsd>";
        empty_legacy_material->data = std::make_shared<std::vector<std::uint8_t>>(
            empty_material_xml.begin(), empty_material_xml.end());
        auto mesh_script = std::make_shared<AssetBase>(
            object_animation_script, "Object animation script", static_cast<std::int8_t>(AssetType::LSLText), "");
        const std::string mesh_script_source = R"LSL(
            default {
                state_entry() {
                    if (llGetObjectName() == "Crossing script") {
                        llSetRegionPos(<250,120,26>);
                        integer outside = llSetRegionPos(<270,120,26>);
                        integer inside = llSetRegionPos(<251,120,26>);
                        if (outside == 0 && inside == 1 && llGetPos() == <251,120,26>) {
                            llSetPos(<270,120,26>);
                        }
                        return;
                    }
                    llStartObjectAnimation("Scripted mesh animation");
                    llSay(0, llList2String(llGetObjectAnimationNames(), 1));
                    llSleep(0.5);
                    llStopObjectAnimation("SCRIPTED MESH ANIMATION");
                }
            }
        )LSL";
        mesh_script->data = std::make_shared<std::vector<std::uint8_t>>(
            mesh_script_source.begin(), mesh_script_source.end());
        const auto attachment_script = std::make_shared<AssetBase>(
            Uuid::random(), "Attachment script", static_cast<std::int8_t>(AssetType::LSLText), "");
        const std::string attachment_script_source = R"LSL(
            integer crossing_mask;
            default {
                state_entry() {
                    if (llGetObjectDesc() == "Attachment checkpoint") {
                        llSetObjectDesc("Attachment restarted");
                        return;
                    }
                    llSetObjectDesc("Attachment checkpoint");
                    llListen(7195, "", llGetOwner(), "Transferred listener");
                    llSleep(0.5);
                    llSetObjectDesc("Attachment resumed");
                }
                listen(integer channel, string name, key sender, string message) {
                    llSetObjectDesc("Attachment heard chat");
                }
                changed(integer mask) {
                    if (llGetObjectDesc() == "Attachment resumed") {
                        crossing_mask = mask;
                        if (llGetPermissions() & PERMISSION_TAKE_CONTROLS)
                            llSetObjectDesc("Attachment changed " + (string)crossing_mask);
                        else if (mask & CHANGED_TELEPORT)
                            llSetObjectDesc("Attachment lost permission");
                        else
                            llRequestPermissions(llGetOwner(), PERMISSION_TAKE_CONTROLS);
                    }
                }
                run_time_permissions(integer granted) {
                    if (granted & PERMISSION_TAKE_CONTROLS) {
                        llTakeControls(CONTROL_BACK, FALSE, FALSE);
                        llTakeControls(CONTROL_FWD, TRUE, FALSE);
                        llSetObjectDesc("Attachment changed " + (string)crossing_mask);
                    }
                }
                control(key avatar, integer held, integer changed) {
                    if (avatar == llGetOwner() && held == CONTROL_FWD && changed == CONTROL_FWD)
                        llSetObjectDesc("Attachment held");
                    if (avatar == llGetOwner() && held == 0 && changed == CONTROL_FWD) {
                        llReleaseControls();
                        llSetObjectDesc("Attachment released " + (string)llGetPermissions());
                    }
                }
            }
        )LSL";
        attachment_script->data = std::make_shared<std::vector<std::uint8_t>>(
            attachment_script_source.begin(), attachment_script_source.end());
        const auto physics_mesh_asset = make_physics_mesh_asset();
        const auto convex_mesh_asset = make_physics_mesh_asset(true);
        policies.load_asset = [covenant, unsit_script, force_sit_script, script_day_cycle, duplicate_script_asset,
                               attachment_script,
                               empty_legacy_material, mesh_script, physics_mesh_asset, convex_mesh_asset,
                               &uploaded_asset](Uuid id)
                -> std::shared_ptr<AssetBase> {
            if (id == physics_mesh_asset_id)
                return physics_mesh_asset;
            if (id == convex_mesh_asset_id)
                return convex_mesh_asset;
            if (id == attachment_script->full_id())
                return attachment_script;
            if (id == empty_legacy_material->full_id())
                return empty_legacy_material;
            if (id == mesh_script->full_id())
                return mesh_script;
            if (id == covenant->full_id())
                return covenant;
            if (id == unsit_script->full_id())
                return unsit_script;
            if (id == script_day_cycle->full_id())
                return script_day_cycle;
            if (id == duplicate_script_asset->full_id())
                return duplicate_script_asset;
            return id == force_sit_script->full_id() ? force_sit_script : uploaded_asset(id);
        };
        policies.library_inventory_item = [library_item, covenant](Uuid id) -> std::optional<InventoryItem> {
            if (id != library_item)
                return {};
            InventoryItem item;
            item.id = id;
            item.asset_id = covenant->full_id();
            item.asset_type = static_cast<int>(AssetType::Notecard);
            return item;
        };
        {
            policies.estate_handshake_only_flags &= ~0x100000u;
            RegionRuntime runtime(region, presence, policies);
            auto stored_prim = SceneObject::primitive(Uuid::random(), {220, 220, 25});
            const auto missing_material = Uuid::random();
            TextureEntry stored_textures(TextureEntry::white_texture);
            stored_textures.default_texture->material_id(empty_legacy_material_id);
            stored_textures.create_face(1)->material_id(missing_material);
            stored_prim.texture_entry = stored_textures.bytes(9);
            if (!runtime.world().add(stored_prim, true, false))
                throw std::runtime_error("Stored legacy material fixture could not be added");
            runtime.set_terrain_limits(-34, 56);
            runtime.set_terrain_handshake(false, 8001.f);
            runtime.load_region_sun_settings(false);
            runtime.set_region_telehub(hub.id, {{0, 0, 32}, {0, 0, 0}, {0, 0, 16}});
            auto bound = runtime.start(listener);
            const auto handshake = runtime.viewers().region_handshake();
            if (std::get<double>(handshake.block("RegionInfo").at("WaterHeight")) != 8000.)
                throw std::runtime_error("Region startup did not clamp the saved water height");
            runtime.poll();
            const auto cleaned_prim = runtime.world().object(stored_prim.id);
            if (!cleaned_prim)
                throw std::runtime_error("Startup lost the stored legacy material fixture");
            TextureEntry cleaned_textures(cleaned_prim->texture_entry);
            if (!cleaned_textures.default_texture->material_id().is_zero() ||
                cleaned_textures.get_face(1)->material_id() != missing_material)
                throw std::runtime_error("Startup did not distinguish empty from missing legacy materials");
            runtime.world().remove(stored_prim.id);
            verify_unattended_object_physics(runtime);
            auto circuit = std::make_shared<AgentCircuitData>();
            circuit->agent_id = region.estate_owner;
            circuit->first_name = "Native";
            circuit->last_name = "Viewer";
            circuit->session_id = Uuid::random();
            circuit->secure_session_id = Uuid("d36e9ccb-4bbb-46c9-8e5e-b8cb159d490d");
            circuit->circuit_code = 42;
            circuit->caps_path = "shutdown";
            circuit->start_position = {126, 64, 25};
            const auto unsit_source = "key avatar = \"" + circuit->agent_id.str() + "\";\n" +
                "key administrator = \"" + blocked_visitor.str() + "\";\n" + R"LSL(
                default {
                    state_entry() {
                        llUnSit(avatar);
                        if (llGetOwner() == administrator) {
                            if (osReplaceAgentEnvironment("invalid", 0, NULL_KEY) != -4
                                || osReplaceAgentEnvironment(avatar, 0, "Missing setting") != -3
                                || osReplaceAgentEnvironment(avatar, 0, "Parcel day cycle") != 1
                                || osReplaceAgentEnvironment(avatar, 0, NULL_KEY) != 1
                                || osReplaceAgentEnvironment(avatar, 1, "Parcel day cycle") != 1) {
                                llSay(0, "Environment fixture failed agent override");
                                return;
                            }
                            integer missing = osReplaceRegionEnvironment(0, "Missing setting", 6, -1, 300, 100, 200);
                            if (missing != -4) {
                                llSay(0, "Environment fixture failed missing asset " + (string)missing);
                                return;
                            }
                            if (osReplaceRegionEnvironment(0, NULL_KEY, 0, 99, 0, 0, 0) != 1)
                                return;
                            integer result = osReplaceRegionEnvironment(1, "9a848f0e-f7df-49c0-9d79-99341ce1d676",
                                    6, -1, 300, 100, 200);
                            if (result != 1 || llGetRegionDayLength() != 21600 || llGetRegionDayOffset() != 82800) {
                                llSay(0, "Environment fixture failed edit " + (string)result);
                                return;
                            }
                            integer parcel_result = osReplaceParcelEnvironment(1, "Parcel day cycle");
                            if (parcel_result != 1 || llGetDayLength() != 21600 || llGetDayOffset() != 82800
                                || osReplaceParcelEnvironment(0, NULL_KEY) != 1) {
                                llSay(0, "Environment fixture failed parcel edit " + (string)parcel_result);
                                return;
                            }
                        }
                        else if (osReplaceRegionEnvironment(0, "", 6, -1, 300, 100, 200) != -3) {
                            llSay(0, "Environment fixture failed permission check");
                            return;
                        }
                        else if (osReplaceAgentEnvironment(avatar, 0, NULL_KEY) != -2) {
                            llSay(0, "Environment fixture failed agent permission check");
                            return;
                        }
                        if (osGetAgentIP("invalid") == "" && osGetAgentIP(NULL_KEY) == "")
                            llSay(0, "External unsit complete " + osGetAgentIP(avatar));
                    }
                }
            )LSL";
            if (const auto compiled = script::compile_lsl(unsit_source); !compiled)
                throw std::runtime_error("External unsit fixture did not compile: " + compiled.diagnostics.front().message);
            unsit_script->data = std::make_shared<std::vector<std::uint8_t>>(unsit_source.begin(), unsit_source.end());
            const std::string_view force_sit_source = R"LSL(
                default {
                    state_entry() {
                        osSetRegionWaterHeight(27.125);
                        osSetRegionWaterHeight(-1);
                        osSetRegionSunSettings(TRUE, TRUE, 11.25);
                        list agents = osGetAgents();
                        if (llWater(ZERO_VECTOR) == 27.125
                            && llGetListLength(agents) == 1
                            && llGetListEntryType(agents, 0) == TYPE_STRING
                            && llList2String(agents, 0) == llKey2Name(llGetOwner())) {
                            osForceOtherSit(llGetOwner());
                            osAvatarPlayAnimation(llGetOwner(), "Custom gesture");
                            key animation = "37aec7d2-5518-4217-9e0c-b6dacb7bc7e6";
                            osAvatarStopAnimation(llGetOwner(), animation);
                        }
                    }
                }
            )LSL";
            force_sit_script->data = std::make_shared<std::vector<std::uint8_t>>(
                force_sit_source.begin(), force_sit_source.end());
            presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id);
            LoginTransferContext context;
            std::string reason;
            std::vector<Uuid> offered_formats;
            if (runtime.query_access(region, restricted_crosser, {}, false, {96, 64, 25}, offered_formats,
                                     context, reason) || reason != "You are banned from the parcel")
                throw std::runtime_error("Crossing access did not enforce the target parcel restriction");
            if (!runtime.query_access(region, restricted_crosser, {}, false, {210, 64, 25}, offered_formats,
                                      context, reason))
                throw std::runtime_error("Crossing access denied an unrestricted parcel: " + reason);
            if (runtime.query_access(region, restricted_crosser, {}, false, {-1, 64, 25}, offered_formats,
                                     context, reason) || reason != "No parcel found")
                throw std::runtime_error("Crossing access accepted an out-of-region position");
            if (!runtime.world().add(hub))
                throw std::runtime_error("Could not place the foreign admission fixture telehub");
            auto foreign = std::make_shared<AgentCircuitData>(*circuit);
            foreign->agent_id = blocked_visitor;
            foreign->session_id = Uuid::random();
            foreign->secure_session_id = Uuid::random();
            foreign->circuit_code = 44;
            foreign->verified_foreign_login = true;
            if (runtime.create_agent(nullptr, region, foreign, 1u << 30, context, reason) ||
                reason != "No suitable landing point found")
                throw std::runtime_error("Foreign visitor bypassed telehub restrictions: " + reason);
            if (!runtime.world().remove(hub.id))
                throw std::runtime_error("Could not remove the foreign admission fixture telehub");
            if (!runtime.create_agent(nullptr, region, circuit, 128, context, reason) ||
                listener->routes.simple_keys().empty() || listener->routes.poll_keys().empty())
                throw std::runtime_error("Region admission did not own live capability routes");
            auto transferred_agent = std::make_shared<AgentData>();
            transferred_agent->agent_id = circuit->agent_id;
            transferred_agent->session_id = circuit->session_id;
            transferred_agent->circuit_code = circuit->circuit_code;
            transferred_agent->position = circuit->start_position;
            transferred_agent->active_group_id = circuit->active_group_id;
            transferred_agent->center = {40, 50, 60};
            transferred_agent->velocity = {1, 2, 3};
            transferred_agent->body_rotation = {0, 0, 1, 0};
            transferred_agent->always_run = true;
            transferred_agent->set_look_at({-1, 0, 0});
            if (!runtime.update_agent(region, transferred_agent, context))
                throw std::runtime_error("Region did not accept the camera transfer fixture");
            auto abandoned = std::make_shared<AgentCircuitData>(*circuit);
            abandoned->agent_id = Uuid::random();
            abandoned->session_id = Uuid::random();
            abandoned->secure_session_id = Uuid::random();
            abandoned->circuit_code = 43;
            abandoned->caps_path = "second-unconnected";
            presence.login(abandoned->agent_id.str(), abandoned->session_id, abandoned->secure_session_id);
            if (!runtime.create_agent(nullptr, region, abandoned, 128, context, reason))
                throw std::runtime_error("Unconnected login reservation blocked subsequent admission");
            verify_transferred_seat(runtime, region, presence, bound, attachment_script->full_id(),
                                    0, "Attachment changed 256");
            constexpr std::uint32_t via_location = 1u << 4;
            verify_transferred_seat(runtime, region, presence, bound, attachment_script->full_id(),
                                    via_location, "Attachment changed 768");
            if (!inventory.create_user_inventory(circuit->agent_id))
                throw std::runtime_error("Could not create upload test inventory");
            auto expired = SceneObject::primitive(circuit->agent_id, {210, 100, 25});
            expired.name = "Expired fixture linkset";
            expired.rezzed_at -= std::chrono::minutes(5);
            const auto root = runtime.world().add(expired);
            if (!root)
                throw std::runtime_error("Could not add parcel return fixture");
            auto child = SceneObject::primitive(circuit->agent_id, {1, 0, 0});
            child.parent_id = root->local_id;
            if (!runtime.world().add(child))
                throw std::runtime_error("Could not add parcel return fixture child");
            auto group_object = SceneObject::primitive(circuit->agent_id, {215, 100, 25});
            group_object.group = return_parcel->group;
            group_object.rezzed_at = expired.rezzed_at;
            if (!runtime.world().add(group_object))
                throw std::runtime_error("Could not add parcel group exemption fixture");
            const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!return_asset_rejected && std::chrono::steady_clock::now() < failure_deadline) {
                runtime.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (!return_asset_rejected || !runtime.world().object(expired.id) ||
                !runtime.world().object(child.id))
                throw std::runtime_error("Parcel auto-return removed a linkset after an asset write failure");
            reject_return_asset = false;
            const auto return_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (runtime.world().object(expired.id) && std::chrono::steady_clock::now() < return_deadline) {
                runtime.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (runtime.world().object(expired.id) || runtime.world().object(child.id) ||
                !runtime.world().object(group_object.id))
                throw std::runtime_error("Parcel auto-return removed the wrong objects");
            const auto lost_and_found = inventory.for_type(circuit->agent_id, 16);
            if (!lost_and_found)
                throw std::runtime_error("Could not find Lost and Found for parcel return fixture");
            const auto returned = inventory.folder_items(circuit->agent_id, lost_and_found->id);
            if (returned.size() != 1 || returned.front().name != expired.name)
                throw std::runtime_error("Parcel return did not deliver the object to Lost and Found");
            const auto asset = uploaded_asset(returned.front().asset_id);
            const auto linkset = asset && asset->data ? read_object_asset_xml(*asset->data) : std::nullopt;
            if (!linkset || linkset->linksets.size() != 1 || linkset->linksets.front().parts.size() != 2)
                throw std::runtime_error("Parcel return did not preserve the entire linkset");
            const auto migrated_part_id = verify_viewer_packets(runtime, *listener, bound, *circuit, *covenant,
                                  library_item, uploaded_asset,
                                  inventory, hub, policies.spawn_point_routing == "sequence" ? 96.f : 112.f,
                                  parcel_refreshes, unsit_script->full_id(), force_sit_script->full_id(), other_parcel->owner,
                                  other_parcel->group, blocked_visitor, dwell_time, hub_parcel->id, dwell_writes,
                                  policies.use_trash_on_delete, grant_viewer_admin, duplicate_script_asset->full_id());
            if (terrain_settings_writes != std::vector<std::array<float, 3>>{{27.125f, 56.f, -34.f}})
                throw std::runtime_error("Script water height must persist once and preserve terrain limits");
            if (!runtime.use_estate_sun())
                throw std::runtime_error("Script did not enable the estate-sun setting");
            if (environment_writes.size() != 2 || environment_writes[0].first ||
                environment_writes[1].first != other_parcel->id)
                throw std::runtime_error("Environment edits should persist once in their region and parcel scopes");
            const auto environment = network::parse_llsd_xml(environment_writes[0].second);
            const auto parcel_environment = network::parse_llsd_xml(environment_writes[1].second);
            if (parcel_environment.get("day_length").integer != 21600 ||
                parcel_environment.get("day_offset").integer != 82800 ||
                parcel_environment.get("day_cycle").get("name").as_string() != "Script fixture")
                throw std::runtime_error("Parcel environment did not inherit the region timing and save the inventory cycle");
            const auto& altitudes = environment.get("track_altitudes").array;
            if (environment.get("day_length").integer != 21600 || environment.get("day_offset").integer != 82800 ||
                environment.get("day_cycle").get("name").as_string() != "Script fixture" ||
                altitudes.size() != 3 || altitudes[0].real != 100 || altitudes[1].real != 200 ||
                altitudes[2].real != 300)
                throw std::runtime_error("Script did not persist the requested timing and sorted altitude settings");
            {
                std::lock_guard lock(home_mutex);
                if (home_writes != std::vector<Vector3>{{32, 64, 60}, {96, 64, 60}})
                    throw std::runtime_error("Set Home persisted a location outside its authorized parcel");
                if (telehub_writes != std::vector<std::pair<Uuid, std::size_t>>{
                        {hub.id, 0}, {hub.id, 1}, {hub.id, 0}, {Uuid{}, 0}})
                    throw std::runtime_error("Telehub management persisted the wrong state");
            }
            std::vector<Uuid> updated_assets;
            for (const auto& [item, level] : upload_events) {
                if (item.name != "Edit")
                    continue;
                const auto asset = uploaded_asset(item.asset_id);
                const auto expected =
                    std::vector<std::uint8_t>{static_cast<std::uint8_t>(3 + updated_assets.size()), 7, 0, 8};
                if (level != 0 || item.owner != circuit->agent_id || !asset || !asset->data ||
                    *asset->data != expected)
                    throw std::runtime_error(
                        "UDP inventory upload completion reported the wrong item or asset");
                updated_assets.push_back(item.asset_id);
            }
            if (updated_assets.size() != 2 || updated_assets.front() == updated_assets.back())
                throw std::runtime_error("Each successful UDP inventory asset update must publish one event");
            if (policies.spawn_point_routing == "sequence") {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
                while (!unused_material_expired && std::chrono::steady_clock::now() < deadline) {
                    runtime.poll();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (!material_stored_in_background)
                    throw std::runtime_error("Periodic material backup did not store the asset off the region thread");
                if (!unused_material_expired || !uploaded_asset(generated_material) || !embedded_store_rejected ||
                    !uploaded_asset(legacy_material_id))
                    throw std::runtime_error("Eviction did not expire the unused material after storing its asset");
            }
            bool failed = false;
            bool material_saved_before_scene = false;
            try {
                runtime.stop([&] {
                    const auto part = runtime.world().object(migrated_part_id);
                    if (!part)
                        return;
                    const auto stores = network::parse_llsd_xml(part->dynamic_attributes_xml).get("OpenSim");
                    material_saved_before_scene = uploaded_asset(generated_material) && uploaded_asset(legacy_material_id) &&
                        !stores.contains("Materials") && stores.get("OtherStore").get("note").text == "keep";
                });
            } catch (const std::runtime_error& e) {
                failed = std::string(e.what()) == "controlled presence cleanup failure";
            }
            if (!failed || runtime.capabilities() || !listener->routes.simple_keys().empty() ||
                !listener->routes.poll_keys().empty())
                throw std::runtime_error("Cleanup failure skipped capability disposal");
            if (!material_saved_before_scene)
                throw std::runtime_error("Forced region backup did not store the pending material before the scene");
            network::UdpSocket replacement({bound.address, bound.port}, [](const auto&, auto) {});
            replacement.start();
            replacement.stop();
            if (runtime.create_agent(nullptr, region, circuit, 128, context, reason))
                throw std::runtime_error("Stopped region accepted new login");
        } // destructor repeats throwing provider cleanup without terminating the process.
        if (presence.region_logouts != 2)
            throw std::runtime_error("Destructor cleanup was not exercised");
        std::cout << "PASS: region cleanup continues after provider failure; destructor does not terminate\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
