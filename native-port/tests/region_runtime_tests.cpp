#include "opensim/framework/atomic_shared_ptr.hpp"
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/remote_data_channels.hpp"
#include "opensim/scene/terrain_layer.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/scene/object_update.hpp"
#include "opensim/framework/agent_data.hpp"
#include "opensim/framework/permissions.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/server/object_handler.hpp"
#include "opensim/network/sdk_llsd_binary.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/http_server.hpp"
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

void verify_unattended_object_physics(
    RegionRuntime& runtime, const std::function<void(std::shared_ptr<AssetBase>)>& store_asset) {
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

    const float hover_ground = runtime.world().terrain().height_at(220, 215);
    auto hover_prim = SceneObject::primitive(prim.owner, {220, 215, hover_ground + 5.f});
    hover_prim.flags = 1u;
    if (!runtime.world().add(hover_prim))
        throw std::runtime_error("Could not rez the hovering prim");
    runtime.world().set_prim_hover(hover_prim.id, {3.f, .2f, false});
    poll_until([&] {
        const auto object = runtime.world().object(hover_prim.id);
        return object && std::abs(object->position.z - (hover_ground + 3.f)) < .03f &&
               std::abs(object->velocity.z) < .12f;
    }, "Hovering prim did not approach the target height above terrain");
    const Quaternion desired_rotation{0, 0, .70710678f, .70710678f};
    runtime.world().set_prim_look_at(hover_prim.id, {desired_rotation, 1.f});
    poll_until([&] {
        const auto object = runtime.world().object(hover_prim.id);
        return object && std::abs(object->rotation.z - desired_rotation.z) < .02f &&
               std::abs(object->rotation.w - desired_rotation.w) < .02f;
    }, "Look-at did not rotate the physical prim");
    runtime.world().set_prim_look_at(hover_prim.id, {});
    if (runtime.world().object(hover_prim.id)->angular_velocity != Vector3{})
        throw std::runtime_error("Stopping look-at did not clear angular velocity");
    const Vector3 move_target{221, 215, hover_ground + 4.f};
    runtime.world().set_prim_move_target(hover_prim.id, {move_target, .2f});
    poll_until([&] {
        const auto object = runtime.world().object(hover_prim.id);
        return object && std::abs(object->position.x - move_target.x) < .03f &&
               std::abs(object->position.z - move_target.z) < .03f &&
               std::abs(object->velocity.x) < .12f;
    }, "Move-to-target did not take precedence over hover");
    runtime.world().set_prim_move_target(hover_prim.id, {});
    poll_until([&] {
        const auto object = runtime.world().object(hover_prim.id);
        return object && std::abs(object->position.z - (hover_ground + 3.f)) < .03f;
    }, "Stopping move-to-target did not resume hover");
    runtime.world().set_prim_hover(hover_prim.id, {});
    poll_until([&] {
        const auto object = runtime.world().object(hover_prim.id);
        return object && object->position.z < hover_ground + 2.8f && object->velocity.z < 0.f;
    }, "Stopping hover did not restore prim gravity");
    runtime.world().remove(hover_prim.id);

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

    auto velocity_root = SceneObject::primitive(prim.owner, {220, 215, 28});
    velocity_root.physics.gravity = 0.f;
    const auto velocity_linkset = runtime.world().add(velocity_root);
    if (!velocity_linkset)
        throw std::runtime_error("Could not rez the scripted velocity root");
    auto velocity_child = SceneObject::primitive(prim.owner, {1, 0, 0});
    velocity_child.parent_id = velocity_linkset->local_id;
    velocity_child.rotation = {0, 0, 1, 0};
    if (!runtime.world().add(velocity_child))
        throw std::runtime_error("Could not rez the scripted velocity child");
    runtime.world().set_prim_velocity(velocity_child.id, {3, 0, 0}, true, false);
    if (runtime.world().object(velocity_root.id)->velocity != Vector3{})
        throw std::runtime_error("Scripted velocity changed a static prim");
    runtime.world().set_group_flags(velocity_root.id, true, false, false);
    runtime.world().set_prim_velocity(velocity_child.id, {3, 0, 0}, true, false);
    runtime.world().set_prim_velocity(velocity_child.id, {0, 0, 2}, false, true);
    poll_until([&] {
        const auto root = runtime.world().object(velocity_root.id);
        return root && root->position.x < velocity_root.position.x - .2f &&
               root->velocity.x < -2.f && root->angular_velocity.z > 1.f;
    }, "Scripted linear and angular velocity did not reach the root body");
    runtime.world().remove(velocity_root.id);

    auto push_root = SceneObject::primitive(prim.owner, {220, 215, ground + 5.f});
    push_root.flags = 1u;
    push_root.physics.gravity = 0.f;
    const auto push_linkset = runtime.world().add(push_root);
    if (!push_linkset)
        throw std::runtime_error("Could not rez the llPushObject target root");
    auto push_child = SceneObject::primitive(prim.owner, {1, 0, 0});
    push_child.parent_id = push_linkset->local_id;
    push_child.rotation = {0, 0, 1, 0};
    const auto pusher = SceneObject::primitive(prim.owner, {226, 215, ground + 5.f});
    if (!runtime.world().add(push_child) || !runtime.world().add(pusher))
        throw std::runtime_error("Could not rez the llPushObject target child and pusher");

    auto push_asset = std::make_shared<AssetBase>(
        Uuid::random(), "Push linked prim", static_cast<std::int8_t>(AssetType::LSLText), "");
    const auto push_source = "default { state_entry() { llPushObject(\"" + push_child.id.str() +
                             "\", <1200, 0, 0>, ZERO_VECTOR, TRUE); } }";
    push_asset->data = std::make_shared<std::vector<std::uint8_t>>(push_source.begin(), push_source.end());
    const auto push_asset_id = push_asset->full_id();
    store_asset(std::move(push_asset));
    auto push_item = std::make_shared<TaskInventoryItem>();
    push_item->item_id = Uuid::random();
    push_item->parent_id = push_item->parent_part_id = pusher.id;
    push_item->asset_id = push_asset_id;
    push_item->owner_id = push_item->creator_id = pusher.owner;
    push_item->type = push_item->inventory_type = 10;
    push_item->script_running = true;
    push_item->name = "Push linked prim";
    runtime.restore_task_inventory(pusher.id, {push_item});
    poll_until([&] {
        const auto target = runtime.world().object(push_root.id);
        return target && target->position.x < push_root.position.x - .1f && target->velocity.x < -.1f;
    }, "llPushObject did not move the root in the target child's local direction");
    runtime.world().remove(pusher.id);
    runtime.world().remove(push_root.id);

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

    auto locked = SceneObject::primitive(prim.owner, {218, 216, 34}, {1, 1, 1});
    locked.flags = 1u;
    locked.buoyancy = 1.f;
    locked.inertia_override = framework::PrimInertiaOverride{10.f, {.5f, 0, 0}, {1, 1, 1}, {0, 0, 0, 1}};
    if (!runtime.world().add(locked))
        throw std::runtime_error("Could not rez the rotation motor fixture");
    runtime.world().set_rotation_axes(locked.id, 2, false);
    runtime.world().set_prim_velocity(locked.id, {0, 0, 2}, false, true);
    poll_until([&] {
        const auto object = runtime.world().object(locked.id);
        return object && object->rotation.z > .2f && object->angular_velocity.z > 1.f;
    }, "The rotation motor fixture did not turn about the unlocked Z axis");

    // The local X axis has rotated away from world X, where the motor is anchored.
    runtime.world().set_prim_velocity(locked.id, {2, 0, 0}, false, false);
    runtime.world().set_prim_velocity(locked.id, {3, 0, 0}, false, true);
    poll_until([&] {
        const auto object = runtime.world().object(locked.id);
        return object && object->velocity.x > 1.f && std::abs(object->angular_velocity.x) < .05f &&
               std::abs(object->angular_velocity.y) < .05f && std::abs(object->angular_velocity.z) < .05f;
    }, "A velocity update moved the existing rotation motor away from its original axis");

    const Vector3 destination{218, 220, 35};
    if (!runtime.world().patch_transform(locked.id, destination, {}, {}))
        throw std::runtime_error("Could not translate the rotation motor fixture");
    runtime.world().set_prim_velocity(locked.id, {3, 0, 0}, false, true);
    poll_until([&] {
        const auto object = runtime.world().object(locked.id);
        return object && object->velocity.x > 1.f && std::abs(object->position.y - destination.y) < .02f &&
               std::abs(object->position.z - destination.z) < .02f &&
               std::abs(object->angular_velocity.x) < .05f && std::abs(object->angular_velocity.y) < .05f;
    }, "Translation did not preserve the root pose, linear motion, and existing rotation motor");

    const auto translated = runtime.world().object(locked.id);
    runtime.world().set_prim_force(locked.id, {10, 0, 0});
    runtime.world().set_prim_torque(locked.id, {10, 0, 0}, false);
    poll_until([&] {
        const auto object = runtime.world().object(locked.id);
        return object && object->velocity.x > translated->velocity.x + .3f &&
               std::abs(object->angular_velocity.x) < .05f && std::abs(object->angular_velocity.y) < .05f;
    }, "Runtime force and torque did not preserve the rotation motor");

    runtime.world().set_prim_force(locked.id, {});
    runtime.world().set_prim_torque(locked.id, {}, false);
    runtime.world().set_prim_buoyancy(locked.id, 0.f);
    poll_until([&] {
        const auto object = runtime.world().object(locked.id);
        return object && object->velocity.z < -.5f && object->position.z < destination.z;
    }, "Clearing neutral buoyancy did not restore world gravity");
    runtime.world().remove(locked.id);

    const Vector3 drop_position{218, 218, 30};
    auto phantom_platform = SceneObject::primitive(prim.owner, {218, 218, 27}, {4, 4, 1});
    auto falling = SceneObject::primitive(prim.owner, drop_position, {1, 1, 1});
    falling.flags = 1u;
    if (!runtime.world().add(phantom_platform) || !runtime.world().add(falling))
        throw std::runtime_error("Could not rez the physical phantom fixture");
    const auto resting_on_platform = [&] {
        const auto object = runtime.world().object(falling.id);
        return object && std::abs(object->position.z - 28.f) < .15f && std::abs(object->velocity.z) < .15f;
    };
    poll_until(resting_on_platform, "The solid prim did not land on the platform");

    runtime.world().set_group_flags(falling.id, true, false, true);
    poll_until([&] {
        const auto object = runtime.world().object(falling.id);
        return object && object->position.z < 26.f && object->velocity.z < -.5f;
    }, "The physical phantom did not fall through the platform");
    poll_until([&] {
        const auto object = runtime.world().object(falling.id);
        return object && object->position.z > 0.f && object->position.z < 26.f &&
               std::abs(object->velocity.z) < .15f;
    }, "The physical phantom did not land on terrain");

    runtime.world().set_group_flags(falling.id, true, false, false);
    if (!runtime.world().patch_transform(falling.id, drop_position, {}, {}) ||
        !runtime.world().update_motion(falling.id, {}, {}, {}))
        throw std::runtime_error("Could not reset the physical phantom fixture");
    poll_until(resting_on_platform, "Disabling phantom did not restore prim contact");
    runtime.world().remove(falling.id);
    runtime.world().remove(phantom_platform.id);
}

void verify_prim_mass_queries(RegionRuntime& runtime, Uuid script_asset, Uuid admin) {
    const framework::PrimInertiaOverride stored{12.f, {.5f, -.25f, 0.f}, {3.f, 4.f, 5.f}, {0, 0, .6f, .8f}};
    if (read_prim_inertia_xml(write_prim_inertia_xml(stored)) != stored || read_prim_inertia_xml(""))
        throw std::runtime_error("The legacy PhysInertia column did not round-trip");
    const auto partial = read_prim_inertia_xml("<PhysicsInertia><MASS> 12 </MASS></PhysicsInertia>");
    if (!partial || partial->mass != 12.f || partial->rotation.w != 0.f ||
        read_prim_inertia_xml("<PhysicsInertia><MASS>bad</MASS></PhysicsInertia>") ||
        read_prim_inertia_xml("<PhysicsInertia><CM><X>1</X><Y>2</Y></CM></PhysicsInertia>"))
        throw std::runtime_error("The legacy PhysInertia parser mishandled missing or malformed fields");
    for (const bool physical : {false, true}) {
        const auto owner = physical ? admin : Uuid::random();
        auto root = SceneObject::primitive(owner, {216, 218, 32}, {1, 1, 1});
        if (physical) {
            root.flags = 1u;
            root.buoyancy = 1.f;
        }
        const auto placed = runtime.world().add(root);
        if (!placed)
            throw std::runtime_error("Could not rez the mass query root");

        auto child = SceneObject::primitive(owner, {2, 1, 0}, {1, 1, 1});
        child.parent_id = placed->local_id;
        child.physics.density = 2000.f;
        if (!runtime.world().add(child))
            throw std::runtime_error("Could not rez the mass query child");

        for (const auto& part : {root.id, child.id}) {
            auto item = std::make_shared<TaskInventoryItem>();
            item->item_id = Uuid::random();
            item->parent_id = part;
            item->parent_part_id = part;
            item->asset_id = script_asset;
            item->owner_id = owner;
            item->creator_id = owner;
            item->type = 10;
            item->inventory_type = 10;
            item->script_running = true;
            item->name = "Prim mass fixture";
            item->base_permissions = item->current_permissions = 0x8e000u;
            std::vector<std::shared_ptr<TaskInventoryItem>> contents{item};
            if (physical && part == child.id) {
                auto note = std::make_shared<TaskInventoryItem>(*item);
                note->item_id = Uuid::random();
                note->asset_id = Uuid::random();
                note->name = "Fixture notecard";
                note->type = 7;
                note->inventory_type = 7;
                note->script_running = false;
                contents.push_back(std::move(note));
            }
            runtime.restore_task_inventory(part, contents);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool verified = false;
        do {
            runtime.poll();
            const auto current_root = runtime.world().object(root.id);
            const auto current_child = runtime.world().object(child.id);
            verified = current_root && current_child && current_root->description == "Mass and inertia verified" &&
                       current_child->description == "Mass and inertia verified" &&
                       (!physical || (std::abs(current_root->rotation.x) < .001f &&
                                      std::abs(current_child->rotation.y - .707107f) < .001f));
            if (verified && physical) {
                const auto contents = runtime.snapshot_task_inventory(child.id);
                verified = contents && contents->empty();
            }
            if (verified)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        if (!verified) {
            const auto current_root = runtime.world().object(root.id);
            const auto current_child = runtime.world().object(child.id);
            throw std::runtime_error(std::string(physical ? "Physical" : "Static") +
                " linkset mass and rotation checks failed. Root: " +
                (current_root ? current_root->description : "missing") + ", child: " +
                (current_child ? current_child->description : "missing"));
        }
        runtime.world().remove(root.id);
    }
}

void verify_script_ground_repel(RegionRuntime& runtime, Uuid script_asset) {
    const auto owner = Uuid::random();
    const float ground = runtime.world().terrain().height_at(210, 220);
    const auto handshake = runtime.viewers().region_handshake();
    const float water = static_cast<float>(std::get<double>(handshake.block("RegionInfo").at("WaterHeight")));
    if (water <= ground + 5.f)
        throw std::runtime_error("Ground repel fixture requires water above the terrain");

    for (const bool use_water : {false, true}) {
        auto prim = SceneObject::primitive(owner, {210, 220, ground + 5.f});
        prim.flags = 1u;
        prim.name = use_water ? "Water repel fixture" : "Ground repel fixture";
        if (!runtime.world().add(prim))
            throw std::runtime_error("Could not rez the ground repel fixture");
        auto item = std::make_shared<TaskInventoryItem>();
        item->item_id = Uuid::random();
        item->parent_id = item->parent_part_id = prim.id;
        item->asset_id = script_asset;
        item->owner_id = item->creator_id = owner;
        item->type = item->inventory_type = 10;
        item->script_running = true;
        item->name = "Ground repel";
        runtime.restore_task_inventory(prim.id, {item});

        // Keep the water case within the playable height range despite the high fixture water level.
        const float target = use_water ? 30.f : ground * 2.f + 3.f;
        const float offset = target - (use_water ? water : ground);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool settled = false;
        do {
            runtime.poll();
            const auto current = runtime.world().object(prim.id);
            settled = current && current->hover.above_water == use_water &&
                std::abs(current->hover.height - offset) < .001f &&
                std::abs(current->position.z - target) < .05f && std::abs(current->velocity.z) < .15f;
            if (settled)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        runtime.world().remove(prim.id);
        if (!settled)
            throw std::runtime_error(use_water ? "llGroundRepel did not use the water surface"
                                               : "llGroundRepel did not use the terrain surface");
    }
}

void verify_script_deletes_linkset(RegionRuntime& runtime, Uuid script_asset) {
    const auto owner = Uuid::random();
    const auto root = runtime.world().add(SceneObject::primitive(owner, {216, 218, 32}));
    if (!root)
        throw std::runtime_error("Could not rez the llDie fixture root");
    auto child = SceneObject::primitive(owner, {2, 0, 0});
    child.parent_id = root->local_id;
    child.name = "llDie fixture";
    if (!runtime.world().add(child))
        throw std::runtime_error("Could not rez the llDie fixture child");

    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = child.id;
    item->parent_part_id = child.id;
    item->asset_id = script_asset;
    item->owner_id = owner;
    item->creator_id = owner;
    item->type = item->inventory_type = 10;
    item->script_running = true;
    item->name = "Linkset deletion fixture";
    runtime.restore_task_inventory(child.id, {item});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        runtime.poll();
        if (!runtime.world().object(root->id) && !runtime.world().object(child.id))
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("llDie from a linked child did not remove its whole linkset");
}

void verify_land_collision_scripts(RegionRuntime& runtime, Uuid script_asset) {
    const auto owner = Uuid::random();
    auto root = SceneObject::primitive(owner, {212, 213, 26});
    root.flags = 1u;
    const auto root_part = runtime.world().add(root);
    if (!root_part)
        throw std::runtime_error("Could not rez the land collision linkset");
    auto child = SceneObject::primitive(owner, {0, 0, -1});
    child.parent_id = root_part->local_id;
    if (!runtime.world().add(child))
        throw std::runtime_error("Could not rez the land collision child");
    for (const auto part : {root.id, child.id}) {
        auto item = std::make_shared<TaskInventoryItem>();
        item->item_id = Uuid::random();
        item->parent_id = item->parent_part_id = part;
        item->asset_id = script_asset;
        item->owner_id = item->creator_id = owner;
        item->type = item->inventory_type = 10;
        item->script_running = true;
        item->name = "Land collision fixture";
        runtime.restore_task_inventory(part, {item});
    }
    const auto wait_description = [&](std::string_view expected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            runtime.poll();
            const auto current_root = runtime.world().object(root.id);
            const auto current_child = runtime.world().object(child.id);
            if (current_root && current_child && current_root->description == expected &&
                current_child->description == expected)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Root and child did not receive land collision phase " + std::string(expected));
    };
    wait_description("Land contact active");
    runtime.world().set_prim_buoyancy(root.id, 1.f);
    if (!runtime.world().patch_transform(root.id, Vector3{212, 213, 26}, {}, {}) ||
        !runtime.world().update_motion(root.id, {}, {}, {}))
        throw std::runtime_error("Could not lift the land collision fixture");
    wait_description("Land contact ended");
    runtime.world().remove(root.id);
}

void verify_object_collision_scripts(RegionRuntime& runtime, Uuid script_asset) {
    const auto owner = Uuid::random();
    auto platform = SceneObject::primitive(owner, {212, 216, 25});
    platform.name = "Collision platform";
    platform.scale = {4, 4, 1};
    if (!runtime.world().add(platform))
        throw std::runtime_error("Could not rez the collision platform");

    const Vector3 start_position{212, 216, 28};
    auto falling = SceneObject::primitive(owner, start_position);
    falling.flags = 1u;
    if (!runtime.world().add(falling))
        throw std::runtime_error("Could not rez the falling collision prim");

    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = falling.id;
    item->parent_part_id = falling.id;
    item->asset_id = script_asset;
    item->owner_id = owner;
    item->creator_id = owner;
    item->type = 10;
    item->inventory_type = 10;
    item->script_running = true;
    item->name = "Object collision fixture";
    runtime.restore_task_inventory(falling.id, {item});

    auto platform_item = std::make_shared<TaskInventoryItem>(*item);
    platform_item->item_id = Uuid::random();
    platform_item->parent_id = platform.id;
    platform_item->parent_part_id = platform.id;
    runtime.restore_task_inventory(platform.id, {platform_item});

    const auto wait_description = [&](std::string_view expected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            runtime.poll();
            const auto current = runtime.world().object(falling.id);
            if (current && current->description == expected)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Falling prim did not receive object collision phase " + std::string(expected));
    };

    wait_description("Object contact active");
    runtime.world().set_prim_buoyancy(falling.id, 1.f);
    if (!runtime.world().patch_transform(falling.id, start_position, {}, {}) ||
        !runtime.world().update_motion(falling.id, {}, {}, {}))
        throw std::runtime_error("Could not lift the object collision fixture");
    wait_description("Object contact ended");
    const auto stationary = runtime.world().object(platform.id);
    if (!stationary || stationary->description != "Platform filter installed")
        throw std::runtime_error("The platform's deny-all collision filter leaked an event");
    runtime.world().remove(falling.id);
    runtime.world().remove(platform.id);
}

void verify_volume_detect(RegionRuntime& runtime, Uuid script_asset) {
    const auto owner = Uuid::random();
    auto sensor = SceneObject::primitive(owner, {216, 216, 28});
    sensor.scale = {4, 4, 1};
    const auto root = runtime.world().add(sensor);
    if (!root)
        throw std::runtime_error("Could not rez the volume detector");

    auto child = SceneObject::primitive(owner, {5, 0, 0});
    child.parent_id = root->local_id;
    if (!runtime.world().add(child))
        throw std::runtime_error("Could not rez the volume detector child");

    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = sensor.id;
    item->parent_part_id = sensor.id;
    item->asset_id = script_asset;
    item->owner_id = owner;
    item->creator_id = owner;
    item->type = 10;
    item->inventory_type = 10;
    item->script_running = true;
    item->name = "Volume detector fixture";
    runtime.restore_task_inventory(sensor.id, {item});

    const auto poll_until = [&](auto ready, const char* failure) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            runtime.poll();
            if (ready())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error(failure);
    };

    constexpr auto detector_flags = prim_flag_volume_detect | 0x400u;
    const auto both_parts_have_flags = [&](std::uint32_t expected) {
        const auto current_root = runtime.world().object(sensor.id);
        const auto current_child = runtime.world().object(child.id);
        return current_root && current_child && (current_root->flags & detector_flags) == expected &&
               (current_child->flags & detector_flags) == expected;
    };
    poll_until([&] { return both_parts_have_flags(detector_flags); },
               "llVolumeDetect did not enable the detector and phantom flags across the linkset");

    const Vector3 drop_position{216, 216, 31};
    auto falling = SceneObject::primitive(owner, drop_position);
    falling.name = "Falling through";
    falling.flags = 1u;
    if (!runtime.world().add(falling))
        throw std::runtime_error("Could not rez the volume detection projectile");

    poll_until([&] {
        const auto current = runtime.world().object(sensor.id);
        const auto projectile = runtime.world().object(falling.id);
        return current && projectile && current->description == "Volume contact ended" &&
               projectile->position.z < 27.f && both_parts_have_flags(0);
    }, "Volume detector did not deliver start and end while allowing passage, then clear its flags");

    if (!runtime.world().patch_transform(falling.id, drop_position, {}, {}) ||
        !runtime.world().update_motion(falling.id, {}, {}, {}))
        throw std::runtime_error("Could not reset the volume detection projectile");
    poll_until([&] {
        const auto current = runtime.world().object(falling.id);
        return current && current->position.z > 28.6f && current->position.z < 29.f &&
               std::abs(current->velocity.z) < .15f;
    }, "The prim did not become solid after llVolumeDetect(0)");

    auto child_script = std::make_shared<TaskInventoryItem>(*item);
    child_script->item_id = Uuid::random();
    child_script->parent_id = child.id;
    child_script->parent_part_id = child.id;
    runtime.restore_task_inventory(child.id, {child_script});
    poll_until([&] {
        const auto current_root = runtime.world().object(sensor.id);
        const auto current_child = runtime.world().object(child.id);
        return current_root && current_child && !(current_root->flags & detector_flags) &&
               (current_child->flags & detector_flags) == detector_flags;
    }, "osVolumeDetect on a child did not leave the root solid");

    runtime.world().remove(falling.id);
    runtime.world().remove(sensor.id);
}

void verify_attachment_physics(RegionRuntime& runtime, GridRegion& region, IPresenceService& presence,
                               const network::UdpEndpoint& endpoint, Uuid script_asset, Uuid collision_asset, Uuid sensor_asset,
                               const std::optional<RegionInstantMessage>& forwarded_script_message,
                               const std::optional<RegionInstantMessage>& forwarded_inventory_offer,
                               InventoryService& inventory, const SceneObject& telehub,
                               std::atomic<bool>& restrict_script_teleport, std::uint64_t remote_handle,
                               const std::atomic<unsigned>& remote_cancellations, Uuid landmark_asset) {
    namespace udp = network::lludp;
    auto circuit = std::make_shared<AgentCircuitData>();
    circuit->agent_id = Uuid("c37a78be-6c52-4f63-a84c-f4bbd8567b05");
    circuit->session_id = Uuid::random();
    circuit->secure_session_id = Uuid::random();
    circuit->circuit_code = 46;
    circuit->caps_path = "attachment-physics";
    circuit->first_name = "Collision";
    circuit->last_name = "Walker";
    circuit->start_position = {128, 128, 22.5f};
    circuit->appearance = std::make_shared<AvatarAppearance>();
    presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id);

    LoginTransferContext context;
    std::string reason;
    if (!runtime.create_agent(nullptr, region, circuit, 128, context, reason))
        throw std::runtime_error("Could not reserve the attachment physics circuit: " + reason);
    std::mutex sound_mutex;
    std::vector<udp::Packet> collision_sounds;
    std::vector<udp::Packet> map_requests;
    std::vector<udp::Packet> url_requests;
    std::vector<udp::Packet> dialogs;
    std::vector<udp::Packet> object_messages;
    std::vector<udp::Packet> asset_transfers;
    std::vector<udp::Packet> bulk_inventory_updates;
    std::optional<std::vector<std::uint8_t>> received_wind;
    std::vector<double> health_updates;
    bool received_death_alert = false;
    bool received_eject_alert = false;
    bool received_teleport_proximity_alert = false;
    bool received_detach_event = false;
    std::optional<Uuid> permission_question;
    std::optional<std::vector<double>> default_teleport;
    std::vector<double> teleport_look;
    std::uint64_t teleport_flags = 0;
    std::optional<std::uint64_t> remote_teleport_finish;
    std::atomic<std::uint32_t> ack_sequence = 0;
    network::UdpSocket viewer({"127.0.0.1", 0}, [&](const auto& from, auto bytes) {
        auto packet = udp::decode_datagram(bytes);
        if (packet.reliable) {
            auto ack = udp::make_packet("PacketAck");
            ack.sequence = ++ack_sequence;
            auto row = udp::make_block(ack.spec->blocks.front());
            row["ID"] = std::uint64_t(packet.sequence);
            ack.blocks["Packets"].push_back(std::move(row));
            viewer.send_to(from, udp::encode_datagram(ack));
        }
        if (packet.spec->name == "TeleportLocal") {
            std::lock_guard lock(sound_mutex);
            const auto& info = packet.block("Info");
            default_teleport = std::get<std::vector<double>>(info.at("Position"));
            teleport_look = std::get<std::vector<double>>(info.at("LookAt"));
            teleport_flags = std::get<std::uint64_t>(info.at("TeleportFlags"));
        } else if (packet.spec->name == "TeleportFinish") {
            const auto& info = packet.block("Info");
            if (std::get<Uuid>(info.at("AgentID")) == circuit->agent_id &&
                (std::get<std::uint64_t>(info.at("TeleportFlags")) == 16 ||
                 std::get<std::uint64_t>(info.at("TeleportFlags")) == 10)) {
                std::lock_guard lock(sound_mutex);
                remote_teleport_finish = std::get<std::uint64_t>(info.at("RegionHandle"));
            }
        } else if (packet.spec->name == "SoundTrigger") {
            std::lock_guard lock(sound_mutex);
            collision_sounds.push_back(std::move(packet));
        } else if (packet.spec->name == "ScriptTeleportRequest") {
            std::lock_guard lock(sound_mutex);
            map_requests.push_back(std::move(packet));
        } else if (packet.spec->name == "LoadURL") {
            std::lock_guard lock(sound_mutex);
            url_requests.push_back(std::move(packet));
        } else if (packet.spec->name == "ScriptDialog") {
            std::lock_guard lock(sound_mutex);
            dialogs.push_back(std::move(packet));
        } else if (packet.spec->name == "ImprovedInstantMessage") {
            std::lock_guard lock(sound_mutex);
            object_messages.push_back(std::move(packet));
        } else if (packet.spec->name == "TransferPacket") {
            std::lock_guard lock(sound_mutex);
            asset_transfers.push_back(std::move(packet));
        } else if (packet.spec->name == "BulkUpdateInventory") {
            std::lock_guard lock(sound_mutex);
            bulk_inventory_updates.push_back(std::move(packet));
        } else if (packet.spec->name == "LayerData" &&
                   std::get<std::uint64_t>(packet.block("LayerID").at("Type")) == 55) {
            std::lock_guard lock(sound_mutex);
            received_wind = std::get<std::vector<std::uint8_t>>(packet.block("LayerData").at("Data"));
        } else if (packet.spec->name == "ChatFromSimulator") {
            const auto& chat = packet.block("ChatData");
            const auto message = framework::sdk_bytes_to_string(
                std::get<std::vector<std::uint8_t>>(chat.at("Message")));
            std::lock_guard lock(sound_mutex);
            received_detach_event |= message == u"Scripted attachment detached";
        } else if (packet.spec->name == "ScriptQuestion") {
            const auto& data = packet.block("Data");
            std::lock_guard lock(sound_mutex);
            const auto questions = std::get<std::int64_t>(data.at("Questions"));
            if (questions == 0x20 || questions == 0x80 || questions == 0x1000)
                permission_question = std::get<Uuid>(data.at("ItemID"));
        } else if (packet.spec->name == "HealthMessage") {
            std::lock_guard lock(sound_mutex);
            health_updates.push_back(std::get<double>(packet.block("HealthData").at("Health")));
        } else if (packet.spec->name == "AgentAlertMessage") {
            const auto& alert = packet.block("AlertData");
            const auto message = framework::sdk_bytes_to_string(
                std::get<std::vector<std::uint8_t>>(alert.at("Message")));
            std::lock_guard lock(sound_mutex);
            received_death_alert |= std::get<bool>(alert.at("Modal")) && message == u"You died!";
        } else if (packet.spec->name == "AlertMessage") {
            const auto& alert = packet.block("AlertData");
            const auto message = framework::sdk_bytes_to_string(
                std::get<std::vector<std::uint8_t>>(alert.at("Message")));
            std::lock_guard lock(sound_mutex);
            received_eject_alert |= message == u"You have been ejected from this land";
            received_teleport_proximity_alert |= message == u"Can't teleport closer to destination";
        }
    });
    viewer.start();
    auto connect = udp::make_packet("UseCircuitCode");
    connect.sequence = 1;
    auto& credentials = connect.block("CircuitCode");
    credentials["Code"] = std::uint64_t{circuit->circuit_code};
    credentials["ID"] = circuit->agent_id;
    credentials["SessionID"] = circuit->session_id;
    if (!viewer.send_to(endpoint, udp::encode_datagram(connect)))
        throw std::runtime_error("Could not connect the attachment physics viewer");

    const auto poll_until = [&](auto ready, const char* failure) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            runtime.poll();
            if (ready())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error(failure);
    };
    poll_until([&] { return runtime.viewers().agent(circuit->agent_id).has_value(); },
               "Attachment physics viewer did not connect");
    auto movement = udp::make_packet("CompleteAgentMovement");
    movement.sequence = 2;
    auto& identity = movement.block("AgentData");
    identity["AgentID"] = circuit->agent_id;
    identity["SessionID"] = circuit->session_id;
    identity["CircuitCode"] = std::uint64_t{circuit->circuit_code};
    if (!viewer.send_to(endpoint, udp::encode_datagram(movement)))
        throw std::runtime_error("Could not enter the region for attachment physics");

    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        return avatar && avatar->root && avatar->position.x == 150 && avatar->position.y == 150;
    }, "CompleteAgentMovement did not use the configured default landing point");
    const auto check_local_teleport = [&](std::uint32_t sequence, std::vector<double> requested,
                                          const std::vector<double>& expected, float look_y) {
        auto request = udp::make_packet("TeleportLocationRequest");
        request.sequence = sequence;
        request.block("AgentData")["AgentID"] = circuit->agent_id;
        request.block("AgentData")["SessionID"] = circuit->session_id;
        auto& destination = request.block("Info");
        destination["RegionHandle"] = runtime.descriptor().handle();
        destination["Position"] = std::move(requested);
        destination["LookAt"] = std::vector<double>{look_y == 0 ? 1.0 : 0.0, look_y, 5};
        if (!viewer.send_to(endpoint, udp::encode_datagram(request)))
            throw std::runtime_error("Could not request the local landing point");
        poll_until([&] {
            std::lock_guard lock(sound_mutex);
            return default_teleport == expected;
        }, "Local teleport did not report the expected landing point");
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        const float expected_z = look_y * std::sqrt(0.5f);
        const float expected_w = look_y == 0 ? 1.f : std::sqrt(0.5f);
        if (!avatar || std::abs(avatar->rotation.z - expected_z) > 0.0001f ||
            std::abs(avatar->rotation.w - expected_w) > 0.0001f)
            throw std::runtime_error("Local teleport did not turn the avatar toward its horizontal look direction");
    };
    check_local_teleport(3, {128, 128, 128}, {128, 128, 128}, -1);
    check_local_teleport(4, {128, 128, 22.5}, {150, 150, 40}, 0);

    if (!runtime.world().add(telehub))
        throw std::runtime_error("Could not place the local teleport test telehub");
    check_local_teleport(5, {151, 150, 40}, {151, 150, 40}, 0);
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return received_teleport_proximity_alert;
    }, "Local teleport did not report that the telehub would move the avatar farther away");
    const auto refused = runtime.viewers().agent(circuit->agent_id);
    if (!refused || refused->position.x != 150 || refused->position.y != 150)
        throw std::runtime_error("A refused local teleport moved the avatar");

    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        return avatar && avatar->position.z < 30 && std::abs(avatar->velocity.z) < .15f;
    }, "Avatar did not land before the refused-teleport movement test");
    auto walking = udp::make_packet("AgentUpdate");
    walking.sequence = 6;
    auto& walker = walking.block("AgentData");
    walker["AgentID"] = circuit->agent_id;
    walker["SessionID"] = circuit->session_id;
    walker["ControlFlags"] = std::uint64_t{1};
    walker["BodyRotation"] = std::vector<double>{0, 0, 0, 1};
    if (!viewer.send_to(endpoint, udp::encode_datagram(walking)))
        throw std::runtime_error("Could not start walking for the refused-teleport test");
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        return avatar && avatar->velocity.x > .5f;
    }, "Avatar did not begin walking before the refused teleport");
    check_local_teleport(7, {156, 150, 40}, {156, 150, 40}, -1);
    // Wait for physics publications so the immediate viewer-state velocity reset cannot satisfy the check.
    const auto observe_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < observe_until) {
        runtime.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        return avatar && avatar->position.x < 156 && std::abs(avatar->velocity.x) < .15f;
    }, "Refused teleport did not clear the physics movement target");

    check_local_teleport(8, {111, 64, 40}, {111, 64, 40}, 0);
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        return avatar && avatar->position.x == 112 && avatar->position.y == 64;
    }, "Local teleport did not land on the nearest telehub spawn point");
    if (!runtime.world().remove(telehub.id))
        throw std::runtime_error("Could not remove the local teleport test telehub");
    check_local_teleport(9, {199, 100, 40}, {199, 100, 40}, 0);
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        return avatar && avatar->position.x == 190 && avatar->position.y == 100;
    }, "Local teleport did not land at the parcel's configured landing point");
    check_local_teleport(10, {128, 128, 22.5}, {150, 150, 40}, 0);

    std::optional<ViewerAgent> before;
    poll_until([&] {
        before = runtime.viewers().agent(circuit->agent_id);
        return before && before->root && before->position.z > 30.f && before->velocity.z < -.2f;
    }, "Attachment physics wearer did not enter free fall");
    std::array<Vector3, 256> expected_wind;
    expected_wind.fill({3, 0, 0});
    const auto expected_wind_bytes = wind_layer_packet(expected_wind).data;
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return received_wind && *received_wind == expected_wind_bytes;
    }, "The viewer did not receive the configured wind layer");

    auto attachment = SceneObject::primitive(circuit->agent_id, {});
    attachment.is_attachment = true;
    attachment.parent_id = before->local_id;
    attachment.shape.state = 5;
    attachment.name = "Wearer collision attachment";
    const auto root = runtime.world().add(attachment, false);
    if (!root)
        throw std::runtime_error("Could not add the physics attachment");
    auto child = SceneObject::primitive(circuit->agent_id, {0.25f, 0, 0});
    child.parent_id = root->local_id;
    PrimitiveBaseShapeCore material_shape([](const std::string&) {});
    material_shape.render_materials = std::make_shared<ShapeRenderMaterials>();
    material_shape.render_materials->entries = std::make_shared<ShapeArray<ShapeMaterialEntry>>(
        std::vector<ShapeMaterialEntry>{{0, Uuid("7d55d860-e588-45d5-9631-7633cde238ce")},
                                        {2, Uuid("e23a5bcd-283b-44b2-aa4c-97ca53b88410")}});
    child.extra_params = material_shape.extra_params();
    if (!runtime.world().add(child, false))
        throw std::runtime_error("Could not add the physics attachment child");
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = item->parent_part_id = child.id;
    item->asset_id = script_asset;
    item->owner_id = item->creator_id = circuit->agent_id;
    item->type = item->inventory_type = 10;
    item->script_running = true;
    item->name = "Attachment physics";
    item->current_permissions = child.owner_mask;
    auto material_item = std::make_shared<TaskInventoryItem>(*item);
    material_item->item_id = Uuid("8d06d95d-c6a9-412a-8f8a-bab07e9f7650");
    material_item->asset_id = Uuid("e23a5bcd-283b-44b2-aa4c-97ca53b88410");
    material_item->name = "Named glTF material";
    material_item->type = material_item->inventory_type = 57;
    material_item->script_running = false;
    material_item->next_permissions = 1u << 13;
    auto no_copy_item = std::make_shared<TaskInventoryItem>(*material_item);
    no_copy_item->item_id = Uuid::random();
    no_copy_item->asset_id = Uuid::random();
    no_copy_item->name = "No-copy landmark";
    no_copy_item->type = no_copy_item->inventory_type = 3;
    no_copy_item->current_permissions &= ~(1u << 15);
    auto nontransferable = std::make_shared<TaskInventoryItem>(*no_copy_item);
    nontransferable->item_id = Uuid::random();
    nontransferable->name = "Nontransferable landmark";
    nontransferable->current_permissions = (no_copy_item->current_permissions | (1u << 15)) & ~(1u << 13);
    runtime.restore_task_inventory(child.id, {item, material_item, no_copy_item, nontransferable});

    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && wearer->position.x > before->position.x + .2f &&
               wearer->velocity.x > .5f && wearer->velocity.z > .3f;
    }, "Attachment script did not push and lift its wearer");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return map_requests.size() >= 2 && !url_requests.empty();
    }, "Attachment map or URL requests did not reach the wearer's UDP session");
    {
        std::lock_guard lock(sound_mutex);
        const auto check_map = [&](const udp::Packet& packet, std::string_view region_name,
                                   const std::vector<double>& position, std::optional<std::uint64_t> flags) {
            const auto& data = packet.block("Data");
            const auto field_text = [&](const char* field) {
                const auto& bytes = std::get<std::vector<std::uint8_t>>(data.at(field));
                return std::string(bytes.begin(), std::ranges::find(bytes, std::uint8_t{0}));
            };
            const auto& options = packet.blocks.at("Options");
            if (!packet.reliable || !packet.zerocoded || field_text("SimName") != region_name ||
                field_text("ObjectName") != child.name ||
                std::get<std::vector<double>>(data.at("SimPosition")) != position ||
                std::get<std::vector<double>>(data.at("LookAt")) != std::vector<double>{0, 0, 0} ||
                options.size() != static_cast<std::size_t>(flags.has_value()) ||
                (flags && std::get<std::uint64_t>(options.front().at("Flags")) != *flags))
                throw std::runtime_error("Incorrect script map packet fields");
        };
        check_map(map_requests[0], "Destination fixture", {64, 65, 30}, {});
        check_map(map_requests[1], "Beacon fixture", {96, 97, 40}, 1);
        const auto& data = url_requests.front().block("Data");
        const auto text = [&](const char* field) {
            const auto& bytes = std::get<std::vector<std::uint8_t>>(data.at(field));
            return std::string(bytes.begin(), std::ranges::find(bytes, std::uint8_t{0}));
        };
        if (std::get<Uuid>(data.at("ObjectID")) != child.id ||
            std::get<Uuid>(data.at("OwnerID")) != circuit->agent_id ||
            std::get<bool>(data.at("OwnerIsGroup")) || text("ObjectName") != child.name ||
            text("Message") != "Fixture URL" || text("URL") != "https://example.invalid/fixture")
            throw std::runtime_error("Incorrect llLoadURL packet fields");
    }
    const auto damage_root = runtime.world().object(root->id);
    const auto damage_child = runtime.world().object(child.id);
    if (!damage_root || !damage_child || damage_root->damage != 12.5f || damage_child->damage != -1.f)
        throw std::runtime_error("llSetDamage did not set damage on the attachment root");
    poll_until([&] {
        const auto part = runtime.world().object(child.id);
        return part && part->description == "Render material change received";
    }, "A scripted material edit did not deliver CHANGED_RENDER_MATERIAL to the affected prim");
    std::uint32_t chat_sequence = 10;
    const auto send_physics_command = [&](std::string_view text) {
        auto chat = udp::make_packet("ChatFromViewer");
        chat.sequence = ++chat_sequence;
        chat.block("AgentData")["AgentID"] = circuit->agent_id;
        chat.block("AgentData")["SessionID"] = circuit->session_id;
        auto& data = chat.block("ChatData");
        data["Channel"] = std::int64_t{7196};
        data["Type"] = std::uint64_t{1};
        data["Message"] = std::vector<std::uint8_t>(text.begin(), text.end());
        if (!viewer.send_to(endpoint, udp::encode_datagram(chat)))
            throw std::runtime_error("Could not send the attachment physics command");
    };
    const auto target_height = [&](const ViewerAgent& wearer) {
        return runtime.world().terrain().height_at(wearer.position.x, wearer.position.y) + 12.f;
    };
    if (!runtime.world().set_script_permission_mask(root->id, 0, root->base_mask & ~(1u << 15)))
        throw std::runtime_error("Could not restrict material asset disclosure on the fixture linkset");
    send_physics_command("hover");
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && std::abs(wearer->position.z - target_height(*wearer)) < .2f &&
               std::abs(wearer->velocity.z) < .25f;
    }, "Attachment did not hold its wearer at the terrain-relative hover height");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return !dialogs.empty();
    }, "The viewer did not receive the scripted dialog");
    if (!runtime.world().set_script_permission_mask(root->id, 0, root->base_mask))
        throw std::runtime_error("Could not restore the fixture linkset base permissions");
    const auto dialog_text = [](const auto& block, const char* field) {
        return framework::sdk_bytes_to_string(std::get<std::vector<std::uint8_t>>(block.at(field)));
    };
    {
        std::lock_guard lock(sound_mutex);
        const auto& dialog = dialogs.front();
        const auto& data = dialog.block("Data");
        const auto& buttons = dialog.blocks.at("Buttons");
        const Uuid image("00000000-0000-2222-3333-100000001000");
        if (std::get<Uuid>(data.at("ObjectID")) != child.id ||
            std::get<Uuid>(dialog.block("OwnerData").at("OwnerID")) != circuit->agent_id ||
            dialog_text(data, "ObjectName") != framework::utf16_replacing(child.name) ||
            dialog_text(data, "Message") != u"Stop hovering?" ||
            dialog_text(data, "FirstName") != framework::utf16_replacing(circuit->first_name.value_or("")) ||
            dialog_text(data, "LastName") != framework::utf16_replacing(circuit->last_name.value_or("")) ||
            std::get<std::int64_t>(data.at("ChatChannel")) != 7196 || std::get<Uuid>(data.at("ImageID")) != image ||
            buttons.size() != 2 || dialog_text(buttons[0], "ButtonLabel") != u"stop" ||
            dialog_text(buttons[1], "ButtonLabel") != u"42")
            throw std::runtime_error("Incorrect llDialog packet fields");
    }
    auto dialog_reply = udp::make_packet("ScriptDialogReply");
    dialog_reply.sequence = ++chat_sequence;
    auto& reply_identity = dialog_reply.block("AgentData");
    reply_identity["AgentID"] = circuit->agent_id;
    reply_identity["SessionID"] = circuit->session_id;
    auto& reply_data = dialog_reply.block("Data");
    reply_data["ObjectID"] = child.id;
    reply_data["ChatChannel"] = std::int64_t{7196};
    reply_data["ButtonIndex"] = std::int64_t{0};
    reply_data["ButtonLabel"] = std::vector<std::uint8_t>{'s', 't', 'o', 'p', 0};
    if (!viewer.send_to(endpoint, udp::encode_datagram(dialog_reply)))
        throw std::runtime_error("Could not send the script dialog reply");
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && wearer->position.z < target_height(*wearer) - .5f &&
               wearer->velocity.z < -1.f;
    }, "The script did not stop attachment hover after receiving the dialog reply");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return dialogs.size() >= 2;
    }, "The viewer did not receive the scripted text box");
    {
        std::lock_guard lock(sound_mutex);
        const auto& text_box = dialogs[1];
        const auto& data = text_box.block("Data");
        const auto& buttons = text_box.blocks.at("Buttons");
        if (std::get<Uuid>(data.at("ObjectID")) != child.id ||
            !std::get<Uuid>(data.at("ImageID")).is_zero() ||
            std::get<std::int64_t>(data.at("ChatChannel")) != 7196 ||
            dialog_text(data, "Message") != u"Enter a movement command" ||
            buttons.size() != 1 || dialog_text(buttons.front(), "ButtonLabel") != u"!!llTextBox!!")
            throw std::runtime_error("Incorrect llTextBox packet fields");
    }
    dialog_reply.sequence = ++chat_sequence;
    reply_data["ButtonLabel"] = std::vector<std::uint8_t>{'m', 'o', 'v', 'e', 0};
    if (!viewer.send_to(endpoint, udp::encode_datagram(dialog_reply)))
        throw std::runtime_error("Could not submit the script text box reply");
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && wearer->flying && std::abs(wearer->position.x - 150.f) < .1f &&
               std::abs(wearer->position.y - 155.f) < .1f && std::abs(wearer->position.z - 36.f) < .1f &&
               wearer->rotation.z > .5f;
    }, "Attachment move-to-target did not bring its wearer to the destination");
    send_physics_command("continue");
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && wearer->position.x > 150.4f && wearer->velocity.x > .5f;
    }, "Attachment did not start the second move-to-target");
    send_physics_command("cancel");
    poll_until([&] {
        const auto script_host = runtime.world().object(child.id);
        return script_host && script_host->description == "Movement cancelled";
    }, "Attachment did not cancel its target movement");
    auto idle = udp::make_packet("AgentUpdate");
    idle.sequence = ++chat_sequence;
    auto& idle_agent = idle.block("AgentData");
    idle_agent["AgentID"] = circuit->agent_id;
    idle_agent["SessionID"] = circuit->session_id;
    idle_agent["ControlFlags"] = std::uint64_t{0x2000u};
    if (!viewer.send_to(endpoint, udp::encode_datagram(idle)))
        throw std::runtime_error("Could not send neutral controls after cancelling attachment movement");
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && wearer->position.x < 160.f && std::abs(wearer->velocity.x) < .25f;
    }, "Stopping attachment move-to-target did not halt movement before the destination");
    send_physics_command("teleport");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return default_teleport == std::vector<double>{154, 155, 36} &&
               teleport_look == std::vector<double>{0, 1, 0} && teleport_flags == (16u | 8192u);
    }, "Scripted local teleport did not report the requested landing, look and flight flags");
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && wearer->flying && std::abs(wearer->position.x - 154.f) < .2f &&
               wearer->position.y > 155.03f && wearer->velocity.y > .25f;
    }, "Scripted local teleport did not apply momentum to the live avatar");
    restrict_script_teleport = true;
    send_physics_command("restricted teleport");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return default_teleport == std::vector<double>{167.5, 158.5, 36};
    }, "Scripted teleport did not choose the first equally near parcel and its inset landing point");
    restrict_script_teleport = false;
    send_physics_command("teleport");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return default_teleport == std::vector<double>{154, 155, 36};
    }, "Could not return the avatar to the ground movement test area");
    send_physics_command("viewer teleport");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return default_teleport == std::vector<double>{154, 156, 36} &&
               teleport_look == std::vector<double>{1, 0, 0};
    }, "osTeleportAgent did not enter the viewer teleport path");
    const auto send_controls = [&](std::uint32_t flags) {
        auto update = udp::make_packet("AgentUpdate");
        update.sequence = ++chat_sequence;
        auto& agent = update.block("AgentData");
        agent["AgentID"] = circuit->agent_id;
        agent["SessionID"] = circuit->session_id;
        agent["ControlFlags"] = std::uint64_t{flags};
        if (!viewer.send_to(endpoint, udp::encode_datagram(update)))
            throw std::runtime_error("Could not send controls for attachment ground movement");
    };
    send_controls(0);
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && !wearer->flying && wearer->position.z < target_height(*wearer) - 9.f &&
               std::abs(wearer->velocity.z) < .25f;
    }, "Attachment wearer did not land before the ground movement test");
    send_controls(0x20000u);
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && wearer->ground_sitting;
    }, "Attachment wearer did not sit on the ground");
    send_physics_command("walk");
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && !wearer->ground_sitting && !wearer->flying &&
               std::abs(wearer->position.x - 156.f) < .1f && std::abs(wearer->position.y - 157.f) < .1f;
    }, "Attachment did not stand its wearer up and walk to the ground destination");
    const float seat_height = runtime.world().terrain().height_at(156, 157) + .5f;
    const auto seat = runtime.world().add(SceneObject::primitive(circuit->agent_id, {156, 157, seat_height}));
    if (!seat)
        throw std::runtime_error("Could not set up the attachment wearer's seat");
    auto collision_script = std::make_shared<TaskInventoryItem>(*item);
    collision_script->item_id = Uuid::random();
    collision_script->parent_id = seat->id;
    collision_script->parent_part_id = seat->id;
    collision_script->asset_id = collision_asset;
    collision_script->name = "Avatar collision fixture";
    runtime.restore_task_inventory(seat->id, {collision_script});
    poll_until([&] {
        const auto platform = runtime.world().object(seat->id);
        return platform && (platform->description == "Avatar contact active" ||
                            platform->description == "Avatar contact ended");
    }, "The seat's script did not detect the avatar's collision");

    runtime.world().set_sit_target(seat->id, {0, 0, 1}, {0, 0, 0, 1});
    auto sit = udp::make_packet("AgentRequestSit");
    sit.sequence = ++chat_sequence;
    sit.block("AgentData")["AgentID"] = circuit->agent_id;
    sit.block("AgentData")["SessionID"] = circuit->session_id;
    sit.block("TargetObject")["TargetID"] = seat->id;
    if (!viewer.send_to(endpoint, udp::encode_datagram(sit)))
        throw std::runtime_error("Could not send the attachment wearer's sit request");
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && wearer->seat && wearer->seat->part == seat->id;
    }, "Attachment wearer did not sit on the object");
    poll_until([&] {
        const auto platform = runtime.world().object(seat->id);
        return platform && platform->description == "Avatar contact ended";
    }, "The seat's script did not detect the end of the avatar's collision");
    send_physics_command("seated move");
    poll_until([&] {
        const auto script_host = runtime.world().object(child.id);
        return script_host && script_host->description == "Seated movement attempted";
    }, "Attachment script did not attempt movement while seated");
    const auto seated_wearer = runtime.viewers().agent(circuit->agent_id);
    if (!seated_wearer || !seated_wearer->seat || seated_wearer->seat->part != seat->id)
        throw std::runtime_error("Attachment movement displaced its wearer from an object seat");
    send_physics_command("rotate attachment");
    poll_until([&] {
        const auto script_host = runtime.world().object(child.id);
        return script_host && script_host->description == "Attachment rotated" &&
               script_host->rotation == Quaternion{0, 0, 1, 0};
    }, "Nonphysical look-at did not update the attachment child's local rotation");

    const float ground = runtime.world().terrain().height_at(162, 157);
    const Uuid terrain_sound("be7295c0-a158-11e1-b3dd-0807200c9a66");
    const Uuid custom_sound("be7295c0-a158-11e1-b3dd-0803203c9a66");
    const auto drop = [&](std::string name) {
        auto falling = SceneObject::primitive(circuit->agent_id, {162, 157, ground + 4.f});
        falling.name = std::move(name);
        falling.flags = 1u;
        if (!runtime.world().add(falling))
            throw std::runtime_error("Could not rez the collision sound projectile");
        auto sound_script = std::make_shared<TaskInventoryItem>(*collision_script);
        sound_script->item_id = Uuid::random();
        sound_script->parent_id = falling.id;
        sound_script->parent_part_id = falling.id;
        runtime.restore_task_inventory(falling.id, {sound_script});
        return falling.id;
    };
    struct SoundCase {
        const char* name;
        Uuid asset;
        double gain;
    };
    const SoundCase cases[] = {
        {"Default impact", terrain_sound, 1.},
        {"Custom impact", custom_sound, .45},
        {"OSSL custom impact", custom_sound, .55},
        {"Scaled impact", terrain_sound, .35},
        {"Restored default impact", terrain_sound, 1.},
    };
    const auto expect_sound = [&](Uuid falling, const SoundCase& expected, float contact_height) {
        poll_until([&] {
            std::lock_guard lock(sound_mutex);
            return std::ranges::any_of(collision_sounds, [&](const auto& packet) {
                const auto& data = packet.block("SoundData");
                if (std::get<Uuid>(data.at("ObjectID")) != falling)
                    return false;
                const auto& position = std::get<std::vector<double>>(data.at("Position"));
                const auto gain = std::get<double>(data.at("Gain"));
                return std::get<Uuid>(data.at("ParentID")) == falling &&
                       std::get<Uuid>(data.at("OwnerID")) == circuit->agent_id &&
                       std::get<Uuid>(data.at("SoundID")) == expected.asset &&
                       std::abs(position.at(2) - contact_height) < .25f && std::abs(gain - expected.gain) < .01;
            });
        }, expected.name);
    };
    for (const auto& expected : cases) {
        const auto falling = drop(expected.name);
        expect_sound(falling, expected, ground);
        runtime.world().remove(falling);
    }

    auto platform = SceneObject::primitive(circuit->agent_id, {162, 157, ground + 1.5f});
    platform.name = "Scaled impact";
    platform.scale = {3, 3, 1};
    if (!runtime.world().add(platform))
        throw std::runtime_error("Could not rez the collision sound platform");
    auto platform_script = std::make_shared<TaskInventoryItem>(*collision_script);
    platform_script->item_id = Uuid::random();
    platform_script->parent_id = platform.id;
    platform_script->parent_part_id = platform.id;
    runtime.restore_task_inventory(platform.id, {platform_script});
    poll_until([&] {
        const auto part = runtime.world().object(platform.id);
        return part && part->collision_sound_volume_override;
    }, "The collision sound platform did not install its volume override");
    const auto falling_on_platform = drop("Default impact");
    expect_sound(falling_on_platform, {"Scaled wood-on-wood impact", custom_sound, .35}, ground + 2.f);
    runtime.world().remove(falling_on_platform);
    runtime.world().remove(platform.id);

    const auto disabled = drop("Disabled impact");
    poll_until([&] {
        const auto landed = runtime.world().object(disabled);
        return landed && landed->position.z < ground + .5f && std::abs(landed->velocity.z) < .15f;
    }, "The disabled collision sound projectile did not land");
    {
        std::lock_guard lock(sound_mutex);
        if (std::ranges::any_of(collision_sounds, [&](const auto& packet) {
                return std::get<Uuid>(packet.block("SoundData").at("ObjectID")) == disabled;
            }))
            throw std::runtime_error("A projectile with collision sounds disabled sent an impact sound");
    }
    runtime.world().remove(disabled);

    auto wearer_collision_script = std::make_shared<TaskInventoryItem>(*collision_script);
    wearer_collision_script->item_id = Uuid::random();
    wearer_collision_script->parent_id = root->id;
    wearer_collision_script->parent_part_id = root->id;
    runtime.restore_task_inventory(root->id, {wearer_collision_script});
    const auto expect_attachment_description = [&](std::string_view expected, const char* failure) {
        poll_until([&] {
            const auto worn = runtime.world().object(root->id);
            return worn && worn->description == expected;
        }, failure);
    };
    const auto teleport_above = [&](float x, float z) {
        auto teleport = udp::make_packet("TeleportLocationRequest");
        teleport.sequence = ++chat_sequence;
        teleport.block("AgentData")["AgentID"] = circuit->agent_id;
        teleport.block("AgentData")["SessionID"] = circuit->session_id;
        auto& destination = teleport.block("Info");
        destination["RegionHandle"] = runtime.descriptor().handle();
        destination["Position"] = std::vector<double>{x, 157, z};
        destination["LookAt"] = std::vector<double>{1, 0, 0};
        if (!viewer.send_to(endpoint, udp::encode_datagram(teleport)))
            throw std::runtime_error("Could not teleport the avatar above the impact location");
    };
    expect_attachment_description("Waiting for local teleport", "The attachment script did not start");
    teleport_above(175, runtime.world().terrain().height_at(175, 157) + 8.f);
    expect_attachment_description("Received local teleport", "The attachment script did not receive CHANGED_TELEPORT");

    const Uuid flesh_wood_sound("be7295c0-a158-11e1-b3dd-0803204c9a66");
    const Uuid disabled_sound("ffffffff-ffff-ffff-ffff-ffffffffffff");
    Uuid restricted_platform;
    struct AvatarImpactCase {
        float x;
        const char* name;
        Uuid sound;
        float volume;
        double expected_gain;
    };
    const AvatarImpactCase avatar_cases[] = {
        {168, "Parcel-restricted avatar impact", custom_sound, .45f, 0.},
        {172, "Scaled avatar impact", flesh_wood_sound, .35f, .07},
        {176, "Custom avatar impact", custom_sound, .45f, .45},
    };
    for (const auto& expected : avatar_cases) {
        const float floor = runtime.world().terrain().height_at(expected.x, 157);
        auto landing = SceneObject::primitive(circuit->agent_id, {expected.x, 157, floor + 1.5f});
        landing.scale = {3, 3, 1};
        if (expected.expected_gain == 0.)
            landing.name = "Wearer collision platform";
        if (!runtime.world().add(landing))
            throw std::runtime_error("Could not rez the avatar impact platform");
        const bool scaled = expected.sound == flesh_wood_sound;
        runtime.world().set_collision_sound(landing.id, scaled ? disabled_sound : expected.sound,
                                            expected.volume, scaled);

        teleport_above(expected.x, floor + 8.f);
        poll_until([&] {
            const auto avatar = runtime.viewers().agent(circuit->agent_id);
            return avatar && !avatar->seat && std::abs(avatar->position.x - expected.x) < .2f &&
                   avatar->position.z > floor + 2.5f && avatar->position.z < floor + 3.5f &&
                   std::abs(avatar->velocity.z) < .2f;
        }, "The avatar did not land on the impact platform");
        if (expected.expected_gain == 0.) {
            restricted_platform = landing.id;
            expect_attachment_description("Wearer contact active",
                                          "The attachment did not receive start and ongoing wearer contacts");
        } else {
            expect_sound(landing.id, {expected.name, expected.sound, expected.expected_gain}, floor + 2.f);
            runtime.world().remove(landing.id);
        }
    }
    expect_attachment_description("Wearer contact ended",
                                  "The attachment did not receive the end of the wearer contact");
    runtime.world().remove(restricted_platform);

    const float landing_ground = runtime.world().terrain().height_at(180, 157);
    teleport_above(180, landing_ground + 8.f);
    expect_attachment_description("Wearer land contact active",
                                  "The attachment did not receive start and ongoing wearer land contacts");
    teleport_above(180, landing_ground + 8.f);
    expect_attachment_description("Wearer land contact ended",
                                  "The attachment did not receive the end of the wearer land contact");
    const float damage_ground = runtime.world().terrain().height_at(184, 157);
    auto damaging_platform = SceneObject::primitive(circuit->agent_id, {184, 157, damage_ground + 1.5f});
    damaging_platform.scale = {3, 3, 1};
    damaging_platform.damage = 12.5f;
    if (!runtime.world().add(damaging_platform))
        throw std::runtime_error("Could not rez the scripted damage platform");
    teleport_above(184, damage_ground + 8.f);
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        std::lock_guard lock(sound_mutex);
        return avatar && avatar->health >= 87.5f && avatar->health < 90.f &&
               !runtime.world().object(damaging_platform.id) &&
               std::ranges::find(health_updates, 87.5) != health_updates.end();
    }, "Scripted collision did not reduce health, notify the viewer and remove the damaging object");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return std::ranges::any_of(health_updates, [](double health) { return health > 87.5 && health < 100.; });
    }, "Passive recovery did not send a viewer health update");
    auto ordinary_platform = SceneObject::primitive(circuit->agent_id, {184, 157, damage_ground + 1.5f});
    ordinary_platform.scale = {3, 3, 1};
    if (!runtime.world().add(ordinary_platform))
        throw std::runtime_error("Could not rez the ordinary impact platform");
    teleport_above(184, damage_ground + 15.f);
    const auto observe_fall_start = [&] {
        float health = 0.f;
        poll_until([&] {
            const auto avatar = runtime.viewers().agent(circuit->agent_id);
            if (!avatar || avatar->position.z <= damage_ground + 8.f)
                return false;
            health = avatar->health;
            return true;
        }, "The avatar did not teleport above the impact site");
        return health;
    };
    auto health_before_impact = observe_fall_start();
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        if (!avatar)
            return false;
        health_before_impact = std::max(health_before_impact, avatar->health);
        return avatar->health < health_before_impact && runtime.world().object(ordinary_platform.id).has_value();
    }, "A hard impact against an ordinary prim did not reduce avatar health");
    runtime.world().remove(ordinary_platform.id);
    teleport_above(184, damage_ground + 15.f);
    health_before_impact = observe_fall_start();
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        if (!avatar)
            return false;
        health_before_impact = std::max(health_before_impact, avatar->health);
        return avatar->health < health_before_impact;
    }, "A hard terrain impact on a damage-enabled parcel did not reduce avatar health");
    {
        std::lock_guard lock(sound_mutex);
        if (std::ranges::any_of(collision_sounds, [&](const auto& packet) {
                return std::get<Uuid>(packet.block("SoundData").at("ObjectID")) == restricted_platform;
            }))
            throw std::runtime_error("An avatar impact sound played on a parcel that disallows avatar sounds");
    }

    auto mute = udp::make_packet("AgentUpdate");
    mute.sequence = ++chat_sequence;
    auto& mute_data = mute.block("AgentData");
    mute_data["AgentID"] = circuit->agent_id;
    mute_data["SessionID"] = circuit->session_id;
    mute_data["Flags"] = std::uint64_t{0x80};
    if (!viewer.send_to(endpoint, udp::encode_datagram(mute)))
        throw std::runtime_error("Could not set the viewer collision mute flag");
    poll_until([&] {
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        return wearer && wearer->mute_collisions;
    }, "The region did not apply the viewer collision mute flag");

    auto muted = SceneObject::primitive(circuit->agent_id, {162, 157, ground + 4.f});
    muted.flags = 1u;
    muted.name = "Collider report fixture";
    if (!runtime.world().add(muted))
        throw std::runtime_error("Could not rez the muted collision sound projectile");
    poll_until([&] {
        const auto landed = runtime.world().object(muted.id);
        return landed && landed->position.z < ground + .5f && std::abs(landed->velocity.z) < .15f;
    }, "The muted projectile did not land");
    {
        std::lock_guard lock(sound_mutex);
        if (std::ranges::any_of(collision_sounds, [&](const auto& packet) {
                return std::get<Uuid>(packet.block("SoundData").at("ObjectID")) == muted.id;
            }))
            throw std::runtime_error("A collision sound reached a muted viewer");
    }
    auto request = udp::make_packet("LandStatRequest");
    request.sequence = ++chat_sequence;
    request.block("AgentData")["AgentID"] = circuit->agent_id;
    request.block("AgentData")["SessionID"] = circuit->session_id;
    auto& fields = request.block("RequestData");
    fields["ReportType"] = std::uint64_t{1};
    fields["RequestFlags"] = std::uint64_t{4};
    const std::string filter = "COLLIDER REPORT";
    fields["Filter"] = std::vector<std::uint8_t>(filter.begin(), filter.end());
    if (!viewer.send_to(endpoint, udp::encode_datagram(request)))
        throw std::runtime_error("Could not request top colliders");

    const auto caps = std::dynamic_pointer_cast<Caps>(
        runtime.capabilities()->capabilities().get_caps_for_user(circuit->circuit_code));
    std::shared_ptr<CapabilityPollHandler> event_queue;
    if (!caps || !caps->try_get_poll("EventQueueGet", event_queue))
        throw std::runtime_error("The collider report viewer has no event queue");
    const auto poll_id = Uuid::random();
    poll_until([&] {
        if (!event_queue->has_events(poll_id, circuit->agent_id))
            return false;
        const auto reply = event_queue->get_events(poll_id, circuit->agent_id);
        if (!reply || !reply->contains("bin_response_data"))
            return false;
        const auto bytes = std::any_cast<server::PollBytes>(reply->get("bin_response_data"));
        const auto events = network::parse_llsd_xml(std::string(bytes->begin(), bytes->end()));
        for (const auto& event : events.get("events").array) {
            if (event.get("message").as_string() != "LandStatReply")
                continue;
            const auto body = event.get("body");
            const auto header = body.get("RequestData").array.at(0);
            const auto rows = body.get("ReportData").array;
            if (network::osd_unsigned(header.get("ReportType")) != 1 ||
                network::osd_unsigned(header.get("TotalObjectCount")) != 1 || rows.size() != 1)
                continue;
            const auto& row = rows.front();
            if (row.get("TaskID").as_string() == muted.id.str() && row.get("TaskName").as_string() == muted.name &&
                row.get("Score").real == 1. && network::osd_unsigned(header.get("RequestFlags")) == 4)
                return true;
        }
        return false;
    }, "The viewer did not receive the filtered top collider and its score");
    runtime.world().remove(muted.id);
    auto lethal_platform = SceneObject::primitive(circuit->agent_id, {184, 157, damage_ground + 1.5f});
    lethal_platform.scale = {3, 3, 1};
    lethal_platform.damage = 110.f;
    if (!runtime.world().add(lethal_platform))
        throw std::runtime_error("Could not rez the lethal collision platform");
    teleport_above(184, damage_ground + 8.f);
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        std::lock_guard lock(sound_mutex);
        return avatar && std::abs(avatar->position.x - 188.f) < .2f && avatar->health == 100.f &&
               received_death_alert && std::ranges::any_of(health_updates, [](double health) { return health <= 0.; }) &&
               std::ranges::find(health_updates, 100.) != health_updates.end() &&
               !runtime.world().object(lethal_platform.id);
    }, "Lethal collision did not notify the viewer, restore health and teleport the avatar home");
    teleport_above(184, damage_ground + 8.f);
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        return avatar && std::abs(avatar->position.x - 184.f) < .2f;
    }, "The avatar did not enter the script owner's parcel");
    send_physics_command("eject");
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        std::lock_guard lock(sound_mutex);
        return avatar && received_eject_alert &&
               (avatar->position.x < 183.f || avatar->position.x >= 185.f ||
                avatar->position.y < 156.f || avatar->position.y >= 158.f);
    }, "llEjectFromLand did not relocate the avatar outside the owner's parcel and send an alert");
    teleport_above(184, damage_ground + 8.f);
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        return avatar && std::abs(avatar->position.x - 184.f) < .2f &&
               std::abs(avatar->position.y - 157.f) < .2f;
    }, "The ejected avatar did not return to the script owner's parcel");
    send_physics_command("message");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return std::ranges::any_of(object_messages, [&](const auto& packet) {
            const auto& data = packet.block("MessageBlock");
            return std::get<Uuid>(packet.block("AgentData").at("AgentID")) == circuit->agent_id &&
                   std::get<Uuid>(data.at("ID")) == child.id &&
                   std::get<Uuid>(data.at("ToAgentID")) == circuit->agent_id &&
                   std::get<std::uint64_t>(data.at("Dialog")) == 19 &&
                   dialog_text(data, "Message") == u"Scripted object message";
        });
    }, "llInstantMessage did not send the expected object message");
    send_physics_command("forward message");
    poll_until([&] {
        return forwarded_script_message.has_value();
    }, "llInstantMessage did not forward the message for an absent recipient");
    const auto& forwarded = *forwarded_script_message;
    const auto location = framework::sdk_bytes_to_string(forwarded.bucket);
    if (forwarded.sender != circuit->agent_id || forwarded.session != child.id ||
        forwarded.recipient != Uuid("8840d11b-4f83-4c7c-b11d-7e41ef0789ad") || forwarded.region != region.id ||
        forwarded.dialog != 19 || forwarded.from_group || forwarded.offline != 0 || !forwarded.timestamp ||
        forwarded.sender_name != framework::utf16_replacing(child.name) ||
        forwarded.message != u"Forwarded object message" ||
        !location.starts_with(framework::utf16_replacing(region.name.value_or("") + "/")))
        throw std::runtime_error("Incorrect llInstantMessage grid forwarding envelope");
    send_physics_command("give inventory");
    Uuid given_item;
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        for (const auto& packet : object_messages) {
            const auto& data = packet.block("MessageBlock");
            if (std::get<std::uint64_t>(data.at("Dialog")) == 9 &&
                std::get<std::vector<std::uint8_t>>(data.at("BinaryBucket")) == std::vector<std::uint8_t>{57}) {
                if (std::get<std::uint64_t>(data.at("Offline")) != 1 ||
                    std::get<std::uint64_t>(data.at("Timestamp")) == 0 ||
                    std::get<Uuid>(data.at("RegionID")) != region.id ||
                    dialog_text(data, "FromAgentName") != framework::utf16_replacing(child.name))
                    throw std::runtime_error("Incorrect llGiveInventory offer metadata");
                given_item = std::get<Uuid>(data.at("ID"));
                return !given_item.is_zero();
            }
        }
        return false;
    }, "llGiveInventory did not send the task offer to the avatar");
    const auto gift = inventory.item(circuit->agent_id, given_item);
    const auto root_inventory = runtime.snapshot_task_inventory(root->id);
    const auto child_inventory = runtime.snapshot_task_inventory(child.id);
    const auto has_material = [](const auto& items) {
        return items && std::ranges::any_of(*items, [](const auto& item) {
            return item.name == "Named glTF material";
        });
    };
    if (!gift || gift->name != "Named glTF material" || !has_material(root_inventory) || !has_material(child_inventory))
        throw std::runtime_error("llGiveInventory did not copy the task item to both destinations");
    auto asset_request = udp::make_packet("TransferRequest");
    asset_request.sequence = ++chat_sequence;
    const auto transfer_id = Uuid::random();
    auto& transfer = asset_request.block("TransferInfo");
    transfer["TransferID"] = transfer_id;
    transfer["ChannelType"] = std::int64_t{2};
    transfer["SourceType"] = std::int64_t{3};
    transfer["Priority"] = 1.0;
    std::vector<std::uint8_t> params(64, 0);
    const auto& item_id_bytes = given_item.bytes();
    const auto& asset_id_bytes = gift->asset_id.bytes();
    params.insert(params.end(), item_id_bytes.begin(), item_id_bytes.end());
    params.insert(params.end(), asset_id_bytes.begin(), asset_id_bytes.end());
    transfer["Params"] = std::move(params);
    if (!viewer.send_to(endpoint, udp::encode_datagram(asset_request)))
        throw std::runtime_error("Could not request the asset for the scripted gift");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return std::ranges::any_of(asset_transfers, [&](const auto& packet) {
            const auto& data = packet.block("TransferData");
            return std::get<Uuid>(data.at("TransferID")) == transfer_id &&
                   std::get<std::vector<std::uint8_t>>(data.at("Data")) == std::vector<std::uint8_t>{1, 3, 5, 7};
        });
    }, "The viewer could not download the asset from its gifted inventory item");
    auto external_target = SceneObject::primitive(Uuid::random(), {189, 158, 30});
    external_target.id = Uuid("653bc91d-c05a-4a90-980c-05f26d8c8488");
    if (!runtime.world().add(external_target))
        throw std::runtime_error("Could not create the cross-owner inventory target");
    send_physics_command("give cross-owner inventory");
    poll_until([&] {
        const auto contents = runtime.snapshot_task_inventory(external_target.id);
        return contents && std::ranges::any_of(*contents, [&](const auto& received) {
            constexpr std::uint32_t allowed = (1u << 13) | (1u << 19);
            return received.asset_id == material_item->asset_id && received.owner_id == external_target.owner &&
                   received.parent_id == external_target.id &&
                   received.current_permissions == (material_item->current_permissions & allowed) &&
                   received.base_permissions == (material_item->base_permissions & allowed) &&
                   (received.flags & 0x100u) != 0;
        });
    }, "llGiveInventory did not propagate next-owner permissions to the receiving prim");
    send_physics_command("give nontransferable inventory");
    poll_until([&] {
        const auto part = runtime.world().object(child.id);
        return part && part->description == "Nontransferable gift attempted";
    }, "The script did not attempt the nontransferable gift");
    const auto receiving_inventory = runtime.snapshot_task_inventory(external_target.id);
    const auto sending_inventory = runtime.snapshot_task_inventory(child.id);
    if (!receiving_inventory || !sending_inventory ||
        std::ranges::any_of(*receiving_inventory, [&](const auto& received) {
            return received.name == nontransferable->name;
        }) ||
        !std::ranges::any_of(*sending_inventory, [&](const auto& source_item) {
            return source_item.item_id == nontransferable->item_id;
        }))
        throw std::runtime_error("llGiveInventory did not reject a nontransferable cross-owner gift");
    send_physics_command("give object inventory list");
    poll_until([&] {
        const auto part = runtime.world().object(child.id);
        return part && part->description == "Object inventory list processed";
    }, "The script did not finish giving the object inventory list");
    const auto list_target = runtime.snapshot_task_inventory(external_target.id);
    const auto list_source = runtime.snapshot_task_inventory(child.id);
    if (!list_target || list_target->size() != 2 ||
        !std::ranges::all_of(*list_target, [&](const auto& received) {
            return received.asset_id == material_item->asset_id;
        }) ||
        !list_source || !std::ranges::any_of(*list_source, [&](const auto& original) {
            return original.item_id == no_copy_item->item_id;
        }))
        throw std::runtime_error("llGiveInventoryList did not copy only the eligible UUID-selected item");
    runtime.world().remove(external_target.id);
    send_physics_command("give no-copy inventory");
    Uuid moved_item;
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        for (const auto& packet : object_messages) {
            const auto& data = packet.block("MessageBlock");
            if (std::get<std::uint64_t>(data.at("Dialog")) == 9 &&
                std::get<std::vector<std::uint8_t>>(data.at("BinaryBucket")) == std::vector<std::uint8_t>{3}) {
                moved_item = std::get<Uuid>(data.at("ID"));
                return !moved_item.is_zero();
            }
        }
        return false;
    }, "llGiveInventory did not offer the no-copy landmark");
    const auto moved = inventory.item(circuit->agent_id, moved_item);
    const auto source_inventory = runtime.snapshot_task_inventory(child.id);
    if (!moved || moved->name != "No-copy landmark" || moved->asset_id != no_copy_item->asset_id ||
        !source_inventory || std::ranges::any_of(*source_inventory, [&](const auto& entry) {
            return entry.item_id == no_copy_item->item_id;
        }))
        throw std::runtime_error("llGiveInventory did not move the no-copy landmark out of the prim");
    send_physics_command("give remote inventory");
    poll_until([&] { return forwarded_inventory_offer.has_value(); },
               "llGiveInventory did not forward an offer for the online grid user");
    const Uuid remote_recipient("8840d11b-4f83-4c7c-b11d-7e41ef0789ad");
    const auto& remote_offer = *forwarded_inventory_offer;
    const auto remote_item = inventory.item(remote_recipient, remote_offer.session);
    if (remote_offer.recipient != remote_recipient || remote_offer.sender != circuit->agent_id ||
        remote_offer.dialog != 9 || remote_offer.offline != 1 ||
        remote_offer.bucket != std::vector<std::uint8_t>{57} ||
        !remote_item || remote_item->name != "Named glTF material")
        throw std::runtime_error("llGiveInventory did not persist and forward the remote offer");
    send_physics_command("give inventory list");
    Uuid list_folder;
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        for (const auto& packet : object_messages) {
            const auto& data = packet.block("MessageBlock");
            if (std::get<std::uint64_t>(data.at("Dialog")) == 9 &&
                std::get<std::vector<std::uint8_t>>(data.at("BinaryBucket")) == std::vector<std::uint8_t>{8} &&
                dialog_text(data, "Message") == u"'Scripted gift folder'") {
                if (std::get<std::uint64_t>(data.at("Offline")) != 0 ||
                    std::get<std::uint64_t>(data.at("Timestamp")) != 0 ||
                    std::get<Uuid>(data.at("RegionID")) != region.id ||
                    dialog_text(data, "FromAgentName") != framework::utf16_replacing(child.name))
                    throw std::runtime_error("Incorrect llGiveInventoryList offer metadata");
                list_folder = std::get<Uuid>(data.at("ID"));
                return !list_folder.is_zero();
            }
        }
        return false;
    }, "llGiveInventoryList did not send a folder offer");
    const auto created_folder = inventory.folder(circuit->agent_id, list_folder);
    const auto list_contents = inventory.content(circuit->agent_id, list_folder);
    if (!created_folder || created_folder->name != "Scripted gift folder" ||
        list_contents.items.size() != 1 || list_contents.items.front().asset_id != material_item->asset_id)
        throw std::runtime_error("llGiveInventoryList did not store the eligible item in the offered folder");
    send_physics_command("home");
    poll_until([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        return avatar && std::abs(avatar->position.x - 188.f) < .2f;
    }, "llTeleportAgentHome did not return the avatar to its home");
    auto retrieve = udp::make_packet("RetrieveInstantMessages");
    retrieve.sequence = ++chat_sequence;
    retrieve.block("AgentData")["AgentID"] = circuit->agent_id;
    retrieve.block("AgentData")["SessionID"] = circuit->session_id;
    if (!viewer.send_to(endpoint, udp::encode_datagram(retrieve)))
        throw std::runtime_error("Could not request the offline inventory offers");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        const auto has_offer = [&](std::uint64_t dialog, std::uint64_t offline, std::u16string_view text) {
            return std::ranges::any_of(object_messages, [&](const auto& packet) {
                const auto& data = packet.block("MessageBlock");
                return std::get<std::uint64_t>(data.at("Dialog")) == dialog &&
                       std::get<std::uint64_t>(data.at("Offline")) == offline &&
                       std::get<Uuid>(data.at("ToAgentID")) == circuit->agent_id &&
                       std::get<std::vector<std::uint8_t>>(data.at("BinaryBucket")) ==
                           (dialog == 9 ? std::vector<std::uint8_t>{6} : std::vector<std::uint8_t>{6, 0, 7}) &&
                       dialog_text(data, "Message") == text;
            });
        };
        return has_offer(4, 0, u"Queued avatar offer") && has_offer(9, 1, u"Queued task offer");
    }, "Offline inventory offers did not preserve their dialog, payload and offline flags");
    poll_until([&] {
        const Uuid folder("d095367f-c602-4370-b216-45b3d874d114");
        const Uuid item("5189e39b-aa55-4379-95e9-9e41da9af9a3");
        std::lock_guard lock(sound_mutex);
        const bool refreshed = std::ranges::any_of(bulk_inventory_updates, [&](const auto& packet) {
            return packet.blocks.contains("FolderData") && packet.blocks.contains("ItemData") &&
                   std::ranges::any_of(packet.blocks.at("FolderData"), [&](const auto& row) {
                       return std::get<Uuid>(row.at("FolderID")) == folder;
                   }) &&
                   std::ranges::any_of(packet.blocks.at("ItemData"), [&](const auto& row) {
                       return std::get<Uuid>(row.at("ItemID")) == item && std::get<Uuid>(row.at("FolderID")) == folder;
                   });
        });
        return refreshed && std::ranges::any_of(object_messages, [&](const auto& packet) {
            const auto& data = packet.block("MessageBlock");
            return std::get<Uuid>(data.at("ID")) == folder && std::get<std::uint64_t>(data.at("Dialog")) == 9 &&
                   std::get<std::vector<std::uint8_t>>(data.at("BinaryBucket")) == std::vector<std::uint8_t>{8};
        });
    }, "The viewer did not receive the offered folder refresh and task offer");
    InventoryFolder destination;
    destination.id = Uuid::random();
    destination.owner = circuit->agent_id;
    destination.parent = Uuid("2ff93757-fc90-43fb-bd26-ce55337e3d5b");
    destination.name = "Accepted task offers";
    destination.type = -1;
    if (!inventory.add_folder(destination))
        throw std::runtime_error("Could not create the task offer destination folder");
    auto accept = udp::make_packet("ImprovedInstantMessage");
    accept.sequence = ++chat_sequence;
    accept.block("AgentData")["AgentID"] = circuit->agent_id;
    accept.block("AgentData")["SessionID"] = circuit->session_id;
    auto& offer = accept.block("MessageBlock");
    const Uuid accepted_item("5189e39b-aa55-4379-95e9-9e41da9af9a3");
    offer["ID"] = accepted_item;
    offer["Dialog"] = std::uint64_t{10};
    const auto& destination_bytes = destination.id.bytes();
    offer["BinaryBucket"] = std::vector<std::uint8_t>(destination_bytes.begin(), destination_bytes.end());
    if (!viewer.send_to(endpoint, udp::encode_datagram(accept)))
        throw std::runtime_error("Could not send the task inventory acceptance");
    poll_until([&] {
        const auto item = inventory.item(circuit->agent_id, accepted_item);
        return item && item->parent == destination.id;
    }, "Task inventory acceptance did not move the item into the selected folder");
    accept.sequence = ++chat_sequence;
    offer["Dialog"] = std::uint64_t{11};
    offer["BinaryBucket"] = std::vector<std::uint8_t>{};
    if (!viewer.send_to(endpoint, udp::encode_datagram(accept)))
        throw std::runtime_error("Could not send the task inventory decline");
    poll_until([&] {
        const auto item = inventory.item(circuit->agent_id, accepted_item);
        return item && item->parent == Uuid("e48cfb3b-cc27-4365-af86-84b4312dbf56");
    }, "Task inventory decline did not move the item to Trash");
    const Uuid offered_folder("d095367f-c602-4370-b216-45b3d874d114");
    const Uuid folder_contents("7ccff012-60de-462e-b82d-e3ca172258b0");
    const Uuid trash("e48cfb3b-cc27-4365-af86-84b4312dbf56");
    const auto check_folder_response = [&](bool decline, Uuid parent) {
        accept.sequence = ++chat_sequence;
        offer["ID"] = offered_folder;
        offer["Dialog"] = std::uint64_t(decline ? 11 : 10);
        offer["BinaryBucket"] = decline ? std::vector<std::uint8_t>{}
            : std::vector<std::uint8_t>(destination_bytes.begin(), destination_bytes.end());
        if (!viewer.send_to(endpoint, udp::encode_datagram(accept)))
            throw std::runtime_error("Could not send the task folder response");
        poll_until([&] {
            const auto folder = inventory.folder(circuit->agent_id, offered_folder);
            const auto item = inventory.item(circuit->agent_id, folder_contents);
            if (!folder || folder->parent != parent || !item || item->parent != offered_folder)
                return false;
            std::lock_guard lock(sound_mutex);
            return std::ranges::any_of(bulk_inventory_updates, [&](const auto& packet) {
                return std::get<Uuid>(packet.block("AgentData").at("TransactionID")) == offered_folder &&
                       packet.blocks.contains("FolderData") &&
                       std::ranges::any_of(packet.blocks.at("FolderData"), [&](const auto& row) {
                           return std::get<Uuid>(row.at("FolderID")) == offered_folder &&
                                  std::get<Uuid>(row.at("ParentID")) == parent;
                       });
            });
        }, decline ? "Task folder decline did not move and refresh the folder in Trash"
                   : "Task folder acceptance did not move and refresh the offered folder");
    };
    check_folder_response(false, destination.id);
    check_folder_response(true, trash);
    auto ground_attach = SceneObject::primitive(circuit->agent_id, {189, 157, 30});
    ground_attach.name = "Script attach from ground";
    if (!runtime.world().add(ground_attach))
        throw std::runtime_error("Could not add the ground attachment fixture");
    auto attach_script = std::make_shared<TaskInventoryItem>(*item);
    attach_script->item_id = Uuid::random();
    attach_script->parent_id = attach_script->parent_part_id = ground_attach.id;
    runtime.restore_task_inventory(ground_attach.id, {attach_script});
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return permission_question == attach_script->item_id;
    }, "Ground attachment script did not request PERMISSION_ATTACH");
    const auto before_grant = runtime.world().object(ground_attach.id);
    if (!before_grant || before_grant->is_attachment)
        throw std::runtime_error("Ground object attached before the viewer granted permission");
    auto answer = udp::make_packet("ScriptAnswerYes");
    answer.sequence = ++chat_sequence;
    answer.block("AgentData")["AgentID"] = circuit->agent_id;
    answer.block("AgentData")["SessionID"] = circuit->session_id;
    answer.block("Data")["TaskID"] = ground_attach.id;
    answer.block("Data")["ItemID"] = attach_script->item_id;
    answer.block("Data")["Questions"] = std::int64_t{0x20};
    if (!viewer.send_to(endpoint, udp::encode_datagram(answer)))
        throw std::runtime_error("Could not grant the script attachment permission");
    poll_until([&] {
        const auto attached = runtime.world().object(ground_attach.id);
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        if (!attached || !wearer || !attached->is_attachment || attached->parent_id != wearer->local_id ||
            attached->shape.state != 5 || attached->name != "Script attached immediately" ||
            attached->description != "Script attach event")
            return false;
        const auto appearance = wearer->appearance();
        const auto inventory_item = inventory.item(circuit->agent_id, attached->from_inventory_item);
        return appearance && appearance->attachpoint(attached->from_inventory_item) == 5 &&
               inventory_item && inventory_item->asset_id == attached->attachment_source_asset &&
               runtime.world().object(root->id).has_value();
    }, "Granted ground script did not append an inventory attachment and receive its attach event");

    for (const bool transferable : {false, true}) {
        const auto original_owner = Uuid::random();
        constexpr auto transfer = bits(PermissionMask::Transfer);
        constexpr auto copy = bits(PermissionMask::Copy);
        auto temporary = SceneObject::primitive(original_owner, {190, 157, 30});
        temporary.owner_mask = copy | (transferable ? transfer : 0);
        temporary.next_mask = transfer;
        temporary.sale_type = 2;
        temporary.sale_price = 50;
        const auto temporary_root = runtime.world().add(temporary);
        if (!temporary_root)
            throw std::runtime_error("Could not add the temporary attachment fixture");
        auto temporary_child = SceneObject::primitive(original_owner, {0.5f, 0, 0});
        temporary_child.parent_id = temporary_root->local_id;
        temporary_child.next_mask = transfer;
        temporary_child.name = "Temporary script attach";
        temporary_child.description = circuit->agent_id.str();
        if (!runtime.world().add(temporary_child))
            throw std::runtime_error("Could not add the temporary attachment child");
        auto temporary_script = std::make_shared<TaskInventoryItem>(*item);
        temporary_script->item_id = Uuid::random();
        temporary_script->parent_id = temporary_script->parent_part_id = temporary_child.id;
        temporary_script->owner_id = original_owner;
        temporary_script->current_permissions = temporary_script->base_permissions = copy | transfer;
        temporary_script->next_permissions = transfer;
        runtime.restore_task_inventory(temporary_child.id, {temporary_script});
        poll_until([&] {
            std::lock_guard lock(sound_mutex);
            return permission_question == temporary_script->item_id;
        }, "Temporary attachment script did not request permission from the wearer");
        answer.sequence = ++chat_sequence;
        answer.block("Data")["TaskID"] = temporary_child.id;
        answer.block("Data")["ItemID"] = temporary_script->item_id;
        if (!viewer.send_to(endpoint, udp::encode_datagram(answer)))
            throw std::runtime_error("Could not grant temporary attachment permission");
        poll_until([&] {
            const auto host = runtime.world().object(temporary_child.id);
            return host && host->name == "Temporary attach attempted" &&
                   (!transferable || host->description == "Script attach event");
        }, "Temporary attachment script did not finish");
        const auto object = runtime.world().object(temporary.id);
        const auto host = runtime.world().object(temporary_child.id);
        const auto wearer = runtime.viewers().agent(circuit->agent_id);
        const auto contents = runtime.snapshot_task_inventory(temporary_child.id);
        if (!object || !host || !wearer || !contents || contents->size() != 1)
            throw std::runtime_error("Temporary attachment fixture disappeared");
        if (transferable) {
            const auto appearance = wearer->appearance();
            const auto& script = contents->front();
            if (!object->is_attachment || object->parent_id != wearer->local_id || object->shape.state != 5 ||
                !object->from_inventory_item.is_zero() || !appearance || appearance->attachments().size() != 1 ||
                object->owner != circuit->agent_id || host->owner != circuit->agent_id ||
                object->last_owner != original_owner || object->sale_type != 0 || object->sale_price != 10 ||
                object->owner_mask != transfer || script.owner_id != circuit->agent_id ||
                script.current_permissions != transfer || script.permissions_granter != circuit->agent_id ||
                !(script.permissions_mask & 0x20))
                throw std::runtime_error("Temporary attachment did not preserve its grant and apply ownership transfer");
        } else if (object->is_attachment || object->owner != original_owner || contents->front().owner_id != original_owner) {
            throw std::runtime_error("Nontransferable object changed ownership or attached");
        }
        runtime.world().remove(temporary.id);
    }

    send_physics_command("detach without grant");
    poll_until([&] {
        const auto host = runtime.world().object(child.id);
        return host && host->description == "Detach without grant attempted";
    }, "Attachment script did not attempt detachment without permission");
    runtime.poll();
    if (!runtime.world().object(root->id) || !runtime.world().object(child.id))
        throw std::runtime_error("Attachment detached without permission");
    send_physics_command("detach with grant");
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return received_detach_event && !runtime.world().object(root->id) && !runtime.world().object(child.id);
    }, "Authorized child script did not receive the detach event and remove the temporary attachment");
    const auto make_link_prim = [&](const char* name, Vector3 position, std::string description = {}) {
        auto prim = SceneObject::primitive(circuit->agent_id, position);
        prim.name = name;
        prim.description = std::move(description);
        const auto added = runtime.world().add(prim);
        if (!added)
            throw std::runtime_error("Could not create the script link fixture");
        return *added;
    };
    const auto target = make_link_prim("Target link root", {190, 157, 30});
    const auto target_child = make_link_prim("Target link child", {191, 157, 30});
    const auto controller = make_link_prim("Script link fixture", {192, 157, 30}, target.id.str());
    const auto original_child = make_link_prim("Original link child", {193, 157, 30});
    if (!runtime.world().link_prim(original_child.id, controller.id) ||
        !runtime.world().link_prim(target_child.id, target.id))
        throw std::runtime_error("Could not prepare the two script linksets");

    auto link_script = std::make_shared<TaskInventoryItem>(*item);
    link_script->item_id = Uuid::random();
    link_script->parent_id = link_script->parent_part_id = controller.id;
    runtime.restore_task_inventory(controller.id, {link_script});
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return permission_question == link_script->item_id;
    }, "Link script did not ask the owner for PERMISSION_CHANGE_LINKS");
    if (runtime.world().link_key(original_child.id, 1) != controller.id)
        throw std::runtime_error("Link script unlinked a child without permission");
    answer.sequence = ++chat_sequence;
    answer.block("Data")["TaskID"] = controller.id;
    answer.block("Data")["ItemID"] = link_script->item_id;
    answer.block("Data")["Questions"] = std::int64_t{0x80};
    if (!viewer.send_to(endpoint, udp::encode_datagram(answer)))
        throw std::runtime_error("Could not grant link permission");
    poll_until([&] {
        const auto host = runtime.world().object(controller.id);
        return host && host->name == "Script link operations finished";
    }, "Script did not complete link insertion, root promotion and unlink operations");
    for (const auto& part : {controller, target, target_child, original_child}) {
        const auto separate = runtime.world().object(part.id);
        if (!separate || separate->parent_id || separate->position != part.position)
            throw std::runtime_error("Script unlink did not preserve a part's world position");
        runtime.world().remove(part.id);
    }
    const auto singleton = make_link_prim("Empty link operations", {194, 157, 30});
    auto empty_link_script = std::make_shared<TaskInventoryItem>(*item);
    empty_link_script->item_id = Uuid::random();
    empty_link_script->parent_id = empty_link_script->parent_part_id = singleton.id;
    runtime.restore_task_inventory(singleton.id, {empty_link_script});
    poll_until([&] {
        std::lock_guard lock(sound_mutex);
        return permission_question == empty_link_script->item_id;
    }, "Singleton script did not ask for link permission");
    answer.sequence = ++chat_sequence;
    answer.block("Data")["TaskID"] = singleton.id;
    answer.block("Data")["ItemID"] = empty_link_script->item_id;
    if (!viewer.send_to(endpoint, udp::encode_datagram(answer)))
        throw std::runtime_error("Could not grant singleton link permission");
    poll_until([&] {
        const auto part = runtime.world().object(singleton.id);
        return part && part->description == "2";
    }, "Self-link and empty root unlink did not emit two link change events");
    runtime.poll();
    const auto unchanged = runtime.world().object(singleton.id);
    if (!unchanged || unchanged->description != "2" || unchanged->parent_id ||
        runtime.world().prim_count(singleton.id, false) != 1)
        throw std::runtime_error("Empty link operations changed the singleton or emitted extra events");
    runtime.world().remove(singleton.id);
    const auto avatar_sensor = make_link_prim("Avatar sensor fixture", {188, 157, 30});
    auto avatar_sensor_script = std::make_shared<TaskInventoryItem>(*item);
    avatar_sensor_script->item_id = Uuid::random();
    avatar_sensor_script->parent_id = avatar_sensor_script->parent_part_id = avatar_sensor.id;
    avatar_sensor_script->asset_id = sensor_asset;
    runtime.restore_task_inventory(avatar_sensor.id, {avatar_sensor_script});
    poll_until([&] {
        const auto host = runtime.world().object(avatar_sensor.id);
        return host && host->name == "Avatar sensor found owner";
    }, "Avatar sensor did not detect the root agent by key");
    runtime.world().remove(avatar_sensor.id);
    const std::array<const char*, 5> remote_scripts{
        "Named remote teleport", "Grid remote teleport", "Position remote teleport",
        "LSL global teleport", "LSL landmark teleport",
    };
    for (const auto script_name : remote_scripts) {
        const auto cancellations_before = remote_cancellations.load();
        const auto host = make_link_prim(script_name, {188, 157, 30});
        auto teleport_script = std::make_shared<TaskInventoryItem>(*item);
        teleport_script->item_id = Uuid::random();
        teleport_script->parent_id = teleport_script->parent_part_id = host.id;
        auto landmark = std::make_shared<TaskInventoryItem>();
        landmark->item_id = Uuid::random();
        landmark->parent_id = landmark->parent_part_id = host.id;
        landmark->asset_id = landmark_asset;
        landmark->name = "Script landmark";
        landmark->type = landmark->inventory_type = 3;
        {
            std::lock_guard lock(sound_mutex);
            remote_teleport_finish.reset();
        }
        runtime.restore_task_inventory(host.id, {teleport_script, landmark});
        const std::string_view mode(script_name);
        if (mode.starts_with("LSL")) {
            poll_until([&] {
                std::lock_guard lock(sound_mutex);
                return permission_question == teleport_script->item_id;
            }, "The LSL teleport script did not request viewer permission");
            auto grant = udp::make_packet("ScriptAnswerYes");
            grant.sequence = ++chat_sequence;
            auto& grant_agent = grant.block("AgentData");
            grant_agent["AgentID"] = circuit->agent_id;
            grant_agent["SessionID"] = circuit->session_id;
            auto& answer_data = grant.block("Data");
            answer_data["TaskID"] = host.id;
            answer_data["ItemID"] = teleport_script->item_id;
            answer_data["Questions"] = std::int64_t{0x1000};
            if (!viewer.send_to(endpoint, udp::encode_datagram(grant)))
                throw std::runtime_error("Could not grant LSL teleport permission");
        }
        const std::string failure = std::string(script_name) + " did not negotiate the destination and reach the viewer";
        poll_until([&] {
            std::lock_guard lock(sound_mutex);
            return remote_teleport_finish == remote_handle;
        }, failure.c_str());
        auto cancel = udp::make_packet("TeleportCancel");
        cancel.sequence = ++chat_sequence;
        auto& info = cancel.block("Info");
        info["AgentID"] = circuit->agent_id;
        info["SessionID"] = circuit->session_id;
        if (!viewer.send_to(endpoint, udp::encode_datagram(cancel)))
            throw std::runtime_error("Could not cancel the scripted remote teleport");
        poll_until([&] {
            return remote_cancellations.load() == cancellations_before + 1;
        }, "Cancelling the scripted teleport did not close the receiving circuit");
        runtime.world().remove(host.id);
    }
    runtime.close_agent_session(circuit->agent_id, circuit->session_id.str());
    runtime.world().remove(seat->id);
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
    const auto wait_for_upload_completion = [&](Uuid id) {
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
    };
    const auto check_uploaded = [&](Uuid id, const std::vector<std::uint8_t>& expected) {
        wait_for_upload_completion(id);
        const auto asset = uploaded_asset(id);
        if (!asset || !asset->data || *asset->data != expected || !asset->local() || !asset->temporary() ||
            asset->creator_id() != circuit.agent_id.str())
            throw std::runtime_error("Uploaded asset storage differs from the viewer request");
    };
    upload(Uuid("28731084-aa42-4fd8-8014-2027257d21c1"), {7, 0, 9});
    check_uploaded(Uuid("83f2cb8e-8d5a-1a33-22e2-f4b5ba4bb964"), {7, 0, 9});

    const Uuid rejected_local_asset("c5f90cb1-d285-d478-39f9-a1f6281f9ac9");
    upload(Uuid("28731084-aa42-4fd8-8014-2027257d21ce"), {7, 0, 9});
    wait_for_upload_completion(rejected_local_asset);
    if (uploaded_asset(rejected_local_asset))
        throw std::runtime_error("Rejected local upload unexpectedly persisted");

    const Uuid multipart_id("1104ffd3-7e88-7876-be92-03f28163512b");
    std::uint64_t xfer_id = 0;
    const auto wait_for_xfer = [&](Uuid asset) {
        wait(
            [&](const auto& packets) {
                for (const auto& packet : packets) {
                    if (packet.spec->name != "RequestXfer")
                        continue;
                    const auto& fields = packet.block("XferID");
                    if (std::get<Uuid>(fields.at("VFileID")) == asset) {
                        xfer_id = std::get<std::uint64_t>(fields.at("ID"));
                        return xfer_id != 0;
                    }
                }
                return false;
            },
            "Viewer did not receive the asset upload Xfer ID");
    };
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
    upload(Uuid("28731084-aa42-4fd8-8014-2027257d21c2"), {});
    wait_for_xfer(multipart_id);
    part(0, {5, 0, 0, 0, 1, 2, 3});
    part(0x80000001, {4, 5});
    check_uploaded(multipart_id, {1, 2, 3, 4, 5});

    const Uuid irregular_id("2f3f74ec-95d8-5d78-e6d8-7bf807bcc4d7");
    upload(Uuid("28731084-aa42-4fd8-8014-2027257d21cf"), {});
    wait_for_xfer(irregular_id);
    part(0, {99, 0, 0, 0, 1});
    part(4, {77, 0, 0, 0, 2, 3});
    part(0x80000001, {4, 5});
    check_uploaded(irregular_id, {2, 3, 4, 5});

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
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [&](const auto& packet) {
            if (packet.spec->name != "ImprovedTerseObjectUpdate")
                return false;
            return std::ranges::any_of(packet.blocks.at("ObjectData"), [&](const auto& row) {
                const auto& data = std::get<std::vector<std::uint8_t>>(row.at("Data"));
                if (data.size() != 60 || data[5] != 1)
                    return false;
                const auto read_u32 = [&](std::size_t offset) {
                    return std::uint32_t(data[offset]) | (std::uint32_t(data[offset + 1]) << 8) |
                           (std::uint32_t(data[offset + 2]) << 16) | (std::uint32_t(data[offset + 3]) << 24);
                };
                return read_u32(0) == landed_viewer->local_id &&
                       std::bit_cast<float>(read_u32(14)) > .9f &&
                       std::bit_cast<float>(read_u32(18)) > 1.f;
            });
        });
    }, "Viewer did not receive its avatar's ground collision plane in a terse block");
    const auto scheduled = runtime.world().add(SceneObject::primitive(circuit.agent_id, {160, 100, 25}));
    if (!scheduled ||
        !runtime.world().patch_transform(scheduled->id, {}, {}, Vector3{1, 1, 1}) ||
        !runtime.world().patch_transform(scheduled->id, {}, {}, Vector3{2, 2, 2}))
        throw std::runtime_error("Could not stage two changes to the scheduled object");
    wait([&](const auto& packets) {
        bool saw_final = false;
        for (const auto& packet : packets) {
            if (packet.spec->name != "ObjectUpdate")
                continue;
            for (const auto& row : packet.blocks.at("ObjectData")) {
                if (std::get<Uuid>(row.at("FullID")) != scheduled->id)
                    continue;
                const auto& scale = std::get<std::vector<double>>(row.at("Scale"));
                if (scale != std::vector<double>{2, 2, 2})
                    throw std::runtime_error("Scheduled creation published a stale scale");
                saw_final |= scale == std::vector<double>{2, 2, 2};
            }
        }
        return saw_final;
    }, "Scheduled object updates did not publish the final scale");
    const auto marker = runtime.world().add(SceneObject::primitive(circuit.agent_id, {161, 100, 25}));
    if (!marker)
        throw std::runtime_error("Could not create the terse update tick marker");
    const auto terse_payload = [&](SceneObject part, Vector3 angular_velocity) {
        part.angular_velocity = angular_velocity;
        const auto packet = object_terse_update(part, runtime.descriptor().handle());
        return std::get<std::vector<std::uint8_t>>(packet.block("ObjectData").at("Data"));
    };
    const auto contains_terse = [](const auto& packets, const auto& payload) {
        return std::ranges::any_of(packets, [&](const auto& packet) {
            return packet.spec->name == "ImprovedTerseObjectUpdate" &&
                   std::ranges::any_of(packet.blocks.at("ObjectData"), [&](const auto& row) {
                       return std::get<std::vector<std::uint8_t>>(row.at("Data")) == payload;
                   });
        });
    };
    const Vector3 baseline{.1f, 0, 0};
    const Vector3 small_change{.1019f, 0, 0};
    const Vector3 large_change{.5f, 0, 0};
    const auto baseline_payload = terse_payload(*scheduled, baseline);
    const auto small_payload = terse_payload(*scheduled, small_change);
    if (baseline_payload == small_payload)
        throw std::runtime_error("The terse filter fixture quantized both velocities to the same payload");
    if (!runtime.world().update_angular_velocity(scheduled->id, baseline))
        throw std::runtime_error("Could not set the initial terse angular velocity");
    wait([&](const auto& packets) { return contains_terse(packets, baseline_payload); },
         "Viewer did not receive the terse motion baseline");
    if (!runtime.world().update_angular_velocity(scheduled->id, small_change) ||
        !runtime.world().update_angular_velocity(marker->id, large_change))
        throw std::runtime_error("Could not queue the small angular change and tick marker");
    const auto marker_payload = terse_payload(*marker, large_change);
    wait([&](const auto& packets) { return contains_terse(packets, marker_payload); },
         "Viewer did not receive the scheduled terse tick marker");
    const Vector3 marker_second_change{.9f, 0, 0};
    if (!runtime.world().update_angular_velocity(scheduled->id, large_change) ||
        !runtime.world().update_angular_velocity(marker->id, marker_second_change))
        throw std::runtime_error("Could not queue two angular changes for the same tick");
    const auto large_payload = terse_payload(*scheduled, large_change);
    const auto marker_second_payload = terse_payload(*marker, marker_second_change);
    wait([&](const auto& packets) {
        if (contains_terse(packets, small_payload))
            throw std::runtime_error("Viewer received a terse packet for the sub-threshold angular change");
        return std::ranges::any_of(packets, [&](const auto& packet) {
            const auto single_packet = std::span{&packet, 1};
            return contains_terse(single_packet, large_payload) &&
                   contains_terse(single_packet, marker_second_payload);
        });
    }, "Viewer did not receive both angular changes in one terse packet");
    runtime.world().remove(marker->id);
    runtime.world().remove(scheduled->id);
    const auto removed = runtime.world().add(SceneObject::primitive(circuit.agent_id, {160, 100, 25}));
    if (!removed || !runtime.world().remove(removed->id))
        throw std::runtime_error("Could not remove the object before its scheduled creation update");
    wait([&](const auto& packets) {
        bool saw_kill = false;
        for (const auto& packet : packets) {
            if (packet.spec->name == "ObjectUpdate")
                for (const auto& row : packet.blocks.at("ObjectData"))
                    if (std::get<Uuid>(row.at("FullID")) == removed->id)
                        throw std::runtime_error("A removed object was published from the creation queue");
            if (packet.spec->name == "KillObject")
                for (const auto& row : packet.blocks.at("ObjectData"))
                    saw_kill |= std::get<std::uint64_t>(row.at("ID")) == removed->local_id;
        }
        return saw_kill;
    }, "Removing a queued object did not send KillObject");
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
    auto taken_script = std::make_shared<TaskInventoryItem>();
    taken_script->item_id = Uuid::random();
    taken_script->asset_id = duplicate_script_asset;
    taken_script->owner_id = circuit.agent_id;
    taken_script->parent_id = taken_script->parent_part_id = take_root->id;
    taken_script->type = taken_script->inventory_type = 10;
    taken_script->script_running = true;
    auto stopped_script = std::make_shared<TaskInventoryItem>(*taken_script);
    stopped_script->item_id = Uuid::random();
    stopped_script->script_running = false;
    runtime.restore_task_inventory(take_root->id, {taken_script, stopped_script});
    wait([&](const auto&) { return runtime.world().object(take_root->id)->floating_text == "fresh"; },
         "The inventory checkpoint script did not start before Take");
    auto checkpoint_chat = udp::make_packet("ChatFromViewer");
    checkpoint_chat.block("AgentData")["AgentID"] = circuit.agent_id;
    checkpoint_chat.block("AgentData")["SessionID"] = circuit.session_id;
    auto& chat_data = checkpoint_chat.block("ChatData");
    chat_data["Channel"] = std::int64_t{7196};
    chat_data["Type"] = std::uint64_t{2};
    chat_data["Message"] = std::vector<std::uint8_t>{'s', 'a', 'v', 'e'};
    transmit(std::move(checkpoint_chat));
    wait([&](const auto&) { return runtime.world().object(take_root->id)->floating_text == "checkpoint ready"; },
         "The inventory checkpoint script did not update its global before Take");

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
    const auto taken_item = std::ranges::find(source_items, take_object.name, &InventoryItem::name);
    if (taken_item == source_items.end() ||
        std::ranges::count(source_items, take_object.name, &InventoryItem::name) != 1)
        throw std::runtime_error("Take did not put the object back in its source folder");

    auto rez_taken = udp::make_packet("RezObject");
    rez_taken.block("AgentData")["AgentID"] = circuit.agent_id;
    rez_taken.block("AgentData")["SessionID"] = circuit.session_id;
    rez_taken.block("InventoryData")["ItemID"] = taken_item->id;
    auto& placement = rez_taken.block("RezData");
    placement["RayStart"] = std::vector<double>{160, 105, 45};
    placement["RayEnd"] = std::vector<double>{160, 105, 25};
    placement["RayEndIsIntersection"] = true;
    transmit(std::move(rez_taken));
    Uuid rezzed_taken;
    wait([&](const auto&) {
        for (const auto& object : runtime.world().objects()) {
            if (object.name == take_object.name && object.floating_text == "Inventory rez 42") {
                rezzed_taken = object.id;
                return true;
            }
        }
        return false;
    }, "Rezzing the taken object did not restore the script global before on_rez");
    const auto rezzed_scripts = runtime.snapshot_task_inventory(rezzed_taken);
    if (!rezzed_scripts || rezzed_scripts->size() != 2 ||
        std::ranges::count(*rezzed_scripts, true, &TaskInventoryItem::script_running) != 1 ||
        std::ranges::any_of(*rezzed_scripts, [&](const auto& item) {
            return item.item_id == taken_script->item_id || item.item_id == stopped_script->item_id;
        }))
        throw std::runtime_error("Rezzed scripts did not preserve running state under their new inventory IDs");
    runtime.world().remove(rezzed_taken);

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
    destination["LookAt"] = std::vector<double>{0, -1, 0};
    const auto seated_avatar = runtime.viewers().agent(circuit.agent_id);
    const auto seat_root = runtime.world().object(prim->local_id);
    if (!seated_avatar || !seated_avatar->seat || !seat_root)
        throw std::runtime_error("Could not read the avatar's seated rotation before teleport");
    const auto standing_rotation =
        framework::sdk_multiply_quaternion(seat_root->rotation, seated_avatar->seat->rotation);
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
        return cleared_camera && arrived && unparented && avatar && !avatar->seat &&
               avatar->rotation == standing_rotation;
    }, "Local teleport did not leave the seat facing its world rotation and deliver the destination");
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
    crossing_root.script_access_pin = -2147483647;
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

    auto crossing_plant = SceneObject::primitive(circuit.agent_id, {250, 120, 26}, {4, 4, 10});
    crossing_plant.shape.pcode = 111;
    crossing_plant.shape.state = 7;
    crossing_plant.flags = 0x400;
    const auto plant = runtime.world().add(crossing_plant);
    if (!plant)
        throw std::runtime_error("Could not create outgoing Cypress");
    move_across_border(plant->local_id);
    wait([&](const auto&) { return !runtime.world().object(plant->id); },
         "Accepted Cypress crossing did not remove the source object");

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
        policies.default_landing = {150, 150, 40};
        struct YEngineStateDirectory {
            std::filesystem::path root = std::filesystem::temp_directory_path() /
                ("opensim-yengine-state-" + Uuid::random().str());
            YEngineStateDirectory() {
                if (!std::filesystem::create_directory(root))
                    throw std::runtime_error("Could not create YEngine state directory fixture");
            }
            ~YEngineStateDirectory() {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        } yengine_state_directory;
        policies.yengine_state_root = yengine_state_directory.root;
        policies.wind.model = RegionPolicies::Wind::Model::Configurable;
        policies.wind.strength_variation = 0.f;
        policies.wind.direction_variation = 0.f;
        policies.retrieve_offline_messages = [](Uuid recipient) {
            if (recipient != Uuid("c37a78be-6c52-4f63-a84c-f4bbd8567b05"))
                return std::vector<RegionInstantMessage>{};
            RegionInstantMessage avatar_offer;
            avatar_offer.sender = Uuid("8840d11b-4f83-4c7c-b11d-7e41ef0789ad");
            avatar_offer.recipient = recipient;
            avatar_offer.session = Uuid::random();
            avatar_offer.dialog = 4;
            avatar_offer.sender_name = u"Offline sender";
            avatar_offer.message = u"Queued avatar offer";
            avatar_offer.bucket = {6, 0, 7};
            auto task_offer = avatar_offer;
            task_offer.session = Uuid::random();
            task_offer.dialog = 9;
            task_offer.message = u"Queued task offer";
            auto folder_offer = task_offer;
            folder_offer.session = Uuid("d095367f-c602-4370-b216-45b3d874d114");
            folder_offer.message = u"Queued folder offer";
            folder_offer.bucket = {8, 0, 7};
            return std::vector<RegionInstantMessage>{std::move(avatar_offer), std::move(task_offer),
                                                     std::move(folder_offer)};
        };
        std::optional<RegionInstantMessage> forwarded_script_message;
        std::optional<RegionInstantMessage> forwarded_inventory_offer;
        policies.forward_instant_message = [&](const RegionInstantMessage& message) {
            if (message.recipient != Uuid("8840d11b-4f83-4c7c-b11d-7e41ef0789ad"))
                return;
            if (message.dialog == 9)
                forwarded_inventory_offer = message;
            else if (message.dialog == 19)
                forwarded_script_message = message;
        };
        policies.script_grid_user_online = [](Uuid agent) {
            return agent == Uuid("8840d11b-4f83-4c7c-b11d-7e41ef0789ad");
        };
        policies.environment_enabled = true;
        ScriptMailbox script_mailbox;
        policies.script_email_events = [&] {
            return PartEmailEvents{
                [&](Uuid part) {
                    script_mailbox.add(part);
                    script_mailbox.insert(part, {"122", "other@example.invalid", "fixture", "other sender"});
                    script_mailbox.insert(part, {"123", "sender@example.invalid", "fixture", "region mailbox"});
                    script_mailbox.insert(part, {"124", "sender@example.invalid", "other", "other subject"});
                },
                [&](Uuid part) { script_mailbox.remove(part); },
            };
        };
        policies.next_script_email = [&](Uuid part, std::string_view address, std::string_view subject) {
            return script_mailbox.next(part, address, subject);
        };
        policies.allow_god_functions = true;
        policies.grid_region_at = [&](std::int32_t x, std::int32_t y) {
            return x == region.x + region.size_x + 1 &&
                   (y == region.y + 128 || y == region.y + region.size_y + 1);
        };
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
        policies.scene_frame_time = .08f;
        policies.update_objects_every_n_frames = 2;
        policies.animations = std::make_shared<DefaultAnimations>(argv[2]);
        if (argc == 4)
            policies.spawn_point_routing = argv[3];
        policies.use_trash_on_delete = policies.spawn_point_routing != "sequence";
        policies.access = [routing = policies.spawn_point_routing](Uuid agent, const auto& position, auto& reason) {
            if (agent == Uuid("c37a78be-6c52-4f63-a84c-f4bbd8567b05") &&
                position == Vector3{128, 128, 22.5f})
                return true;
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
        InventoryFolder offered_folder;
        offered_folder.id = Uuid("d095367f-c602-4370-b216-45b3d874d114");
        offered_folder.owner = Uuid("c37a78be-6c52-4f63-a84c-f4bbd8567b05");
        offered_folder.parent = Uuid("2ff93757-fc90-43fb-bd26-ce55337e3d5b");
        offered_folder.name = "Offered task folder";
        offered_folder.type = -1;
        InventoryItem offered_item;
        offered_item.id = Uuid("5189e39b-aa55-4379-95e9-9e41da9af9a3");
        offered_item.owner = offered_folder.owner;
        offered_item.parent = offered_folder.id;
        offered_item.name = "Offered task item";
        InventoryFolder inventory_root;
        inventory_root.id = offered_folder.parent;
        inventory_root.owner = offered_folder.owner;
        inventory_root.name = "My Inventory";
        inventory_root.type = 8;
        auto retained_item = offered_item;
        retained_item.id = Uuid("7ccff012-60de-462e-b82d-e3ca172258b0");
        if (!inventory_store.store(inventory_root) || !inventory.add_folder(offered_folder) ||
            !inventory.add_item(offered_item) || !inventory.add_item(retained_item))
            throw std::runtime_error("Could not seed the offered task folder");
        InventoryFolder offer_trash;
        offer_trash.id = Uuid("e48cfb3b-cc27-4365-af86-84b4312dbf56");
        offer_trash.owner = offered_folder.owner;
        offer_trash.parent = inventory_root.id;
        offer_trash.name = "Trash";
        offer_trash.type = 14;
        if (!inventory_store.store(offer_trash))
            throw std::runtime_error("Could not seed the task offer trash folder");
        InventoryFolder remote_inventory_root = inventory_root;
        remote_inventory_root.id = Uuid::random();
        remote_inventory_root.owner = Uuid("8840d11b-4f83-4c7c-b11d-7e41ef0789ad");
        if (!inventory_store.store(remote_inventory_root))
            throw std::runtime_error("Could not seed the remote recipient inventory");
        policies.can_edit = [](Uuid agent, const SceneObject& object) { return agent == object.owner; };
        policies.can_move = policies.can_edit;
        auto crossing_destination = region;
        crossing_destination.id = Uuid::random();
        crossing_destination.x += 256;
        auto script_destination = region;
        script_destination.id = Uuid::random();
        script_destination.name = "Remote Script Region";
        script_destination.x = 1024;
        script_destination.y = 768;
        script_destination.size_x = 512;
        constexpr std::uint64_t script_destination_tile = (std::uint64_t{1280} << 32) | 768;
        policies.teleport_destination = [crossing_destination, script_destination](std::uint64_t handle) {
            return std::optional{handle == script_destination_tile ? script_destination : crossing_destination};
        };
        policies.teleport_destination_by_name = [script_destination](const std::string& name)
            -> std::optional<GridRegion> {
            return name == script_destination.name ? std::optional{script_destination} : std::nullopt;
        };
        std::atomic<unsigned> remote_cancellations = 0;
        const std::array<Vector3, 5> script_destination_positions{
            Vector3{280, 35, 42}, Vector3{281, 36, 43}, Vector3{282, 37, 44},
            Vector3{283, 38, 45}, Vector3{284, 39, 46},
        };
        policies.teleport_access = [&, destination_id = script_destination.id](
                                      GridRegion& destination, const ViewerAgent& avatar, Vector3 position,
                                      bool teleport, LoginTransferContext&) -> std::string {
            const auto index = remote_cancellations.load();
            if (!teleport || destination.id != destination_id || avatar.circuit->caps_path != "attachment-physics" ||
                index >= script_destination_positions.size() || position != script_destination_positions[index])
                return "Incorrect scripted teleport access request";
            return {};
        };
        policies.teleport_create = [&, destination_id = script_destination.id](
                                       const GridRegion&, GridRegion&, GridRegion& destination,
                                       const ViewerAgent& avatar, Vector3 position, Vector3 look,
                                       LoginTransferContext&, const Uuid& token, std::uint32_t flags,
                                       const AgentData*, std::string& reason) -> std::optional<network::lludp::Packet> {
            const auto index = remote_cancellations.load();
            if (destination.id != destination_id || token.is_zero() || flags != (index == 4 ? 10u : 16u) ||
                look != Vector3{0, 1, 0} ||
                index >= script_destination_positions.size() || position != script_destination_positions[index]) {
                reason = "Incorrect scripted teleport creation request";
                return std::nullopt;
            }
            auto packet = network::lludp::make_packet("TeleportFinish");
            auto& info = packet.block("Info");
            info["AgentID"] = avatar.circuit->agent_id;
            info["RegionHandle"] = destination.handle();
            info["TeleportFlags"] = std::uint64_t{flags};
            return packet;
        };
        policies.cancel_teleport_destination = [&, destination_id = script_destination.id](
                                                  GridRegion& destination, Uuid, Uuid) {
            if (destination.id == destination_id)
                ++remote_cancellations;
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
            const auto& root = parts.front().object;
            if (root.shape.pcode == 111)
                return parts.size() == 1 && position == Vector3{4, 120, 26} &&
                       root.shape.state == 7 && root.scale == Vector3{4, 4, 10} &&
                       (root.flags & 0x400u) != 0;
            if (parts.size() == 1 && parts.front().object.name == "Crossing script")
                return position == Vector3{5, 120, 26} && parts.front().object.position == Vector3{251, 120, 26} &&
                       state && state->find("<NativeCheckpoint>") != std::string::npos;
            return xml.starts_with("<SceneObjectGroup><SceneObjectPart>") &&
                   position == Vector3{4, 120, 26} && parts.size() == 2 &&
                   parts.front().object.name == "Crossing accepted" &&
                   parts.front().object.script_access_pin == -2147483647 &&
                   !parts.front().object.object_animations.empty() && parts.back().inventory.size() == 1 &&
                   state && state->find(parts.back().inventory.front().item_id.str()) != std::string::npos &&
                   state->find("<Running>False</Running>") != std::string::npos;
        };
        policies.can_rez = [](Uuid, const Vector3&, std::string&) { return true; };
        network::HttpServerOptions script_http_options;
        script_http_options.port = 0;
        script_http_options.workers = 1;
        network::HttpServer script_http_server(script_http_options, [](const network::HttpRequest& request) {
            const auto valid = request.method == "POST" && request.path == "/script" && request.body == "fixture" &&
                std::ranges::any_of(request.headers, [](const auto& header) {
                    return header.first == "x-fixture" && header.second == "custom";
                }) && request.headers.contains("authorization") &&
                request.headers.at("authorization") == "Basic dXNlcjpzZWNyZXQ=";
            network::HttpResponse response;
            response.status = valid ? 201 : 400;
            response.body = valid ? "network reply" : "unexpected request";
            return response;
        });
        const auto script_http_port = script_http_server.start();
        policies.script_outbound_http = std::make_shared<network::ScriptHttpModule>(
            std::make_shared<network::ScriptHttpService>(
                std::make_shared<OutboundUrlFilter>(OutboundUrlFilter::default_disallow,
                    "127.0.0.1:" + std::to_string(script_http_port)), 16384, 2000, 1));
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
        opensim::framework::AtomicSharedPtr<const ScriptParcel> current_other_parcel{other_parcel};
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
        opensim::framework::AtomicSharedPtr<const ScriptParcel> current_sale_parcel{sale_parcel};
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
        std::atomic<bool> restrict_script_teleport = false;
        const auto make_teleport_parcel = [&](int first_x, int end_x, Vector3d center) {
            auto parcel = std::make_shared<ScriptParcel>();
            parcel->id = Uuid::random();
            parcel->owner = other_parcel->owner;
            parcel->center = center;
            const auto width = region.size_x / 4;
            const auto height = region.size_y / 4;
            parcel->land_bitmap.resize((width * height + 7) / 8);
            for (int x = first_x; x < end_x; ++x) {
                const auto cell = 39 * width + x;
                parcel->land_bitmap[cell / 8] |= 1u << (cell % 8);
            }
            return parcel;
        };
        auto west_landing = make_teleport_parcel(40, 42, {164, std::nextafter(158.f, 159.f), 0});
        auto east_landing = make_teleport_parcel(45, 48, {186, 158, 0});
        auto restricted_landing = make_teleport_parcel(43, 45, {176, 158, 0});
        restricted_landing->flags |= 1u << 9;
        policies.list_parcels = [=, &current_other_parcel, &current_sale_parcel, &restrict_script_teleport] {
            if (restrict_script_teleport)
                return std::vector<std::shared_ptr<const ScriptParcel>>{
                    west_landing, east_landing, restricted_landing};
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
        const auto estate_access_target = Uuid::random();
        policies.script_account = [estate_owner = region.estate_owner, estate_access_target, &grant_viewer_admin](Uuid agent)
            -> std::optional<UserAccount> {
            if (agent == estate_access_target) {
                UserAccount account(agent);
                account.user_flags = 36;
                return account;
            }
            if (agent != estate_owner || !grant_viewer_admin)
                return std::nullopt;
            UserAccount account(agent);
            account.user_level = 200;
            return account;
        };
        const auto hub = SceneObject::primitive(Uuid::random(), {96, 64, 25});
        auto silent_parcel = std::make_shared<ScriptParcel>(*current_other_parcel.load());
        silent_parcel->any_avatar_sounds = false;
        silent_parcel->group_avatar_sounds = false;
        auto combat_parcel = std::make_shared<ScriptParcel>(*current_other_parcel.load());
        combat_parcel->flags |= 0x20u;
        combat_parcel->owner = Uuid("c37a78be-6c52-4f63-a84c-f4bbd8567b05");
        const auto script_landmark = std::make_shared<AssetBase>(
            Uuid::random(), "Script landmark", static_cast<std::int8_t>(AssetType::Landmark), "");
        script_landmark->data = std::make_shared<std::vector<std::uint8_t>>(1, 1);
        policies.resolve_landmark = [handle = region.handle(), script_landmark, script_destination](
                                      const ViewerAgent& avatar, Uuid landmark,
                                                              network::lludp::Packet& request) -> std::string {
            if (landmark == script_landmark->full_id()) {
                auto& info = request.block("Info");
                info["RegionHandle"] = script_destination.handle();
                info["Position"] = std::vector<double>{284, 39, 46};
                return {};
            }
            if (!landmark.is_zero() || avatar.circuit->caps_path != "attachment-physics")
                return "Your home region not found";
            auto& info = request.block("Info");
            info["RegionHandle"] = handle;
            info["Position"] = std::vector<double>{188, 157, 30};
            info["LookAt"] = std::vector<double>{1, 0, 0};
            return {};
        };
        auto teleport_parcel = std::make_shared<ScriptParcel>(*current_other_parcel.load());
        teleport_parcel->landing_type = 1;
        teleport_parcel->landing_point = {190, 100, 40};
        policies.script_parcel_at = [=, &current_other_parcel, &current_sale_parcel, &restrict_script_teleport](
                                       Vector3 position)
            -> std::shared_ptr<const ScriptParcel> {
            if (restrict_script_teleport && position.y >= 156 && position.y < 160) {
                if (position.x >= 160 && position.x < 168)
                    return west_landing;
                if (position.x >= 172 && position.x < 180)
                    return restricted_landing;
                if (position.x >= 180 && position.x < 192)
                    return east_landing;
            }
            if (position.x >= 199 && position.x < 200 && position.y >= 100 && position.y < 101)
                return teleport_parcel;
            if (position.x >= 167 && position.x < 169 && position.y >= 156 && position.y < 158)
                return silent_parcel;
            if (position.x >= 183 && position.x < 185 && position.y >= 156 && position.y < 158)
                return combat_parcel;
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
        policies.save_appearance = [](Uuid, const AvatarAppearance&) { return true; };
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
            if (id == Uuid("c5f90cb1-d285-d478-39f9-a1f6281f9ac9"))
                return std::nullopt;
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
            "{'type':'daycycle','name':'Script fixture',"
            "'frames':{'Sky':{'type':'sky','moon_rotation':'<0,0.6,0,0.8>'}},"
            "'tracks':[[],[{'key_keyframe':r0,'key_name':'Sky'}],"
            "[],[{'key_keyframe':r0,'key_name':'Sky'}],[{'key_keyframe':r0}]]}";
        script_day_cycle->data = std::make_shared<std::vector<std::uint8_t>>(
            day_cycle_data.begin(), day_cycle_data.end());
        policies.script_ossl_threat_level = "None";
        policies.script_ossl_rules["osTeleportAgent"].owners = "true";
        policies.script_ossl_rules["osForceOtherSit"].owners = "true";
        policies.script_ossl_rules["osGetAgentIP"].owners = "true";
        policies.script_ossl_rules["osGetScriptEngineName"].owners = "true";
        policies.script_grid_name = "Fixture Grid";
        policies.script_grid_nick = "fixture";
        for (const auto* function : {"osGetGridLoginURI", "osGetGridHomeURI", "osGetGridGatekeeperURI",
                                     "osGetGridCustom"})
            policies.script_ossl_rules[function].owners = "true";
        policies.script_grid_info = [](script::LslGridQuery query, std::string_view key) -> std::string {
            using Query = script::LslGridQuery;
            switch (query) {
            case Query::Login: return "https://login.example";
            case Query::Home: return "https://home.example";
            case Query::Gatekeeper: return "https://gate.example";
            case Query::Custom: return std::string(key);
            }
            return {};
        };
        policies.script_ossl_rules["osListenRegex"].owners = "true";
        policies.script_ossl_rules["osAvatarPlayAnimation"].owners = "true";
        policies.script_ossl_rules["osAvatarStopAnimation"].owners = "true";
        policies.script_ossl_rules["osSetRegionWaterHeight"].owners = "true";
        policies.script_ossl_rules["osSetHealRate"].owners = "true";
        policies.script_ossl_rules["osSetRegionSunSettings"].owners = "true";
        policies.script_ossl_rules["osReplaceAgentEnvironment"].owners = blocked_visitor.str();
        const auto duplicate_script_asset = std::make_shared<AssetBase>(
            Uuid::random(), "Duplication script", static_cast<std::int8_t>(AssetType::LSLText), "");
        const std::string duplicate_script_source = R"LSL(
            integer inventory_value;
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
                        if (llGetObjectName() == "Taken fixture")
                            llListen(7196, "", NULL_KEY, "save");
                        llSetText("fresh", <1,1,1>, 1);
                    }
                }
                listen(integer channel, string name, key speaker, string message) {
                    inventory_value = 42;
                    llSetText("checkpoint ready", <1,1,1>, 1);
                }
                on_rez(integer parameter) {
                    if (llGetObjectName() == "Taken fixture")
                        llSetText("Inventory rez " + (string)inventory_value, <1,1,1>, 1);
                    else
                        llSetText("unexpected on_rez", <1,1,1>, 1);
                }
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
        const auto attachment_physics_script = std::make_shared<AssetBase>(
            Uuid::random(), "Attachment physics", static_cast<std::int8_t>(AssetType::LSLText), "");
        const std::string attachment_physics_source = R"LSL(
            integer owner_changed;
            integer link_changes;
            default {
                state_entry() {
                    if (llGetObjectName() == "Named remote teleport") {
                        osTeleportOwner("Remote Script Region", <280, 35, 42>, <0, 1, 0>);
                        return;
                    }
                    if (llGetObjectName() == "Grid remote teleport") {
                        osTeleportAgent(llGetOwner(), 5, 3, <25, 36, 43>, <0, 1, 0>);
                        return;
                    }
                    if (llGetObjectName() == "Position remote teleport") {
                        osTeleportAgent(llGetOwner(), <1306, 805, 44>, <0, 1, 0>);
                        return;
                    }
                    if (llGetObjectName() == "LSL global teleport" || llGetObjectName() == "LSL landmark teleport") {
                        llRequestPermissions(llGetOwner(), PERMISSION_TELEPORT);
                        return;
                    }
                    if (llGetObjectName() == "Script attach from ground") {
                        llAttachToAvatar(ATTACH_LHAND);
                        llRequestPermissions(llGetOwner(), PERMISSION_ATTACH);
                        return;
                    }
                    if (llGetObjectName() == "Temporary script attach") {
                        llRequestPermissions((key)llGetObjectDesc(), PERMISSION_ATTACH);
                        return;
                    }
                    if (llGetObjectName() == "Script link fixture") {
                        llBreakAllLinks();
                        llRequestPermissions(llGetOwner(), PERMISSION_CHANGE_LINKS);
                        return;
                    }
                    if (llGetObjectName() == "Empty link operations") {
                        llRequestPermissions(llGetOwner(), PERMISSION_CHANGE_LINKS);
                        return;
                    }
                    llApplyImpulse(<35, 0, 0>, FALSE);
                    llSetBuoyancy(2);
                    llListen(7196, "", llGetOwner(), "");
                    key wearer = llGetOwner();
                    if (osGetHealRate(wearer) != 0.5)
                        return;
                    osSetHealRate(wearer, -5);
                    if (osGetHealRate(wearer) != 0.0)
                        return;
                    osSetHealRate(wearer, 101);
                    if (osGetHealRate(wearer) != 100.0)
                        return;
                    osSetHealRate(wearer, 0.5);
                    if (!llIsLinkGLTFMaterial(LINK_THIS, 0) || llIsLinkGLTFMaterial(LINK_ROOT, 0) ||
                        llIsLinkGLTFMaterial(LINK_THIS, ALL_SIDES) || llIsLinkGLTFMaterial(LINK_THIS, 1))
                        return;
                    if (llGetRenderMaterial(ALL_SIDES) != "7d55d860-e588-45d5-9631-7633cde238ce" ||
                        llGetRenderMaterial(2) != "Named glTF material" || llGetRenderMaterial(1) != NULL_KEY)
                        return;
                    llSetRenderMaterial("Named glTF material", 1);
                    if (llGetRenderMaterial(1) != "Named glTF material")
                        return;
                    llSetRenderMaterial(NULL_KEY, 1);
                    if (llGetRenderMaterial(1) != NULL_KEY || llGetRenderMaterial(2) != "Named glTF material")
                        return;
                    llSetRenderMaterial("Named glTF material", ALL_SIDES);
                    integer face;
                    for (face = 0; face < llGetNumberOfSides(); ++face) {
                        if (llGetRenderMaterial(face) != "Named glTF material")
                            return;
                    }
                    llSetRenderMaterial(NULL_KEY, ALL_SIDES);
                    for (face = 0; face < llGetNumberOfSides(); ++face) {
                        if (llGetRenderMaterial(face) != NULL_KEY || llIsLinkGLTFMaterial(LINK_THIS, face))
                            return;
                    }
                    if (llWind(<32, -32, 200>) != <3, 0, 0>)
                        return;
                    llSetDamage(12.5);
                    llLoadURL(llGetOwner(), "Fixture URL", "https://example.invalid/fixture");
                    llMapDestination("Destination fixture", <64, 65, 30>, <1, 2, 3>);
                    llMapBeacon("Beacon fixture", <96, 97, 40>, [BEACON_MAP, 1, 0]);
                }
                changed(integer flags) {
                    if ((flags & CHANGED_LINK) && llGetObjectName() == "Empty link operations") {
                        ++link_changes;
                        llSetObjectDesc((string)link_changes);
                    }
                    if (flags & CHANGED_OWNER)
                        owner_changed = TRUE;
                    if (flags & CHANGED_RENDER_MATERIAL)
                        llSetObjectDesc("Render material change received");
                }
                listen(integer channel, string name, key id, string message) {
                    if (message == "hover") {
                        llSetRenderMaterial("7d55d860-e588-45d5-9631-7633cde238ce", 0);
                        llSetRenderMaterial("Named glTF material", 1);
                        if (!llIsLinkGLTFMaterial(LINK_THIS, 0) || llGetRenderMaterial(0) != NULL_KEY ||
                            llGetRenderMaterial(1) != "Named glTF material")
                            return;
                        llSetBuoyancy(0);
                        llSetHoverHeight(12, FALSE, 0.25);
                        llDialog(llGetOwner(), "Stop hovering?", ["stop", 42], channel);
                    } else if (message == "stop") {
                        llStopHover();
                        if (llGetHealth(llGetOwner()) == 100.0)
                            llTextBox(llGetOwner(), "Enter a movement command", channel);
                    } else if (message == "move") {
                        llMoveToTarget(<150, 155, 36>, 0.5);
                    } else if (message == "teleport") {
                        osLocalTeleportAgent(llGetOwner(), <154, 155, 36>, <0, 3, 0>, <0, 2, 1>,
                                             OS_LTPAG_USEVEL | OS_LTPAG_USELOOKAT | OS_LTPAG_FORCEFLY);
                    } else if (message == "viewer teleport") {
                        osTeleportAgent(llGetOwner(), <154, 156, 36>, <1, 0, 0>);
                    } else if (message == "restricted teleport") {
                        osLocalTeleportAgent(llGetOwner(), <175, 158, 36>, ZERO_VECTOR, <0, 1, 0>,
                                             OS_LTPAG_USEVEL | OS_LTPAG_USELOOKAT);
                    } else if (message == "continue") {
                        llMoveToTarget(<166, 155, 36>, 2);
                    } else if (message == "cancel") {
                        llStopMoveToTarget();
                        llSetObjectDesc("Movement cancelled");
                    } else if (message == "walk") {
                        llMoveToTarget(<156, 157, 0>, 0.5);
                    } else if (message == "seated move") {
                        llMoveToTarget(<160, 157, 0>, 0.5);
                        llSetObjectDesc("Seated movement attempted");
                    } else if (message == "rotate attachment") {
                        llRotLookAt(<0, 0, 1, 0>, 1, 1);
                        llSetObjectDesc("Attachment rotated");
                    } else if (message == "home") {
                        llTeleportAgentHome(id);
                    } else if (message == "eject") {
                        llEjectFromLand(id);
                    } else if (message == "message") {
                        llInstantMessage(id, "Scripted object message");
                    } else if (message == "forward message") {
                        llInstantMessage("8840d11b-4f83-4c7c-b11d-7e41ef0789ad", "Forwarded object message");
                    } else if (message == "give inventory") {
                        llGiveInventory(llGetLinkKey(LINK_ROOT), "Named glTF material");
                        llGiveInventory(id, "Named glTF material");
                    } else if (message == "give no-copy inventory") {
                        llGiveInventory(id, "No-copy landmark");
                    } else if (message == "give cross-owner inventory") {
                        llGiveInventory("653bc91d-c05a-4a90-980c-05f26d8c8488", "Named glTF material");
                    } else if (message == "give nontransferable inventory") {
                        llGiveInventory("653bc91d-c05a-4a90-980c-05f26d8c8488", "Nontransferable landmark");
                        llSetObjectDesc("Nontransferable gift attempted");
                    } else if (message == "give object inventory list") {
                        llGiveInventoryList("653bc91d-c05a-4a90-980c-05f26d8c8488", "Ignored category",
                            ["8d06d95d-c6a9-412a-8f8a-bab07e9f7650", "No-copy landmark", "Nontransferable landmark"]);
                        llSetObjectDesc("Object inventory list processed");
                    } else if (message == "give remote inventory") {
                        llGiveInventory("8840d11b-4f83-4c7c-b11d-7e41ef0789ad", "Named glTF material");
                    } else if (message == "give inventory list") {
                        llGiveInventoryList(id, "Scripted gift folder", ["Named glTF material", "Missing item"]);
                    } else if (message == "detach without grant") {
                        llDetachFromAvatar();
                        llSetObjectDesc("Detach without grant attempted");
                    } else if (message == "detach with grant") {
                        llRequestPermissions(id, PERMISSION_ATTACH);
                    }
                }
                run_time_permissions(integer permissions) {
                    if (llGetObjectName() == "LSL global teleport") {
                        if (permissions & PERMISSION_TELEPORT)
                            llTeleportAgentGlobalCoords(llGetOwner(), <1287, 778, 0>, <27, 38, 45>, <0, 1, 0>);
                        return;
                    }
                    if (llGetObjectName() == "LSL landmark teleport") {
                        if (permissions & PERMISSION_TELEPORT)
                            llTeleportAgent(llGetOwner(), "Script landmark", ZERO_VECTOR, <0, 1, 0>);
                        return;
                    }
                    if (permissions & PERMISSION_CHANGE_LINKS) {
                        if (llGetObjectName() == "Empty link operations") {
                            llCreateLink(llGetKey(), TRUE);
                            llBreakLink(LINK_ROOT);
                            llBreakAllLinks();
                            return;
                        }
                        key target = (key)llGetObjectDesc();
                        key original_child = llGetLinkKey(2);
                        key target_child = llGetObjectLinkKey(target, 2);
                        llCreateLink(target, TRUE);
                        if (llGetLinkKey(2) != target || llGetLinkKey(3) != target_child ||
                            llGetLinkKey(4) != original_child)
                            return;
                        llBreakLink(3);
                        if (llGetLinkKey(3) != original_child)
                            return;
                        llBreakLink(LINK_ROOT);
                        if (llGetNumberOfPrims() != 1 || llGetObjectLinkKey(target, LINK_ROOT) != original_child)
                            return;
                        llCreateLink(target, FALSE);
                        if (llGetLinkKey(LINK_ROOT) != original_child)
                            return;
                        llBreakLink(LINK_THIS);
                        if (llGetNumberOfPrims() != 1)
                            return;
                        llCreateLink(target, FALSE);
                        llBreakAllLinks();
                        if (llGetNumberOfPrims() == 1)
                            llSetObjectName("Script link operations finished");
                    }
                    if (permissions & PERMISSION_ATTACH) {
                        if (llGetObjectName() == "Script attach from ground") {
                            llAttachToAvatar(ATTACH_LHAND);
                            if (llGetAttached() == ATTACH_LHAND)
                                llSetObjectName("Script attached immediately");
                        } else if (llGetObjectName() == "Temporary script attach") {
                            llAttachToAvatarTemp(ATTACH_LHAND);
                            if (llGetAttached() == 0 || llGetOwner() == (key)llGetObjectDesc())
                                llSetObjectName("Temporary attach attempted");
                        } else {
                            llDetachFromAvatar();
                        }
                    }
                }
                attach(key avatar) {
                    if (avatar == NULL_KEY)
                        llOwnerSay("Scripted attachment detached");
                    else if (avatar == llGetOwner() && llGetAttached() == ATTACH_LHAND &&
                             (llGetObjectName() != "Temporary attach attempted" || owner_changed))
                        llSetObjectDesc("Script attach event");
                }
            }
        )LSL";
        const auto attachment_compiled = script::compile_lsl(attachment_physics_source);
        if (!attachment_compiled)
            throw std::runtime_error("Attachment fixture failed to compile: " +
                                     attachment_compiled.diagnostics.front().message);
        attachment_physics_script->data = std::make_shared<std::vector<std::uint8_t>>(
            attachment_physics_source.begin(), attachment_physics_source.end());
        const auto land_collision_script = std::make_shared<AssetBase>(
            Uuid::random(), "Land collision", static_cast<std::int8_t>(AssetType::LSLText), "");
        const std::string land_collision_source = R"LSL(
            default {
                state_entry() { llPassCollisions(1); }
                land_collision_start(vector position) {
                    if (llFabs(position.x - 212) < 0.1 && llFabs(position.y - 213) < 0.1 && position.z > 22)
                        llSetObjectDesc("Land contact started");
                }
                land_collision(vector position) {
                    if (llGetObjectDesc() == "Land contact started")
                        llSetObjectDesc("Land contact active");
                }
                land_collision_end(vector position) {
                    if (llGetObjectDesc() == "Land contact active" && position.z > 25)
                        llSetObjectDesc("Land contact ended");
                }
            }
        )LSL";
        land_collision_script->data = std::make_shared<std::vector<std::uint8_t>>(
            land_collision_source.begin(), land_collision_source.end());
        const auto object_collision_script = std::make_shared<AssetBase>(
            Uuid::random(), "Object collision", static_cast<std::int8_t>(AssetType::LSLText), "");
        const std::string object_collision_source = R"LSL(
            default {
                state_entry() {
                    if (llGetObjectName() == "Collision platform") {
                        llCollisionFilter("", NULL_KEY, 0);
                        llSetObjectDesc("Platform filter installed");
                    } else {
                        llCollisionFilter("cOlLiSiOn pLaTfOrM", "invalid key", -1);
                    }
                }

                collision_start(integer count) {
                    if (llGetObjectName() == "Collision platform")
                        llSetObjectDesc("Unexpected platform collision");
                    else if (count == 1 && llDetectedName(0) == "Collision platform" && llDetectedType(0) == 12)
                        llSetObjectDesc("Object contact started");
                }

                collision(integer count) {
                    if (count == 1 && llGetObjectDesc() == "Object contact started") {
                        llCollisionFilter("COLLISION PLATFORM", llDetectedKey(0), 1);
                        llSetObjectDesc("Object contact active");
                    }
                }

                collision_end(integer count) {
                    if (count == 1 && llDetectedName(0) == "Collision platform" &&
                        llGetObjectDesc() == "Object contact active")
                        llSetObjectDesc("Object contact ended");
                }
            }
        )LSL";
        object_collision_script->data = std::make_shared<std::vector<std::uint8_t>>(
            object_collision_source.begin(), object_collision_source.end());
        const auto avatar_collision_script = std::make_shared<AssetBase>(
            Uuid::random(), "Avatar collision", static_cast<std::int8_t>(AssetType::LSLText), "");
        const std::string avatar_collision_source = R"LSL(
            default {
                state_entry() {
                    string name = llGetObjectName();
                    if (name == "Wearer collision attachment") {
                        llCollisionFilter("wearer collision platform", NULL_KEY, 1);
                        llSetObjectDesc("Waiting for local teleport");
                    } else if (name == "Custom impact")
                        llCollisionSound("be7295c0-a158-11e1-b3dd-0803203c9a66", 0.45);
                    else if (name == "OSSL custom impact")
                        osCollisionSound("be7295c0-a158-11e1-b3dd-0803203c9a66", 0.55);
                    else if (name == "Scaled impact")
                        osCollisionSound("", 0.35);
                    else if (name == "Restored default impact") {
                        osCollisionSound("", 0.35);
                        osCollisionSound("", 1.0);
                    } else if (name == "Disabled impact")
                        osCollisionSound("", 0.0);
                    else
                        llCollisionFilter("collision walker", llGetOwner(), 1);
                }

                collision_start(integer count) {
                    if (count == 1 && llDetectedName(0) == "Wearer collision platform" &&
                        llDetectedLinkNumber(0) == 0)
                        llSetObjectDesc("Wearer contact started");
                    else if (count == 1 && llDetectedName(0) == "Collision Walker" &&
                        llDetectedKey(0) == llGetOwner() && llDetectedOwner(0) == llGetOwner() &&
                        (llDetectedType(0) & 1))
                        llSetObjectDesc("Avatar contact started");
                }

                collision(integer count) {
                    if (count == 1 && llGetObjectDesc() == "Wearer contact started")
                        llSetObjectDesc("Wearer contact active");
                    else if (count == 1 && llGetObjectDesc() == "Avatar contact started")
                        llSetObjectDesc("Avatar contact active");
                }

                collision_end(integer count) {
                    if (count == 1 && llGetObjectDesc() == "Wearer contact active")
                        llSetObjectDesc("Wearer contact ended");
                    else if (count == 1 && llDetectedKey(0) == llGetOwner() &&
                             llGetObjectDesc() == "Avatar contact active")
                        llSetObjectDesc("Avatar contact ended");
                }

                changed(integer mask) {
                    if ((mask & CHANGED_TELEPORT) && llGetObjectDesc() == "Waiting for local teleport")
                        llSetObjectDesc("Received local teleport");
                }

                land_collision_start(vector position) {
                    if (llGetObjectName() == "Wearer collision attachment" &&
                        llFabs(position.x - 180.0) < 0.2)
                        llSetObjectDesc("Wearer land contact started");
                }

                land_collision(vector position) {
                    if (llGetObjectDesc() == "Wearer land contact started")
                        llSetObjectDesc("Wearer land contact active");
                }

                land_collision_end(vector position) {
                    if (llGetObjectDesc() == "Wearer land contact active")
                        llSetObjectDesc("Wearer land contact ended");
                }
            }
        )LSL";
        avatar_collision_script->data = std::make_shared<std::vector<std::uint8_t>>(
            avatar_collision_source.begin(), avatar_collision_source.end());
        const auto volume_detect_script = std::make_shared<AssetBase>(
            Uuid::random(), "Volume detect", static_cast<std::int8_t>(AssetType::LSLText), "");
        const std::string volume_detect_source = R"LSL(
            default {
                state_entry() {
                    if (llGetLinkNumber() > 1)
                        osVolumeDetect(-1);
                    else
                        llVolumeDetect(-1);
                }

                collision_start(integer count) {
                    if (count == 1 && llDetectedName(0) == "Falling through" && llGetObjectDesc() == "")
                        llSetObjectDesc("Volume contact started");
                }

                collision(integer count) {
                    if (llGetObjectDesc() == "Volume contact started")
                        llSetObjectDesc("Unexpected ongoing volume collision");
                }

                collision_end(integer count) {
                    if (count == 1 && llDetectedName(0) == "Falling through" &&
                        llGetObjectDesc() == "Volume contact started") {
                        llSetObjectDesc("Volume contact ended");
                        llVolumeDetect(0);
                    }
                }
            }
        )LSL";
        volume_detect_script->data = std::make_shared<std::vector<std::uint8_t>>(
            volume_detect_source.begin(), volume_detect_source.end());
        const auto mass_query_script = std::make_shared<AssetBase>(
            Uuid::random(), "Prim mass", static_cast<std::int8_t>(AssetType::LSLText), "");
        const std::string mass_query_source = R"LSL(
            integer override_phase;

            default {
                state_entry() {
                    if (llGetObjectName() == "llDie fixture")
                        llDie();
                    llSetTimerEvent(0.1);
                }

                timer() {
                    if (llGetObjectName() == "Water repel fixture") {
                        llGroundRepel(30.0 - 2.0 * llWater(ZERO_VECTOR), TRUE, 0.2);
                        llSetTimerEvent(0.0);
                        return;
                    }
                    if (llGetObjectName() == "Ground repel fixture") {
                        llGroundRepel(3.0, FALSE, 0.2);
                        llSetTimerEvent(0.0);
                        return;
                    }
                    if (override_phase != 0) {
                        rotation current_rotation = llGetLocalRot();
                        list custom = osGetInertiaData();
                        float mass = llList2Float(custom, 0);
                        vector center_local = llList2Vector(custom, 1);
                        vector axes = llList2Vector(custom, 2);
                        if (override_phase == 1 && llFabs(mass - 12.0) < 0.01 &&
                            llVecDist(center_local, <0.5, 0.0, 0.0>) < 0.01 &&
                            llVecDist(axes, <0.4, 0.4, 0.4>) < 0.01) {
                            osSetInertiaAsBox(15.0, <2.0, 3.0, 4.0>, ZERO_VECTOR,
                                              <0.0, 0.0, 0.382683432, 0.923879533>);
                            list immediate = osGetInertiaData();
                            if (llFabs(llList2Float(immediate, 0) - 15.0) > 0.01 ||
                                llVecDist(llList2Vector(immediate, 3), <0.208333, 0.0, 0.0>) > 0.01) {
                                llSetObjectDesc("Immediate rotated inertia readback failed");
                                llSetTimerEvent(0.0);
                                return;
                            }
                            override_phase = 2;
                        } else if (override_phase == 2 && llFabs(mass - 15.0) < 0.01 &&
                                   llVecDist(axes, <1.875, 1.875, 1.083333>) < 0.01) {
                            osSetInertiaAsCylinder(18.0, 1.0, 2.0, ZERO_VECTOR, ZERO_ROTATION);
                            override_phase = 3;
                        } else if (override_phase == 3 && llFabs(mass - 18.0) < 0.01 &&
                                   llVecDist(axes, <0.583333, 0.583333, 0.5>) < 0.01) {
                            osSetInertia(20.0, ZERO_VECTOR, <0.5, 0.6, 0.7>, ZERO_ROTATION);
                            override_phase = 4;
                        } else if (override_phase == 4 && llFabs(mass - 20.0) < 0.01 &&
                                   llVecDist(axes, <0.5, 0.6, 0.7>) < 0.01) {
                            osClearInertia();
                            override_phase = 5;
                        } else if (override_phase == 5 && llFabs(mass - 30.0) < 0.01 &&
                                   llVecDist(axes, <0.388889, 1.055556, 1.277778>) < 0.01) {
                            llSetPhysicsMaterial(DENSITY, 0.0, 0.0, 0.0, 4000.0);
                            override_phase = 6;
                        } else if (override_phase == 6) {
                            if (llFabs(llList2Float(llGetPhysicsMaterial(), 3) - 4000.0) > 0.01 ||
                                llFabs(mass - 30.0) > 0.01) {
                                llSetObjectDesc("Density edit changed live mass or failed to update density");
                                llSetTimerEvent(0.0);
                                return;
                            }
                            llSetScale(<2.0, 1.0, 1.0>);
                            override_phase = 7;
                        } else if (override_phase == 7 && llFabs(mass - 90.0) < 0.01) {
                            llSetLocalRot(<0.0, 0.0, 1.0, 1.0>);
                            llSetObjectDesc("Waiting for local rotation");
                            override_phase = 8;
                        } else if (override_phase == 8 && llFabs(current_rotation.z - 0.707107) < 0.001) {
                            llSetRot(<0.0, 0.707107, 0.0, 0.707107>);
                            rotation applied = llGetLocalRot();
                            if (llFabs(applied.y - 0.707107) > 0.001) {
                                llSetObjectDesc("Script rotation readback failed");
                                llSetTimerEvent(0.0);
                                return;
                            }
                            llSetObjectPermMask(MASK_NEXT, 0);
                            if (llGetObjectPermMask(MASK_NEXT) != PERM_TRANSFER) {
                                llSetObjectDesc("Administrator object permission edit failed");
                                llSetTimerEvent(0.0);
                                return;
                            }
                            llSetObjectPermMask(MASK_BASE, PERM_COPY);
                            llSetObjectPermMask(MASK_GROUP, PERM_MOVE);
                            if (llGetObjectPermMask(MASK_BASE) != (PERM_MOVE | PERM_TRANSFER)) {
                                llSetObjectDesc("Object permission selector masking failed");
                                llSetTimerEvent(0.0);
                                return;
                            }
                            llSetInventoryPermMask("Fixture notecard", MASK_NEXT, 0);
                            if (llGetInventoryPermMask("Fixture notecard", MASK_NEXT) != PERM_TRANSFER) {
                                llSetObjectDesc("Administrator inventory permission edit failed");
                                llSetTimerEvent(0.0);
                                return;
                            }
                            llSetInventoryPermMask("Fixture notecard", MASK_BASE, PERM_COPY);
                            llSetInventoryPermMask("Fixture notecard", MASK_GROUP, PERM_MOVE);
                            if (llGetInventoryPermMask("Fixture notecard", MASK_BASE) != (PERM_MOVE | PERM_TRANSFER)) {
                                llSetObjectDesc("Inventory permission selector masking failed");
                                llSetTimerEvent(0.0);
                                return;
                            }
                            llRemoveInventory("fixture notecard");
                            if (llGetInventoryType("Fixture notecard") != INVENTORY_NOTECARD) {
                                llSetObjectDesc("Inventory name matching failed");
                                llSetTimerEvent(0.0);
                                return;
                            }
                            llRemoveInventory("Fixture notecard");
                            if (llGetInventoryType("Fixture notecard") != -1) {
                                llSetObjectDesc("Inventory removal was not immediately visible");
                                llSetTimerEvent(0.0);
                                return;
                            }
                            llSetObjectDesc("Mass and inertia verified");
                            llRemoveInventory(llGetScriptName());
                            llSetObjectDesc("Self-removal allowed the script to continue");
                            llSetTimerEvent(0.0);
                        }
                        return;
                    }
                    if (llEdgeOfWorld(<128, 128, 0>, <1, 0, 0>) != 0 ||
                        llEdgeOfWorld(<128, 128, 0>, <1, 1, 0>) != 0 ||
                        llEdgeOfWorld(<128, 128, 0>, <-1, 0, 0>) != 1 ||
                        llEdgeOfWorld(<128, 128, 0>, <0, 0, 1>) != 1) {
                        llSetObjectDesc("Grid edge query failed");
                        llSetTimerEvent(0.0);
                        return;
                    }
                    if (llGetMinScaleFactor() <= 0.0 || llGetMinScaleFactor() >= 1.0 ||
                        llGetMaxScaleFactor() <= 1.0 || llScaleByFactor(1000000) != 0 ||
                        llScaleByFactor(1) != !llGetStatus(STATUS_PHYSICS)) {
                        llSetObjectDesc("Linkset scale limits failed");
                        llSetTimerEvent(0.0);
                        return;
                    }
                    if (!llGetStatus(STATUS_PHYSICS)) {
                        llSetObjectPermMask(MASK_BASE, PERM_COPY);
                        if (llGetObjectPermMask(MASK_BASE) != PERM_ALL) {
                            llSetObjectDesc("Non-administrator object permission edit was accepted");
                            llSetTimerEvent(0.0);
                            return;
                        }
                        llSetInventoryPermMask(llGetScriptName(), MASK_BASE, PERM_COPY);
                        if (llGetInventoryPermMask(llGetScriptName(), MASK_BASE) != PERM_ALL) {
                            llSetObjectDesc("Non-administrator inventory permission edit was accepted");
                            llSetTimerEvent(0.0);
                            return;
                        }
                    }
                    vector center = llGetCenterOfMass();
                    vector expected_center = <218.0, 219.0, 32.0>;
                    if (llGetLinkNumber() == 1)
                        expected_center = <217.333333, 218.666667, 32.0>;
                    list inertia = osGetInertiaData();
                    vector local_center = llList2Vector(inertia, 1);
                    vector diagonal = llList2Vector(inertia, 2);
                    vector products = llList2Vector(inertia, 3);
                    vector expected_diagonal = ZERO_VECTOR;
                    vector expected_products = ZERO_VECTOR;
                    if (llGetStatus(STATUS_PHYSICS)) {
                        expected_diagonal = <0.388889, 1.055556, 1.277778>;
                        expected_products = <-0.444444, 0.0, 0.0>;
                    }
                    if (llFabs(llGetMass() - 30.0) < 0.01 &&
                        llFabs(llGetMassMKS() - 3000.0) < 0.1 &&
                        llFabs(llGetObjectMass(llGetLinkKey(2)) - 30.0) < 0.01 &&
                        llVecDist(center, expected_center) < 0.02 &&
                        llGetListLength(inertia) == 4 && llFabs(llList2Float(inertia, 0) - 30.0) < 0.01 &&
                        llVecDist(local_center, <1.333333, 0.666667, 0.0>) < 0.01 &&
                        llVecDist(diagonal, expected_diagonal) < 0.01 &&
                        llVecDist(products, expected_products) < 0.01) {
                        if (llGetStatus(STATUS_PHYSICS) && llGetLinkNumber() == 2) {
                            llSetStatus(STATUS_ROTATE_X | STATUS_ROTATE_Z, FALSE);
                            if (llGetStatus(STATUS_ROTATE_X) || !llGetStatus(STATUS_ROTATE_Y) ||
                                llGetStatus(STATUS_ROTATE_Z))
                                return;
                            llSetStatus(STATUS_ROTATE_X | STATUS_ROTATE_Z, TRUE);
                            if (!llGetStatus(STATUS_ROTATE_X) || !llGetStatus(STATUS_ROTATE_Z))
                                return;
                            osSetInertiaAsSphere(12.0, 1.0, <0.5, 0.0, 0.0>);
                            list immediate = osGetInertiaData();
                            if (llFabs(llList2Float(immediate, 0) - 12.0) > 0.01 ||
                                llVecDist(llList2Vector(immediate, 1), <0.5, 0.0, 0.0>) > 0.01) {
                                llSetObjectDesc("Immediate sphere inertia readback failed");
                                llSetTimerEvent(0.0);
                                return;
                            }
                            override_phase = 1;
                        } else {
                            if (llGetStatus(STATUS_PHYSICS))
                                llSetRot(<0.707107, 0.0, 0.0, 0.707107>);
                            llSetObjectDesc("Mass and inertia verified");
                            llSetTimerEvent(0.0);
                        }
                    }
                }
            }
        )LSL";
        mass_query_script->data = std::make_shared<std::vector<std::uint8_t>>(
            mass_query_source.begin(), mass_query_source.end());
        const auto terrain_brush_script = std::make_shared<AssetBase>(Uuid::random(), "Terrain brush", 10, "");
        const std::string_view terrain_brush_source = R"lsl(
            default {
                state_entry() {
                    string mode = llGetObjectName();
                    if (mode == "Flatten high") {
                        llModifyLand(LAND_LEVEL, LAND_SMALL_BRUSH);
                    } else {
                        if (mode == "Invalid first") {
                            llModifyLand(255, LAND_SMALL_BRUSH);
                        }
                        llModifyLand(LAND_RAISE, LAND_SMALL_BRUSH);
                        llModifyLand(LAND_RAISE, LAND_SMALL_BRUSH);
                    }
                    llSetObjectName("Terrain brush finished");
                }
            }
        )lsl";
        terrain_brush_script->data = std::make_shared<std::vector<std::uint8_t>>(
            terrain_brush_source.begin(), terrain_brush_source.end());
        const auto remote_script_asset = std::make_shared<AssetBase>(Uuid::random(), "Remote script fixture", 10, "");
        const std::string_view remote_script_source = R"lsl(
            default {
                state_entry() {
                    if (llGetScriptName() == "Payload") {
                        llSetObjectName((string)llGetStartParameter());
                        return;
                    }
                    string destination = llGetObjectDesc();
                    if (destination == "") {
                        llSetRemoteScriptAccessPin(-123);
                        return;
                    }
                    if (llGetObjectName() == "Transfer permission fixture") {
                        llRemoteLoadScriptPin(destination, "Payload", -123, FALSE, 42);
                        llSetObjectName("Transfer attempt finished");
                        return;
                    }
                    llRemoteLoadScriptPin(destination, "Payload", 0, TRUE, 17);
                    llSetObjectName("Wrong PIN returned");
                    llSleep(0.25);
                    llRemoteLoadScriptPin(destination, "Payload", -123, TRUE, 81);
                }
            }
        )lsl";
        remote_script_asset->data = std::make_shared<std::vector<std::uint8_t>>(
            remote_script_source.begin(), remote_script_source.end());
        const auto sensor_asset = std::make_shared<AssetBase>(Uuid::random(), "Sensor fixture", 10, "");
        const std::string_view sensor_source = R"lsl(
            integer phase;
            integer repeats;
            integer timer_events;
            integer listener_messages;
            integer restored_events;
            default {
                state_entry() {
                    if (llGetObjectName() == "Managed speaker") {
                        if (osGetRegionSize() != <256, 256, 0> ||
                            osGetScriptEngineName() != "NativeLSL" ||
                            osGetGridName() != "Fixture Grid" || osGetGridNick() != "fixture" ||
                            osGetGridLoginURI() != "https://login.example" ||
                            osGetGridHomeURI() != "https://home.example" ||
                            osGetGridGatekeeperURI() != "https://gate.example" ||
                            osGetGridCustom("custom") != "custom" ||
                            osSHA256("fixture") != llSHA256String("fixture"))
                            return;
                        llRegionSay(7197, "ignore");
                        llRegionSay(7197, "matched 42");
                        llRegionSay(7198, "legacy filter");
                        llRegionSay(7199, "ignore");
                        llRegionSay(7199, "native 42");
                    } else if (llGetObjectName() == "Native regex listener") {
                        integer handle = osListenRegex(7199, "^Managed speaker$", NULL_KEY, "^native [0-9]+$",
                            OS_LISTEN_REGEX_NAME | OS_LISTEN_REGEX_MESSAGE);
                        if (handle > 0)
                            llSetObjectName("Native regex listener armed");
                    } else if (llGetObjectName() == "Avatar sensor fixture")
                        llSensor("", llGetOwner(), AGENT, 96, PI);
                    else if (llGetObjectName() == "Restored sensor fixture")
                        llSensorRepeat("Sensor target", NULL_KEY, PASSIVE, 10, 0.5, 0.5);
                    else if (llGetObjectName() != "Managed sensor fixture")
                        llSensor("Sensor target", NULL_KEY, PASSIVE, 10, 0.5);
                }
                sensor(integer count) {
                    if (llGetObjectName() == "Managed sensor fixture") {
                        if (count == 2 && llDetectedPos(0) == <202, 240, 30>) {
                            llSensorRemove();
                            llSetObjectName("Managed sensor imported");
                        }
                        return;
                    }
                    if (llGetObjectName() == "Avatar sensor fixture") {
                        if (count == 1 && llDetectedKey(0) == llGetOwner() &&
                            llDetectedOwner(0) == llGetOwner() && (llDetectedType(0) & AGENT))
                            llSetObjectName("Avatar sensor found owner");
                        return;
                    }
                    if (llGetObjectName() == "Restored sensor fixture" ||
                        llGetObjectName() == "Sensor checkpoint armed") {
                        ++repeats;
                        if (repeats == 1)
                            llSetObjectName("Sensor checkpoint armed");
                        else if (repeats == 2) {
                            llSensorRemove();
                            llSetObjectName("Sensor checkpoint resumed");
                        }
                        return;
                    }
                    if (phase == 0) {
                        if (count != 2 || llDetectedName(0) != "Sensor target" ||
                            llDetectedType(0) != PASSIVE || llDetectedLinkNumber(0) != 0 ||
                            llDetectedPos(0) != <202, 240, 30> || llDetectedPos(1) != <204, 240, 30>)
                            return;
                        phase = 1;
                        llSensor("Missing sensor target", NULL_KEY, PASSIVE, 10, PI);
                        return;
                    }
                    if (phase == 2) {
                        ++repeats;
                        if (repeats == 2) {
                            llSensorRemove();
                            llSetObjectName("Sensor repeat stopped");
                        } else if (repeats > 2) {
                            llSetObjectName("Sensor repeat leaked");
                        }
                    }
                }
                no_sensor() {
                    if (phase == 1) {
                        phase = 2;
                        llSensorRepeat("Sensor target", NULL_KEY, PASSIVE, 10, 0.5, 0.03);
                    }
                }
                timer() {
                    ++timer_events;
                    if (timer_events == 2) {
                        llSetTimerEvent(0);
                        if (llGetTime() >= 12.5 && restored_events == 2)
                            llSetObjectDesc("Managed timer repeated");
                    }
                }
                dataserver(key query, string data) {
                    if (query == "cfab1234-1234-4321-1234-123456789abc" && data == "queued answer")
                        restored_events = 1;
                }
                touch_start(integer count) {
                    if (restored_events == 1 && count == 1 &&
                        llDetectedKey(0) == "cfab1234-1234-4321-1234-123456789abc" &&
                        llDetectedName(0) == "Saved toucher" && llDetectedLinkNumber(0) == 3 &&
                        llDetectedPos(0) == <201, 240, 30> && llDetectedGrab(0) == <1, 2, 3>)
                        restored_events = 2;
                }
                listen(integer channel, string name, key sender, string message) {
                    if (channel == 7199) {
                        if (message == "native 42" && llGetObjectName() == "Native regex listener armed")
                            llSetObjectName("Native regex listener matched");
                        else
                            llSetObjectName("Unexpected native regex listener match");
                        return;
                    }
                    ++listener_messages;
                    if (listener_messages == 1 && channel == 7197 && message == "matched 42")
                        llSetObjectName("Managed regex listener matched");
                    else if (listener_messages == 2 && channel == 7198 && message == "legacy filter" &&
                             llGetObjectName() == "Managed regex listener matched")
                        llSetObjectName("Managed listeners matched");
                    else
                        llSetObjectName("Unexpected managed listener match");
                }
            }
        )lsl";
        const auto sensor_compiled = script::compile_lsl(sensor_source);
        if (!sensor_compiled)
            throw std::runtime_error("Sensor fixture failed to compile: " + sensor_compiled.diagnostics.front().message);
        sensor_asset->data = std::make_shared<std::vector<std::uint8_t>>(sensor_source.begin(), sensor_source.end());
        const auto idle_asset = std::make_shared<AssetBase>(Uuid::random(), "Idle snapshot fixture", 10, "");
        constexpr std::string_view idle_source = R"lsl(
            integer counter = 1;
            default {
                state_entry() { llSetObjectName("Fresh startup"); }
            }
            state zed {
                dataserver(key query, string data) {
                    llSetObjectDesc((string)llGetPermissionsKey() + ":" + (string)llGetPermissions());
                    llSetObjectName(data + (string)counter);
                }
            }
        )lsl";
        idle_asset->data = std::make_shared<std::vector<std::uint8_t>>(idle_source.begin(), idle_source.end());
        const auto physics_mesh_asset = make_physics_mesh_asset();
        const auto convex_mesh_asset = make_physics_mesh_asset(true);
        const auto gifted_asset = std::make_shared<AssetBase>(
            Uuid("e23a5bcd-283b-44b2-aa4c-97ca53b88410"), "Gifted asset", 57, "");
        gifted_asset->data = std::make_shared<std::vector<std::uint8_t>>(
            std::initializer_list<std::uint8_t>{1, 3, 5, 7});
        policies.load_asset = [covenant, unsit_script, force_sit_script, script_day_cycle, duplicate_script_asset,
                               script_landmark,
                               attachment_script, attachment_physics_script, land_collision_script,
                               object_collision_script, avatar_collision_script, volume_detect_script, mass_query_script,
                               empty_legacy_material, mesh_script, physics_mesh_asset, convex_mesh_asset, gifted_asset,
                               terrain_brush_script, remote_script_asset, sensor_asset, idle_asset, &uploaded_asset](Uuid id)
                -> std::shared_ptr<AssetBase> {
            if (id == script_landmark->full_id())
                return script_landmark;
            if (id == sensor_asset->full_id())
                return sensor_asset;
            if (id == idle_asset->full_id())
                return idle_asset;
            if (id == remote_script_asset->full_id())
                return remote_script_asset;
            if (id == terrain_brush_script->full_id())
                return terrain_brush_script;
            if (id == gifted_asset->full_id())
                return gifted_asset;
            if (id == physics_mesh_asset_id)
                return physics_mesh_asset;
            if (id == convex_mesh_asset_id)
                return convex_mesh_asset;
            if (id == attachment_script->full_id())
                return attachment_script;
            if (id == attachment_physics_script->full_id())
                return attachment_physics_script;
            if (id == land_collision_script->full_id())
                return land_collision_script;
            if (id == object_collision_script->full_id())
                return object_collision_script;
            if (id == avatar_collision_script->full_id())
                return avatar_collision_script;
            if (id == volume_detect_script->full_id())
                return volume_detect_script;
            if (id == mass_query_script->full_id())
                return mass_query_script;
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
            const std::array<std::string, 4> wind_rate{"wind", "base", "wind_update_rate", " +1 "};
            const std::array<std::string, 4> wind_set{"wind", "ConfigurableWind", "avgStrength", "3"};
            const std::array<std::string, 3> wind_get{"wind", "ConfigurableWind", "avgStrength"};
            if (!runtime.wind_console_command(wind_rate).empty() ||
                runtime.wind_console_command(wind_set) != "avgStrength set to 3" ||
                runtime.wind_console_command(wind_get) != "avgStrength : 3")
                throw std::runtime_error("Wind console parameters did not update the region");
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
            const auto check_terrain_brush = [&](Uuid owner, int x, std::string mode, float increase) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                constexpr int y = 245;
                const float before = runtime.world().terrain().get(x, y);
                const float elevation = mode == "Flatten high" ? 4050.f : before + 2.f;
                auto fixture = SceneObject::primitive(owner, {static_cast<float>(x), static_cast<float>(y), elevation});
                fixture.name = std::move(mode);
                const auto prim = runtime.world().add(std::move(fixture));
                if (!prim)
                    throw std::runtime_error("Could not rez the terrain brush fixture");

                auto item = std::make_shared<TaskInventoryItem>();
                item->item_id = Uuid::random();
                item->parent_id = item->parent_part_id = prim->id;
                item->asset_id = terrain_brush_script->full_id();
                item->owner_id = item->creator_id = owner;
                item->type = item->inventory_type = 10;
                item->script_running = true;
                item->name = "Terrain brush";
                runtime.restore_task_inventory(prim->id, {item});

                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (std::chrono::steady_clock::now() < deadline) {
                    runtime.poll();
                    const auto current = runtime.world().object(prim->id);
                    if (current && current->name == "Terrain brush finished") {
                        const float after = runtime.world().terrain().get(x, y);
                        runtime.world().remove(prim->id);
                        const float expected = elevation == 4050.f ? elevation : before + increase;
                        if (std::abs(after - expected) > .011f)
                            throw std::runtime_error("Script terrain brush produced an unexpected height");
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                throw std::runtime_error("Terrain brush script did not finish");
            };
            check_terrain_brush(Uuid::random(), 248, "Raise", 0.f);
            check_terrain_brush(return_parcel->owner, 241, "Raise", .25f);
            check_terrain_brush(return_parcel->owner, 248, "Invalid first", 0.f);
            check_terrain_brush(blocked_visitor, 252, "Flatten high", 0.f);
            runtime.revert_terrain();
            {
                const auto owner = Uuid::random();
                auto receiver = SceneObject::primitive(owner, {230, 245, 30});
                receiver.description.clear();
                if (!runtime.world().add(receiver))
                    throw std::runtime_error("Could not rez the remote script receiver");
                auto sender = SceneObject::primitive(owner, {232, 245, 30});
                sender.description = receiver.id.str();
                if (!runtime.world().add(sender))
                    throw std::runtime_error("Could not rez the remote script sender");

                const auto task = [&](const SceneObject& part, std::string name, bool running) {
                    auto item = std::make_shared<TaskInventoryItem>();
                    item->item_id = Uuid::random();
                    item->parent_id = item->parent_part_id = part.id;
                    item->asset_id = remote_script_asset->full_id();
                    item->owner_id = item->creator_id = part.owner;
                    item->type = item->inventory_type = 10;
                    item->base_permissions = bits(PermissionMask::Copy);
                    item->script_running = running;
                    item->name = std::move(name);
                    return item;
                };
                const auto original = task(receiver, "Payload", false);
                const auto payload = task(sender, "Payload", false);
                runtime.restore_task_inventory(receiver.id, {task(receiver, "Installer", true), original});
                const auto wait_for_script = [&](const auto& ready, const char* failure) {
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
                    while (std::chrono::steady_clock::now() < deadline) {
                        runtime.poll();
                        if (ready())
                            return;
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    throw std::runtime_error(failure);
                };
                wait_for_script([&] {
                    const auto part = runtime.world().object(receiver.id);
                    return part && part->script_access_pin == -123;
                }, "Remote script installer did not set the signed PIN");

                runtime.restore_task_inventory(sender.id, {task(sender, "Sender", true), payload});
                wait_for_script([&] {
                    const auto part = runtime.world().object(sender.id);
                    return part && part->name == "Wrong PIN returned";
                }, "Remote script sender did not resume after the wrong PIN");
                const auto rejected = runtime.snapshot_task_inventory(receiver.id);
                if (!rejected || !std::ranges::any_of(*rejected, [&](const auto& item) {
                        return item.item_id == original->item_id && !item.script_running;
                    }))
                    throw std::runtime_error("Wrong PIN replaced the receiver's script");

                wait_for_script([&] {
                    const auto part = runtime.world().object(receiver.id);
                    return part && part->name == "81";
                }, "Remote script did not run with the supplied start parameter");
                const auto installed = runtime.snapshot_task_inventory(receiver.id);
                const auto source_items = runtime.snapshot_task_inventory(sender.id);
                if (!installed || installed->size() != 2 || !std::ranges::any_of(*installed, [&](const auto& item) {
                        return item.name == "Payload" && item.item_id != original->item_id &&
                               item.item_id != payload->item_id && item.parent_part_id == receiver.id &&
                               item.asset_id == payload->asset_id && item.script_running;
                    }) || !source_items || !std::ranges::any_of(*source_items, [&](const auto& item) {
                        return item.item_id == payload->item_id;
                    }))
                    throw std::runtime_error("Remote script did not copy and replace the destination inventory item");
                runtime.world().remove(sender.id);
                runtime.world().remove(receiver.id);

                struct TransferCase {
                    const char* description;
                    bool same_owner;
                    bool same_group;
                    bool destination_can_modify;
                    bool source_can_transfer;
                    bool payload_can_copy;
                    bool expected;
                };
                const TransferCase cases[] = {
                    {"shared group", false, true, true, true, true, true},
                    {"different group", false, false, true, true, true, false},
                    {"group modify denied", false, true, false, true, true, false},
                    {"source transfer denied", false, true, true, false, true, false},
                    {"payload copy denied", false, true, true, true, false, false},
                    {"owner modify denied", true, true, false, true, true, false},
                };
                struct TransferAttempt {
                    SceneObject source;
                    SceneObject destination;
                    Uuid payload;
                };
                std::vector<TransferAttempt> attempts;
                const auto shared_group = Uuid::random();
                constexpr auto copy = bits(PermissionMask::Copy);
                constexpr auto modify = bits(PermissionMask::Modify);
                constexpr auto transfer = bits(PermissionMask::Transfer);
                constexpr auto move = bits(PermissionMask::Move);
                for (const auto& scenario : cases) {
                    auto destination = SceneObject::primitive(scenario.same_owner ? owner : Uuid::random(),
                                                              {230, 245, 30});
                    destination.group = shared_group;
                    destination.script_access_pin = -123;
                    destination.group_mask = scenario.destination_can_modify ? modify : 0;
                    if (!scenario.destination_can_modify)
                        destination.owner_mask &= ~modify;

                    auto source = SceneObject::primitive(owner, {232, 245, 30});
                    source.name = "Transfer permission fixture";
                    source.description = destination.id.str();
                    source.group = scenario.same_group ? shared_group : Uuid::random();
                    if (!scenario.source_can_transfer)
                        source.owner_mask &= ~transfer;
                    if (!runtime.world().add(destination) || !runtime.world().add(source))
                        throw std::runtime_error("Could not rez the remote script transfer fixtures");

                    const auto item = task(source, "Payload", false);
                    item->base_permissions = modify | move | (scenario.payload_can_copy ? copy : 0);
                    item->current_permissions = copy | move;
                    item->group_permissions = copy | modify;
                    item->everyone_permissions = modify | transfer;
                    item->next_permissions = copy | transfer;
                    item->flags = 2;
                    runtime.restore_task_inventory(source.id, {task(source, "Sender", true), item});
                    attempts.push_back({std::move(source), std::move(destination), item->item_id});
                }
                wait_for_script([&] {
                    return std::ranges::all_of(attempts, [&](const auto& attempt) {
                        const auto source = runtime.world().object(attempt.source.id);
                        return source && source->name == "Transfer attempt finished";
                    });
                }, "Remote script transfer attempts did not finish");

                for (std::size_t index = 0; index < attempts.size(); ++index) {
                    const auto& scenario = cases[index];
                    const auto& attempt = attempts[index];
                    const auto items = runtime.snapshot_task_inventory(attempt.destination.id);
                    if (!scenario.expected) {
                        if (items && !items->empty())
                            throw std::runtime_error(std::string("Remote load unexpectedly allowed ") + scenario.description);
                    } else {
                        const auto destination = runtime.world().object(attempt.destination.id);
                        if (!items || items->size() != 1 || !destination || destination->name == "42")
                            throw std::runtime_error("Remote load must install the shared-group payload stopped");
                        const auto& received = items->front();
                        if (received.item_id == attempt.payload || received.creator_id != attempt.source.owner ||
                            received.owner_id != attempt.destination.owner || received.group_id != shared_group ||
                            received.base_permissions != copy || received.current_permissions != copy ||
                            received.group_permissions != copy || received.everyone_permissions != transfer ||
                            received.next_permissions != (copy | transfer) || received.flags != 0x102u ||
                            received.script_running)
                            throw std::runtime_error("Remote load must apply the source next-owner permissions");
                    }
                    runtime.world().remove(attempt.source.id);
                    runtime.world().remove(attempt.destination.id);
                }
            }
            {
                const auto owner = Uuid::random();
                std::vector<Uuid> targets;
                for (const float x : {204.f, 198.f, 202.f}) {
                    auto target = SceneObject::primitive(owner, {x, 240, 30});
                    target.name = "Sensor target";
                    if (!runtime.world().add(target))
                        throw std::runtime_error("Could not add a sensor target");
                    targets.push_back(target.id);
                }
                const auto origin = runtime.world().add(SceneObject::primitive(owner, {200, 240, 30}));
                if (!origin)
                    throw std::runtime_error("Could not add the sensor host");
                auto task = std::make_shared<TaskInventoryItem>();
                task->item_id = Uuid::random();
                task->parent_id = task->parent_part_id = origin->id;
                task->asset_id = sensor_asset->full_id();
                task->owner_id = task->creator_id = owner;
                task->name = "Sensor fixture";
                task->type = task->inventory_type = 10;
                task->script_running = true;
                runtime.restore_task_inventory(origin->id, {task});
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                bool stopped = false;
                while (std::chrono::steady_clock::now() < deadline) {
                    runtime.poll();
                    const auto host = runtime.world().object(origin->id);
                    if (host && host->name == "Sensor repeat stopped") {
                        stopped = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                if (!stopped)
                    throw std::runtime_error("Object sensor did not report ordered cone matches, a miss and a repeat");
                const auto repeat_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
                while (std::chrono::steady_clock::now() < repeat_deadline) {
                    runtime.poll();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                const auto host = runtime.world().object(origin->id);
                if (!host || host->name != "Sensor repeat stopped")
                    throw std::runtime_error("Removed sensor continued to deliver repeat events");
                runtime.world().remove(origin->id);
                const auto make_checkpoint_host = [&](std::string name) {
                    auto prim = SceneObject::primitive(owner, {200, 240, 30});
                    prim.name = std::move(name);
                    if (!runtime.world().add(prim))
                        throw std::runtime_error("Could not rez the sensor checkpoint fixture");
                    return prim.id;
                };
                const auto wait_for_sensor_name = [&](Uuid part, std::string_view name) {
                    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    while (std::chrono::steady_clock::now() < until) {
                        runtime.poll();
                        const auto object = runtime.world().object(part);
                        if (object && object->name == name)
                            return;
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    throw std::runtime_error("Sensor checkpoint fixture did not reach " + std::string(name));
                };
                const auto source = make_checkpoint_host("Restored sensor fixture");
                auto checkpoint_task = std::make_shared<TaskInventoryItem>(*task);
                checkpoint_task->item_id = Uuid::random();
                checkpoint_task->parent_id = checkpoint_task->parent_part_id = source;
                runtime.restore_task_inventory(source, {checkpoint_task});
                wait_for_sensor_name(source, "Sensor checkpoint armed");
                const auto saved = runtime.snapshot_task_script_states(source);
                const auto state = saved.find(checkpoint_task->item_id);
                if (state == saved.end())
                    throw std::runtime_error("Could not snapshot the repeating sensor script");
                const auto decoded = script::decode_lsl_checkpoint(state->second);
                if (!decoded || decoded->sensors.size() != 1 || decoded->sensors.front().interval != .5)
                    throw std::runtime_error("Repeating sensor was missing from the script checkpoint");
                runtime.world().remove(source);
                runtime.poll();

                const auto receiver = make_checkpoint_host("Sensor checkpoint armed");
                auto restored_task = std::make_shared<TaskInventoryItem>(*checkpoint_task);
                restored_task->parent_id = restored_task->parent_part_id = receiver;
                const auto restored_at = std::chrono::steady_clock::now();
                runtime.restore_task_inventory(receiver, {restored_task}, saved);
                wait_for_sensor_name(receiver, "Sensor checkpoint resumed");
                if (std::chrono::steady_clock::now() - restored_at < std::chrono::milliseconds(500))
                    throw std::runtime_error("Restored sensor fired before its interval elapsed");
                runtime.world().remove(receiver);
                const std::string managed_listeners_and_sensors = R"xml(
                        <plugin type="System.String">listener</plugin>
                        <plugin type="System.Int32">20</plugin>
                        <plugin type="System.Boolean">True</plugin>
                        <plugin type="System.Int32">7</plugin>
                        <plugin type="System.Int32">7197</plugin>
                        <plugin type="System.String">^Managed speaker$</plugin>
                        <plugin type="OpenMetaverse.UUID">00000000-0000-0000-0000-000000000000</plugin>
                        <plugin type="System.String">^matched [0-9]+$</plugin>
                        <plugin type="System.Int32">3</plugin>
                        <plugin type="System.Boolean">False</plugin>
                        <plugin type="System.Int32">8</plugin>
                        <plugin type="System.Int32">7197</plugin>
                        <plugin type="System.String"></plugin>
                        <plugin type="OpenMetaverse.UUID">00000000-0000-0000-0000-000000000000</plugin>
                        <plugin type="System.String"></plugin>
                        <plugin type="System.Int32">0</plugin>
                        <plugin type="System.Boolean">True</plugin>
                        <plugin type="System.Int32">9</plugin>
                        <plugin type="System.Int32">7198</plugin>
                        <plugin type="System.String">Managed speaker</plugin>
                        <plugin type="OpenMetaverse.UUID">00000000-0000-0000-0000-000000000000</plugin>
                        <plugin type="System.String">legacy filter</plugin>
                        <plugin type="System.String">sensor</plugin>
                        <plugin type="System.Int32">6</plugin>
                        <plugin type="System.Double">0.1</plugin>
                        <plugin type="System.String">Sensor target</plugin>
                        <plugin type="OpenMetaverse.UUID">00000000-0000-0000-0000-000000000000</plugin>
                        <plugin type="System.Int32">4</plugin>
                        <plugin type="System.Single">10</plugin>
                        <plugin type="System.Single">0.5</plugin>
                    </Plugins>
                )xml";
                const std::string managed_event_queue = R"xml(
                    <EventQueue>
                        <Event Name="dataserver">
                            <param type="OpenMetaverse.UUID">cfab1234-1234-4321-1234-123456789abc</param>
                            <param type="OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLString">queued answer</param>
                        </Event>
                        <Event Name="touch_start">
                            <param type="System.String">bad count</param>
                        </Event>
                        <Event Name="touch_start">
                            <param type="OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLInteger">1</param>
                            <DetectParams key="cfab1234-1234-4321-1234-123456789abc"
                                owner="cfab1234-1234-4321-1234-123456789abc"
                                group="00000000-0000-0000-0000-000000000000"
                                name="Saved toucher" type="1" linkNum="3"
                                pos="&lt;1, 2, 3&gt;" position="&lt;201, 240, 30&gt;"
                                velocity="&lt;0, 0, 0&gt;" rotation="&lt;0, 0, 0, 1&gt;" />
                        </Event>
                    </EventQueue>
                )xml";
                const auto import_managed_script = [&](const std::string& asset,
                                                       const std::string& embedded_state,
                                                       const std::string& crossing_state) {
                    const auto object = Uuid::random();
                    const auto item = Uuid::random();
                    auto xml = "<SceneObjectGroup><SceneObjectPart><UUID><UUID>" + object.str() +
                        "</UUID></UUID><OwnerID><UUID>" + return_parcel->owner.str() +
                        "</UUID></OwnerID><Name>Managed sensor fixture</Name>"
                        "<TaskInventory><TaskInventoryItem><ItemID><UUID>" + item.str() +
                        "</UUID></ItemID><AssetID><UUID>" + asset +
                        "</UUID></AssetID><ParentID><UUID>" + object.str() +
                        "</UUID></ParentID><ParentPartID><UUID>" + object.str() +
                        "</UUID></ParentPartID><OwnerID><UUID>" + return_parcel->owner.str() +
                        "</UUID></OwnerID><Name>Sensor fixture</Name>"
                        "<InvType>10</InvType><Type>10</Type></TaskInventoryItem></TaskInventory>"
                        "</SceneObjectPart><OtherParts />";
                    if (!embedded_state.empty())
                        xml += "<GroupScriptStates><SavedScriptState UUID=\"" + item.str() + "\">" +
                               "<State Engine=\"YEngine\" UUID=\"" + item.str() + "\" Asset=\"" + asset + "\">" +
                               embedded_state + "</State></SavedScriptState></GroupScriptStates>";
                    xml += "</SceneObjectGroup>";
                    const auto transfer_state = crossing_state.empty() ? std::string{} :
                        "<ScriptData><ScriptStates><State Engine=\"YEngine\" UUID=\"" + item.str() +
                        "\" Asset=\"" + asset + "\">" + crossing_state + "</State></ScriptStates></ScriptData>";
                    network::HttpResponse response;
                    server::ObjectHandler(&runtime).post_object(network::Value::object({
                        {"destination_uuid", runtime.descriptor().id.str()},
                        {"sog", xml},
                        {"new_position", "<200,240,30>"},
                        {"state", transfer_state},
                    }), response);
                    if (response.body != "True")
                        throw std::runtime_error("Managed script fixture import failed with HTTP " +
                                                 std::to_string(response.status) + " " + response.body);
                    return std::pair{object, item};
                };
                for (const bool embedded : {false, true}) {
                    const auto asset = sensor_asset->full_id().str();
                    const auto timer_remaining = embedded ? "7500000" : "-10000";
                    const auto managed_plugins = std::string("<Plugins>"
                        "<plugin type=\"System.String\">timer</plugin>"
                        "<plugin type=\"System.Int32\">2</plugin>"
                        "<plugin type=\"System.Int64\">2500000</plugin>"
                        "<plugin type=\"System.Int64\">") + timer_remaining + "</plugin>" +
                        managed_listeners_and_sensors;
                    const auto script_state = "<ScriptState Engine=\"YEngine\" Asset=\"" + asset +
                        "\"><Running>True</Running><scrpTime>12500.5</scrpTime>"
                        "<mEvtDly>0.05</mEvtDly>" + managed_event_queue + managed_plugins + "</ScriptState>";
                    const auto received_at = std::chrono::steady_clock::now();
                    const auto [object, item] = import_managed_script(
                        asset, embedded ? script_state : std::string{}, embedded ? std::string{} : script_state);
                    runtime.poll();
                    const auto restored = runtime.snapshot_task_script_states(object);
                    const auto checkpoint = restored.find(item);
                    if (checkpoint == restored.end())
                        throw std::runtime_error("Managed sensor script was not compiled after import");
                    const auto imported = script::decode_lsl_checkpoint(checkpoint->second);
                    if (!imported || imported->sensors.size() != 1)
                        throw std::runtime_error(embedded ? "Embedded managed sensor was not restored" :
                                                           "Crossing managed sensor was not restored");
                    if (imported->timer_interval != 2500000 ||
                        (embedded && imported->timer_remaining <= imported->timer_interval))
                        throw std::runtime_error("Managed timer interval or remaining delay was not restored");
                    if (imported->timer_elapsed < 125005000 || imported->minimum_event_delay != 0.05)
                        throw std::runtime_error("Managed script clock or event delay was not restored");
                    if (imported->listeners.size() != 3 || imported->listeners[0].handle != 7 ||
                        imported->listeners[0].regex_flags != 3 || imported->listeners[1].active ||
                        imported->listeners[2].regex_flags != 0)
                        throw std::runtime_error("Managed listeners did not survive checkpoint encoding");
                    const auto listener_blob = script::encode_lsl_listeners(imported->listeners);
                    const auto listeners = script::decode_lsl_listeners(listener_blob);
                    if (!listeners || listeners->size() != 3 || listeners->front().regex_flags != 3)
                        throw std::runtime_error("Managed regex flags did not survive standalone listener encoding");
                    wait_for_sensor_name(object, "Managed sensor imported");
                    bool timer_repeated = false;
                    const auto deadline = received_at + std::chrono::seconds(5);
                    while (std::chrono::steady_clock::now() < deadline) {
                        runtime.poll();
                        const auto part = runtime.world().object(object);
                        if (part && part->description == "Managed timer repeated") {
                            timer_repeated = true;
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    if (!timer_repeated)
                        throw std::runtime_error("Managed timer or queued events did not run after import");
                    const auto expected_delay = std::chrono::milliseconds(embedded ? 1000 : 250);
                    if (std::chrono::steady_clock::now() - received_at < expected_delay)
                        throw std::runtime_error("Managed timer fired before its saved delay and interval elapsed");
                    const auto start_fixture_script = [&](std::string name) {
                        const auto host = make_checkpoint_host(std::move(name));
                        auto task = std::make_shared<TaskInventoryItem>(*checkpoint_task);
                        task->item_id = Uuid::random();
                        task->parent_id = task->parent_part_id = host;
                        runtime.restore_task_inventory(host, {task});
                        return host;
                    };
                    const auto listener = start_fixture_script("Native regex listener");
                    wait_for_sensor_name(listener, "Native regex listener armed");
                    const auto speaker = start_fixture_script("Managed speaker");
                    wait_for_sensor_name(object, "Managed listeners matched");
                    wait_for_sensor_name(listener, "Native regex listener matched");
                    runtime.world().remove(listener);
                    runtime.world().remove(speaker);
                    runtime.world().remove(object);
                }
                const auto idle_asset_id = idle_asset->full_id().str();
                const auto make_idle_state = [&](std::string_view hash, bool running) {
                    return "<ScriptState Engine=\"YEngine\" Asset=\"" + idle_asset_id +
                        "\" SourceHash=\"" + std::string(hash) + "\"><Running>" +
                        (running ? "True" : "False") + "</Running>" R"xml(
                            <Snapshot>DBAAAAEAAAD/////CQJ4YQAAAAAJAmNoAAAAAAkCZG8AAAAACQJpbgEAAAAMKgAAAAkCbGkAAAAACQJvYgAAAAAJAnJvAAAAAAkCc3QAAAAACQJ2ZQAAAAAJAnNjAAAAAAkEZGVbXQAAAAAA</Snapshot>
                            <Permissions granter="cfab1234-1234-4321-1234-123456789abc" mask="16" />
                            <EventQueue><Event Name="dataserver">
                                <param type="OpenMetaverse.UUID">cfab1234-1234-4321-1234-123456789abc</param>
                                <param type="System.String">Restored global </param>
                            </Event></EventQueue>
                        </ScriptState>)xml";
                };
                const auto idle_state = make_idle_state("7A0A5CBC9360F2BEE78A30ED4F837D2D", true);
                const auto stopped_state = make_idle_state("7A0A5CBC9360F2BEE78A30ED4F837D2D", false);
                const auto stale_state = make_idle_state("00000000000000000000000000000000", false);
                struct IdleImportCase {
                    std::string embedded, crossing;
                    bool restored;
                    bool running = true;
                };
                std::optional<script::LslCheckpoint> native_idle_checkpoint;
                for (const auto& scenario : {
                        IdleImportCase{idle_state, {}, true},
                        IdleImportCase{{}, idle_state, true},
                        IdleImportCase{stopped_state, {}, true, false},
                        IdleImportCase{stale_state, {}, false},
                        IdleImportCase{idle_state, stale_state, false},
                     }) {
                    const auto [object, item] = import_managed_script(
                        idle_asset_id, scenario.embedded, scenario.crossing);
                    if (scenario.running)
                        wait_for_sensor_name(object, scenario.restored ? "Restored global 42" : "Fresh startup");
                    else
                        runtime.poll();
                    const auto states = runtime.snapshot_task_script_states(object);
                    const auto found = states.find(item);
                    const auto checkpoint = found == states.end() ? std::nullopt
                        : script::decode_lsl_checkpoint(found->second);
                    if (!checkpoint || checkpoint->running != scenario.running ||
                        checkpoint->permissions_mask != 0 || !checkpoint->permissions_granter.is_zero() ||
                        checkpoint->state != (scenario.restored ? "zed" : "default") ||
                        (!scenario.running && checkpoint->queued_events.size() != 1))
                        throw std::runtime_error("Managed idle import chose the wrong script state");
                    if (scenario.restored && scenario.running)
                        native_idle_checkpoint = checkpoint;
                    runtime.world().remove(object);
                }
                if (!native_idle_checkpoint)
                    throw std::runtime_error("Missing native checkpoint for YEngine disk import fixture");
                native_idle_checkpoint->globals.front() = std::int32_t{84};
                native_idle_checkpoint->queued_events.push_back(
                    {"dataserver", {Uuid{}.str(), std::string("Native global ")}, {}});
                const auto directory = yengine_state_directory.root / "Yengine" / runtime.descriptor().id.str();
                std::filesystem::create_directories(directory);
                const auto write_state = [](const std::filesystem::path& path, const std::string& xml) {
                    std::ofstream file(path, std::ios::binary);
                    if (!(file << xml))
                        throw std::runtime_error("Could not write YEngine state fixture");
                };
                for (const bool sharded : {false, true}) {
                    const auto object = make_checkpoint_host("YEngine disk state fixture");
                    auto task = std::make_shared<TaskInventoryItem>(*checkpoint_task);
                    task->item_id = Uuid::random();
                    task->parent_id = task->parent_part_id = object;
                    task->asset_id = idle_asset->full_id();
                    const auto filename = task->item_id.str() + ".state";
                    write_state(directory / filename, sharded ? stale_state : idle_state);
                    if (sharded) {
                        const auto shard = directory / filename.substr(0, 1);
                        std::filesystem::create_directories(shard);
                        write_state(shard / filename.substr(1), idle_state);
                    }
                    runtime.restore_task_inventory(object, {task}, {}, RegionRuntime::TaskInventoryRestoreSource::RegionStart);
                    wait_for_sensor_name(object, "Restored global 42");
                    const auto restored = runtime.world().object(object);
                    if (!restored || restored->description != "cfab1234-1234-4321-1234-123456789abc:16")
                        throw std::runtime_error("YEngine disk import did not restore the script grant");
                    runtime.world().remove(object);
                }
                const auto object = make_checkpoint_host("Native checkpoint priority fixture");
                auto native_task = std::make_shared<TaskInventoryItem>(*checkpoint_task);
                native_task->item_id = Uuid::random();
                native_task->parent_id = native_task->parent_part_id = object;
                native_task->asset_id = idle_asset->full_id();
                write_state(directory / (native_task->item_id.str() + ".state"), idle_state);
                runtime.restore_task_inventory(
                    object, {native_task},
                    {{native_task->item_id, script::encode_lsl_checkpoint(*native_idle_checkpoint)}},
                    RegionRuntime::TaskInventoryRestoreSource::RegionStart);
                wait_for_sensor_name(object, "Native global 84");
                runtime.world().remove(object);
                for (const auto& target : targets)
                    runtime.world().remove(target);
            }
            verify_unattended_object_physics(runtime, policies.store_asset);
            {
                const auto source = R"LSL(
                    key expected;
                    default {
                        state_entry() {
                            expected = llHTTPRequest("http://user:secret@127.0.0.1:)LSL" + std::to_string(script_http_port) +
                            R"LSL(/script", [HTTP_METHOD, "POST", HTTP_CUSTOM_HEADER, "X-Fixture", "custom"], "fixture");
                        }
                        http_response(key request, integer status, list metadata, string body) {
                            if (request == expected && llGetListLength(metadata) == 0)
                                llSetObjectName((string)status + " " + body);
                        }
                    }
                )LSL";
                const auto asset = std::make_shared<AssetBase>(
                    Uuid::random(), "Outbound script HTTP", static_cast<std::int8_t>(AssetType::LSLText), "");
                asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
                policies.store_asset(asset);
                const auto part = SceneObject::primitive(region.estate_owner, {220, 215, 30});
                if (!runtime.world().add(part))
                    throw std::runtime_error("Could not rez outbound script HTTP fixture");
                auto item = std::make_shared<TaskInventoryItem>();
                item->item_id = Uuid::random();
                item->parent_id = item->parent_part_id = part.id;
                item->asset_id = asset->full_id();
                item->owner_id = item->creator_id = part.owner;
                item->type = item->inventory_type = 10;
                item->script_running = true;
                item->name = "Outbound script HTTP";
                runtime.restore_task_inventory(part.id, {item});
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                while (std::chrono::steady_clock::now() < deadline) {
                    runtime.poll();
                    if (runtime.world().object(part.id)->name == "201 network reply")
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                if (runtime.world().object(part.id)->name != "201 network reply")
                    throw std::runtime_error("Outbound script HTTP did not deliver its reply");
                runtime.world().remove(part.id);
            }
            {
                const auto source = R"LSL(
                    default {
                        state_entry() {
                            key account = ")LSL" + estate_access_target.str() + R"LSL(";
                            key group = ")LSL" + other_group.str() + R"LSL(";
                            string result;
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_ALLOWED_AGENT_REMOVE, account);
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_BANNED_AGENT_ADD, account);
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_BANNED_AGENT_REMOVE, account);
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_BANNED_AGENT_ADD, account);
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_ALLOWED_AGENT_ADD, account);
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_BANNED_AGENT_REMOVE, account);
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_ALLOWED_AGENT_REMOVE, account);
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_ALLOWED_GROUP_ADD, group);
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_ALLOWED_GROUP_REMOVE, group);
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_ALLOWED_GROUP_ADD, account);
                            result += (string)llManageEstateAccess(ESTATE_ACCESS_ALLOWED_AGENT_ADD, NULL_KEY);
                            llSetObjectName(result);
                        }
                    }
                )LSL";
                auto asset = std::make_shared<AssetBase>(
                    Uuid::random(), "Manage estate access", static_cast<std::int8_t>(AssetType::LSLText), "");
                asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
                policies.store_asset(asset);
                const auto check_access = [&](Uuid owner, std::string_view expected) {
                    const auto part = SceneObject::primitive(owner, {220, 215, 30});
                    if (!runtime.world().add(part))
                        throw std::runtime_error("Could not rez the estate access script fixture");
                    auto item = std::make_shared<TaskInventoryItem>();
                    item->item_id = Uuid::random();
                    item->parent_id = item->parent_part_id = part.id;
                    item->asset_id = asset->full_id();
                    item->owner_id = item->creator_id = owner;
                    item->type = item->inventory_type = 10;
                    item->script_running = true;
                    item->name = "Manage estate access";
                    runtime.restore_task_inventory(part.id, {item});
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                    while (std::chrono::steady_clock::now() < deadline) {
                        runtime.poll();
                        const auto current = runtime.world().object(part.id);
                        if (current && current->name == expected) {
                            runtime.world().remove(part.id);
                            return;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    throw std::runtime_error("llManageEstateAccess returned unexpected script results");
                };
                check_access(Uuid::random(), "00000000000");
                check_access(region.estate_owner, "01111011100");
            }
            verify_prim_mass_queries(runtime, mass_query_script->full_id(), blocked_visitor);
            verify_script_deletes_linkset(runtime, mass_query_script->full_id());
            verify_script_ground_repel(runtime, mass_query_script->full_id());
            verify_land_collision_scripts(runtime, land_collision_script->full_id());
            verify_object_collision_scripts(runtime, object_collision_script->full_id());
            verify_volume_detect(runtime, volume_detect_script->full_id());
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
                integer waypoint;
                integer evicted_waypoint;
                integer eviction_failed;
                integer ordering_rotation;
                integer position_miss_seen;
                integer ordering_verified;
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
                                    6, -1, 300, 1, 2);
                            if (result != 1 || llGetRegionDayLength() != 21600 || llGetRegionDayOffset() != 82800) {
                                llSay(0, "Environment fixture failed edit " + (string)result);
                                return;
                            }
                            rotation sun = llGetRegionSunRotation();
                            rotation moon = llGetRegionMoonRotation();
                            if (llFabs(sun.y + 0.3824995) > 0.000001 ||
                                llFabs(moon.y - 0.6) > 0.000001 ||
                                llFabs(moon.s - 0.8) > 0.000001) {
                                llSay(0, "Environment fixture failed sky rotation conversion");
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
                        if (osGetAgentIP("invalid") == "" && osGetAgentIP(NULL_KEY) == "") {
                            evicted_waypoint = llTarget(llGetPos(), 1);
                            list rotations;
                            integer i;
                            for (i = 0; i < 8; ++i)
                                rotations += [llRotTarget(llGetRot(), 0.01)];
                            for (i = 0; i < 8; ++i)
                                llRotTargetRemove(llList2Integer(rotations, i));
                            waypoint = llTarget(llGetPos() + <20, 0, 0>, 1);
                            ordering_rotation = llRotTarget(llGetRot(), 0.01);
                        }
                    }
                    not_at_target() {
                        position_miss_seen = TRUE;
                        llTargetRemove(waypoint);
                        waypoint = llTarget(llGetPos(), 1);
                    }
                    at_target(integer handle, vector target, vector current) {
                        llTargetRemove(handle);
                        if (handle == evicted_waypoint) {
                            eviction_failed = TRUE;
                            return;
                        }
                        if (target == current)
                            waypoint = llRotTarget(<1,0,0,0>, 0.01);
                    }
                    not_at_rot_target() {
                        llRotTargetRemove(waypoint);
                        waypoint = llRotTarget(llGetRot(), 0.01);
                    }
                    at_rot_target(integer handle, rotation target, rotation current) {
                        llRotTargetRemove(handle);
                        if (handle == ordering_rotation) {
                            ordering_verified = position_miss_seen;
                            return;
                        }
                        if (target == current && !eviction_failed && ordering_verified)
                            llGetNextEmail("Sender@example.invalid", "Fixture");
                    }
                    email(string stamp, string sender, string subject, string body, integer remaining) {
                        if (stamp == "123" && sender == "sender@example.invalid" && subject == "fixture"
                            && body == "region mailbox" && remaining == 2)
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
            verify_attachment_physics(runtime, region, presence, bound, attachment_physics_script->full_id(),
                                      avatar_collision_script->full_id(), sensor_asset->full_id(), forwarded_script_message,
                                      forwarded_inventory_offer, inventory, hub, restrict_script_teleport,
                                      script_destination.handle(), remote_cancellations, script_landmark->full_id());
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
                altitudes.size() != 3 || altitudes[0].real != 1 || altitudes[1].real != 2 ||
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
        {
            NullPresenceStore store;
            PresenceService shared_presence(store);
            RegionPolicies shared_policies;
            shared_policies.access = [](auto, const auto&, auto&) { return true; };
            shared_policies.authorize = [](const auto&, auto&) { return true; };
            const auto channels = std::make_shared<RemoteDataChannels>();
            shared_policies.script_remote_data_channels = channels;
            auto neighbor = region;
            neighbor.id = Uuid::random();
            neighbor.x += 256;
            RegionRuntime first(region, shared_presence, shared_policies);
            RegionRuntime second(neighbor, shared_presence, shared_policies);
            const auto first_udp = first.start(listener);
            const auto second_udp = second.start(listener);
            if (first_udp.port == 0 || second_udp.port == 0 || first_udp.port == second_udp.port)
                throw std::runtime_error("Two live regions did not bind independent UDP sockets");
            const auto item = Uuid::random();
            const auto first_channel = channels->open(17, item, {}, region.id);
            const auto second_channel = channels->open(17, item, {}, neighbor.id);
            const auto admit = [&](RegionRuntime& runtime, GridRegion& destination, std::string path) {
                auto circuit = std::make_shared<AgentCircuitData>();
                circuit->agent_id = Uuid::random();
                circuit->session_id = Uuid::random();
                circuit->secure_session_id = Uuid::random();
                circuit->circuit_code = 47;
                circuit->caps_path = std::move(path);
                circuit->start_position = {128, 128, 30};
                shared_presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id);
                LoginTransferContext context;
                std::string reason;
                if (!runtime.create_agent(nullptr, destination, circuit, 128, context, reason))
                    throw std::runtime_error("Shared listener fixture admission failed: " + reason);
                if (!shared_presence.report(circuit->session_id, destination.id))
                    throw std::runtime_error("Could not report the session in its destination region");
                return circuit->session_id;
            };
            const auto second_session = admit(second, neighbor, "second-region-caps");
            const auto second_routes = listener->routes.simple_keys();
            const auto second_polls = listener->routes.poll_keys();
            const auto first_session = admit(first, region, "first-region-caps");
            if (second_routes.empty() || second_polls.empty() ||
                listener->routes.simple_keys().size() <= second_routes.size())
                throw std::runtime_error("Both regions did not register capability routes");
            first.stop();
            const auto remaining = shared_presence.agent(second_session);
            if (shared_presence.agent(first_session) || !remaining || remaining->region_id != neighbor.id)
                throw std::runtime_error("Stopping one region did not isolate presence cleanup");
            if (listener->routes.simple_keys() != second_routes || listener->routes.poll_keys() != second_polls)
                throw std::runtime_error("First region shutdown changed the second region's capability routes");
            if (channels->find(first_channel) || !channels->find(second_channel))
                throw std::runtime_error("Stopping one region changed another region's remote-data channels");
            second.poll();
            second.stop();
            if (shared_presence.agent(second_session))
                throw std::runtime_error("Final region shutdown retained its presence session");
            if (!listener->routes.simple_keys().empty() || !listener->routes.poll_keys().empty())
                throw std::runtime_error("Final region shutdown left capability routes on the shared listener");
            if (channels->find(second_channel))
                throw std::runtime_error("Second region retained its remote-data channel after shutdown");
        }
        std::cout << "PASS: region cleanup continues after provider failure; destructor does not terminate\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
