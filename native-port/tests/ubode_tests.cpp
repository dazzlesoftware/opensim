#include "opensim/physics/ubode.hpp"
#include "opensim/physics/mesh_asset.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/sdk_llsd_binary.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/framework/texture_entry.hpp"
#include "opensim/scene/map_tile.hpp"
#include "opensim/scene/world.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <stdexcept>
using namespace opensim;
static void check(bool b, const char* text) {
    if (!b)
        throw std::runtime_error(text);
}
static void check_mesh_asset_decoder(bool render_only = false) {
    using network::Value;
    const auto binary = [](std::initializer_list<std::uint8_t> bytes) {
        return network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(bytes));
    };
    auto submesh = Value::object({
        {"Position", binary({0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 255, 255, 0, 0})},
        {"TriangleList", binary({0, 0, 1, 0, 2, 0})}
    });
    const auto vec = [](int x, int y, int z) {
        Value value;
        value.kind = Value::Kind::Array;
        value.array = {Value(x), Value(y), Value(z)};
        return value;
    };
    auto scaled = submesh;
    scaled.set("PositionDomain", Value::object({{"Min", vec(-2, -3, 4)}, {"Max", vec(2, 3, 4)}}));
    Value contents;
    contents.kind = Value::Kind::Array;
    contents.array = {submesh, Value::object({{"NoGeometry", Value(true)}}), scaled};
    const auto asset = [](const Value& data, std::string_view section, int offset) {
        const auto compressed = network::serialize_zipped_llsd_binary(data);
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
        key(section);
        header += '{';
        number(2);
        key("offset");
        header += 'i';
        number(offset);
        key("size");
        header += 'i';
        number(static_cast<std::uint32_t>(compressed.size()));
        return header + "}}padding" + compressed;
    };
    if (render_only) {
        auto render_face = submesh;
        render_face.set("TexCoord0", binary({0, 0, 0, 0, 255, 255, 0, 0, 0, 0, 255, 255}));
        const auto uv_pair = [](int u, int v) {
            Value pair;
            pair.kind = Value::Kind::Array;
            pair.array = {Value(u), Value(v)};
            return pair;
        };
        render_face.set("TexCoord0Domain", Value::object({{"Min", uv_pair(0, 0)}, {"Max", uv_pair(1, 1)}}));
        render_face.set("Normal", binary({255}));
        Value render_contents;
        render_contents.kind = Value::Kind::Array;
        render_contents.array = {Value::object({{"NoGeometry", Value(true)}}), render_face};
        const auto render_asset = asset(render_contents, "high_lod", 7);
        const auto decoded_render = physics::decode_asset_render_mesh(render_asset, physics::AssetMeshLod::High);
        check(decoded_render && decoded_render->size() == 1 &&
                  decoded_render->front().texcoords[1] == std::array{1.f, 0.f},
              "Warp3D render LOD skips empty submeshes and normal data and decodes UV coordinates");
        check(!physics::decode_asset_render_mesh(render_asset, physics::AssetMeshLod::Low),
              "missing requested render LOD reports absent geometry");
        render_contents.array[1].set("TexCoord0", binary({0, 0, 0, 0}));
        check(!physics::decode_asset_render_mesh(asset(render_contents, "high_lod", 7), physics::AssetMeshLod::High),
              "truncated render UV data is rejected");

        const auto mesh_id = framework::Uuid("7e0b2a75-0b6a-4aae-8f6a-0ea69823e5aa");
        auto part = scene::SceneObject::primitive({}, {4.f, 4.f, 20.f}, {4.f, 4.f, 2.f});
        const auto mesh_shape = framework::PrimitiveBaseShapeCore::create_mesh(2, mesh_id, [](const std::string&) {});
        part.extra_params = mesh_shape.extra_params();
        part.shape.path_curve = 32;
        framework::TextureEntry textures(framework::TextureEntry::white_texture);
        textures.get_face(0)->color({0.f, 1.f, 0.f, 1.f});
        part.texture_entry = textures.bytes();
        scene::WarpMapRaster raster(8, 8);
        scene::draw_warp_prims(raster, std::span(&part, 1), {}, false, -100.f, 4086.f,
                              [&](framework::Uuid id) {
                                  check(id == mesh_id, "Warp3D requests the mesh asset UUID");
                                  auto result = std::make_shared<framework::AssetBase>();
                                  result->data = std::make_shared<std::vector<std::uint8_t>>(
                                      render_asset.begin(), render_asset.end());
                                  return result;
                              });
        const auto& pixels = raster.image().pixels;
        const scene::MapColor green{0, 255, 0};
        const auto offset = (5 * 8 + 3) * 3;
        check(std::equal(green.begin(), green.end(), pixels.begin() + offset),
              "Warp3D draws render geometry using the compacted face order");

        scene::MapBitmap sculpt{4, 4, {}};
        for (unsigned y = 0; y < 4; ++y) {
            for (unsigned x = 0; x < 4; ++x) {
                sculpt.pixels.insert(sculpt.pixels.end(),
                    {static_cast<std::uint8_t>(x * 85), static_cast<std::uint8_t>(y * 85), 128});
            }
        }
        const auto sculpt_face = [&](std::uint8_t type) {
            return physics::build_sculpt_render_face(sculpt.pixels, 4, 4, 3, type, physics::AssetMeshLod::High);
        };
        const auto plane = sculpt_face(3);
        const auto sphere = sculpt_face(1);
        const auto torus = sculpt_face(2);
        const auto cylinder = sculpt_face(4);
        const auto mirrored = sculpt_face(3 | 0x80);
        check(plane && plane->geometry.vertices.size() == 16 && plane->geometry.indices.size() == 54 &&
                  std::equal(plane->geometry.indices.begin(), plane->geometry.indices.begin() + 6,
                             std::array{0u, 4u, 5u, 0u, 5u, 1u}.begin()) &&
                  plane->normals.front() == framework::Vector3{0, 0, -1} &&
                  plane->texcoords.back() == std::array{1.f, 1.f},
              "sculpt plane matches managed topology, normals, and texture coordinates");
        check(sphere && sphere->geometry.vertices.size() == 30 &&
                  sphere->geometry.vertices[0] == sphere->geometry.vertices[4] &&
                  torus && torus->geometry.vertices.size() == 25 &&
                  torus->geometry.vertices[0] == torus->geometry.vertices[20] &&
                  cylinder && cylinder->geometry.vertices.size() == 20 &&
                  cylinder->geometry.vertices[0] == cylinder->geometry.vertices[4],
              "sculpt sphere, torus, and cylinder close the managed poles and seams");
        check(mirrored && mirrored->geometry.vertices.front().x == .5f &&
                  mirrored->geometry.indices[1] == 5,
              "sculpt mirror reflects X and reverses winding");
        const auto low = physics::build_sculpt_render_face(
            std::vector<std::uint8_t>(64 * 64 * 3), 64, 64, 3, 3, physics::AssetMeshLod::Lowest);
        check(low && low->geometry.vertices.size() == 81 && low->geometry.indices.size() == 384,
              "sculpt map subsamples to the managed low-detail grid");
        check(!physics::build_sculpt_render_face(std::vector<std::uint8_t>(17 * 17 * 3),
                  17, 17, 3, 3, physics::AssetMeshLod::Lowest),
              "sculpt maps that overrun the managed sampling grid are rejected");

        auto sculpt_shape = framework::PrimitiveBaseShapeCore::create_mesh(1, mesh_id, [](const std::string&) {});
        sculpt_shape.set_sculpt_properties(3 | 0x40, mesh_id);
        part.extra_params = sculpt_shape.extra_params();
        scene::WarpMapRaster sculpt_raster(8, 8);
        scene::draw_warp_prims(sculpt_raster, std::span(&part, 1),
                              [&](const auto& id) {
                                  check(id == mesh_id, "Warp3D requests the sculpt map UUID");
                                  return sculpt;
                              }, false, -100.f, 4086.f,
                              [](auto) { return std::shared_ptr<framework::AssetBase>{}; });
        check(std::equal(green.begin(), green.end(), sculpt_raster.image().pixels.begin() + offset),
              "Warp3D draws geometry decoded from a sculpt bitmap");
        return;
    }

    for (const auto section : {"physics_shape", "physics_mesh"}) {
        const auto decoded = physics::decode_asset_physics_mesh(asset(contents, section, 7));
        check(decoded && decoded->indices == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5},
              "physics mesh block offset and submesh index rebasing");
        check(decoded->vertices.size() == 6 && decoded->vertices[0] == framework::Vector3{-.5f, -.5f, -.5f} &&
                  decoded->vertices[4] == framework::Vector3{2, -3, 4},
              "default and explicit mesh position domains");
    }
    check(!physics::decode_asset_physics_mesh(asset(contents, "physics_mesh", 90000)),
          "out-of-range mesh block rejected");

    contents.array[0].set("TriangleList", binary({0, 0, 1, 0, 3, 0}));
    check(!physics::decode_asset_physics_mesh(asset(contents, "physics_mesh", 7)),
          "out-of-range submesh index rejected");

    auto hulls = Value::object({
        {"HullList", binary({4})},
        {"Positions", binary({0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0,
                              0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 255, 255})},
        {"BoundingVerts", binary({0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0,
                                  255, 255, 255, 255, 0, 0, 0, 0, 255, 255, 0, 0,
                                  0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 255, 255,
                                  255, 255, 255, 255, 255, 255, 0, 0, 255, 255, 255, 255})}
    });
    const auto convex_asset = asset(hulls, "physics_convex", 7);
    const auto decomposition = physics::decode_asset_physics_mesh(convex_asset);
    const auto bounding_hull = physics::decode_asset_physics_mesh(convex_asset, true);
    check(decomposition && decomposition->vertices.size() == 4 && decomposition->indices.size() == 12,
          "prim mode uses the decomposed tetrahedron");
    check(bounding_hull && bounding_hull->vertices.size() == 8 && bounding_hull->indices.size() == 36,
          "convex mode triangulates the bounding cube");
}
static std::string h(float f) {
    std::ostringstream s;
    s << std::hex << std::bit_cast<std::uint32_t>(f);
    return s.str();
}
static std::string v(framework::Vector3 a) {
    return h(a.x) + "," + h(a.y) + "," + h(a.z);
}
static framework::Quaternion orientation(int profile) {
    switch (profile) {
    case 0:
        return {0, 0, .70710677f, .70710677f};
    case 1:
        return {.1f, .2f, .3f, .4f};
    case 2:
        return {0, 0, 0, 0};
    case 3:
        return {.01f, .02f, .03f, .99995f};
    case 4:
        return {1, 0, .0011f, 0};
    case 5:
        return {1, 0, .0009f, 0};
    case 6:
        return {.001f, 0, 0, 0};
    default:
        return {-.3f, .4f, -.5f, -.6f};
    }
}
static std::string q(framework::Quaternion a) {
    return h(a.x) + "," + h(a.y) + "," + h(a.z) + "," + h(a.w);
}
static void probe(const std::filesystem::path& path, int mode) {
    scene::Terrain terrain(256, 256);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            terrain.set(x, y, mode & 1 ? 21.f + x * .03125f + y * .015625f : 21.f);
    physics::UbodeWorld world(path, terrain);
    world.add_avatar({128, 128, mode & 2 ? 35.f : 23.f}, {.45f, .6f, 1.9f}, mode & 4);
    for (int i = 0; i < 150; i++) {
        bool rejected = false;
        if ((mode & 1024) && (i == 10 || i == 11 || i == 90)) {
            try {
                world.set_orientation(orientation((mode >> 11) & 7));
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
        }
        if (i == 15 && (mode & 256))
            world.avatar_jump();
        if (i == 70 && (mode & 512))
            world.avatar_jump(-2.f);
        if (i == 20)
            world.set_target_velocity({4.096f, mode & 8 ? 2.f : 0.f, mode & 16 ? 1.f : 0.f});
        if (i == 50)
            world.set_running(mode & 32);
        if (i == 80)
            world.set_target_velocity({0, 0, 0});
        if (i == 100)
            world.set_flying(mode & 64);
        if (i == 120 && (mode & 128)) {
            world.remove_avatar();
            world.add_avatar({128, 128, 23}, {.45f, .6f, 1.9f}, mode & 4);
        }
        world.step();
        auto s = world.snapshot();
        std::cout << v(s.position) << "|" << v(s.velocity) << "|" << v(s.acceleration) << "|"
                  << v(s.body_position) << "|" << v(s.body_velocity) << "|" << v(s.controller_force) << "|"
                  << s.colliding << s.colliding_ground << "|" << q(s.orientation) << "|"
                  << q(s.body_orientation) << "|" << rejected << "\n";
    }
}
static void check_dynamic_boxes(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    const framework::Uuid lower("88f8f560-a0e4-4e08-bc88-c8592dc9a70d");
    const framework::Uuid upper("88f8f560-a0e4-4e08-bc88-c8592dc9a70e");
    physics::UbodeBodyState state;
    state.position = {128, 128, 24};
    world.add_dynamic_box(lower, {1, 1, 1}, 10, state);
    state.position.z = 26;
    world.add_dynamic_box(upper, {1, 1, 1}, 10, state);
    world.step(10);
    const auto falling = world.body_state(upper);
    check(falling.position.z < 26 && falling.velocity.z < -1, "gravity on a dynamic box without avatars");
    world.step(200);
    check(std::abs(world.body_state(lower).position.z - 21.5f) < .05f, "box rests on native terrain");
    check(std::abs(world.body_state(upper).position.z - 22.5f) < .05f, "box rests on another dynamic box");
    const auto stacked = world.top_colliders();
    check(stacked.size() == 2 && stacked[0] == std::pair{lower, 2.f} && stacked[1] == std::pair{upper, 1.f},
          "top colliders rank active prims by terrain and prim contacts in the last step");

    world.remove_box(lower);
    terrain.clear_land(18);
    world.update_terrain(terrain);
    world.step(150);
    const auto updated = world.body_state(upper);
    if (std::abs(updated.position.z - 18.5f) >= .05f)
        std::cerr << "Box after terrain edit: " << updated.position.x << ", "
                  << updated.position.y << ", " << updated.position.z << '\n';
    check(std::abs(updated.position.z - 18.5f) < .05f,
          "removed box and terrain edits affect existing bodies");
    const auto after_removal = world.top_colliders();
    check(after_removal.size() == 1 && after_removal.front() == std::pair{upper, 1.f},
          "top colliders omit removed bodies and reset each step");
}

static void check_sphere_contacts(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    const framework::Uuid base("88f8f560-a0e4-4e08-bc88-c8592dc9a711");
    const framework::Uuid falling("88f8f560-a0e4-4e08-bc88-c8592dc9a712");
    world.add_static_prim(base, framework::PrimType::Sphere, {2, 2, 2}, {128, 128, 22}, {0, 0, 0, 1});
    const physics::UbodePrimPart sphere{{.2f, .2f, .2f}, {}, {0, 0, 0, 1}, 1.f, framework::PrimType::Sphere};
    physics::UbodeBodyState state;
    state.position = {128.8f, 128.8f, 25};
    world.add_dynamic_linkset(falling, {&sphere, 1}, state);
    world.step(100);
    check(std::abs(world.body_state(falling).position.z - 21.1f) < .04f,
          "sphere clears the empty corner of another sphere's bounds");

    world.remove_box(falling);
    state.position = {128, 128, 25};
    world.add_dynamic_linkset(falling, {&sphere, 1}, state);
    world.step(100);
    check(std::abs(world.body_state(falling).position.z - 23.1f) < .04f,
          "sphere contacts the top of a static sphere");
}

static void check_cylinder_mesh(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    const framework::Uuid cylinder("88f8f560-a0e4-4e08-bc88-c8592dc9a713");
    const framework::Uuid falling("88f8f560-a0e4-4e08-bc88-c8592dc9a714");
    world.add_static_prim(cylinder, framework::PrimType::Cylinder, {2, 2, 1}, {128, 128, 23},
                          {0, 0, 0, 1}, 24);
    physics::UbodeBodyState state;
    state.position = {128, 128, 26};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.6f) < .05f,
          "box rests on the upper cylinder cap");
    world.remove_box(cylinder);
    world.add_static_prim(cylinder, framework::PrimType::Cylinder, {2, 2, 1}, {128, 128, 23},
                          {0, 0, 0, 1}, 24, .5f);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.1f) < .05f,
          "box falls through the cylinder opening");
    world.remove_box(falling);
    physics::UbodeBodyState inside;
    inside.position = {128, 128, 23};
    inside.velocity = {4, 0, 0};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, inside);
    world.step(12);
    check(world.body_state(falling).position.x < 128.48f,
          "the inner wall blocks lateral motion through the cylinder");
    world.remove_box(falling);
    state.position.x = 128.75f;
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.6f) < .05f,
          "box rests on the hollow cylinder rim");
    world.remove_box(cylinder);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.1f) < .05f,
          "removing a hollow cylinder removes its rim contact");

    world.remove_box(falling);
    const physics::UbodePrimPart part{{2, 1, 1}, {}, {0, 0, 0, 1}, 2.f, framework::PrimType::Cylinder, 6};
    world.add_dynamic_linkset(cylinder, {&part, 1}, state);
    world.step(120);
    check(std::abs(world.body_state(cylinder).position.z - 21.5f) < .05f,
          "dynamic elliptical cylinder rests on terrain");
}

static void check_linear_path_mesh(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    const framework::Uuid platform("88f8f560-a0e4-4e08-bc88-c8592dc9a717");
    const framework::Uuid falling("88f8f560-a0e4-4e08-bc88-c8592dc9a718");
    const framework::LinearPrimPath extrusion{.25f, .75f, .5f, .5f, .5f, 0.f};
    world.add_static_prim(platform, framework::PrimType::Box, {2, 2, 2}, {128, 128, 23},
                          {0, 0, 0, 1}, 4, 0.f, extrusion);
    physics::UbodeBodyState state;
    state.position = {129.15f, 128, 26};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.6f) < .05f,
          "path cut lowers the cap and shear shifts it past the original box edge");
    world.remove_box(falling);
    state.position = {128.75f, 128.85f, 26};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(world.body_state(falling).position.z < 23.3f,
          "taper leaves the outer edge of the unscaled cap clear");

    world.remove_box(platform);
    world.remove_box(falling);
    framework::LinearPrimPath twisted;
    twisted.twist_end = .78539816f;
    world.add_static_prim(platform, framework::PrimType::Box, {2, 2, 2}, {128, 128, 23},
                          {0, 0, 0, 1}, 4, 0.f, twisted);
    state.position = {129.2f, 128, 26};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 24.1f) < .05f,
          "the twisted top corner supports a box outside the original cap");

    world.remove_box(platform);
    world.remove_box(falling);
    framework::LinearPrimPath cut;
    cut.profile_end = .5f;
    world.add_static_prim(platform, framework::PrimType::Cylinder, {2, 2, 1}, {128, 128, 23},
                          {0, 0, 0, 1}, 24, .25f, cut);
    state.position = {128, 127.4f, 25};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.1f) < .05f,
          "the removed profile sector is clear");
    world.remove_box(falling);
    state.position.y = 128.6f;
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.6f) < .05f,
          "the remaining cut cylinder rim supports the falling box");
    world.remove_box(falling);
    state.position = {128.6f, 127.6f, 23};
    state.velocity = {0, 4, 0};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(12);
    check(world.body_state(falling).position.y < 128.f,
          "the exposed profile edge blocks entry into the remaining cylinder");
}

static void check_polygon_prims(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    const framework::Uuid platform("88f8f560-a0e4-4e08-bc88-c8592dc9a719");
    const framework::Uuid falling("88f8f560-a0e4-4e08-bc88-c8592dc9a71a");
    world.add_static_prim(platform, framework::PrimType::Prism, {2, 2, 1}, {128, 128, 23},
                          {0, 0, 0, 1}, 3);
    physics::UbodeBodyState state;
    state.position = {128.65f, 128.6f, 25};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.1f) < .05f,
          "box clears a point outside the triangular prism");
    world.remove_box(falling);
    state.position.y = 128.f;
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.6f) < .05f,
          "box rests on the triangular cap");

    world.remove_box(platform);
    world.remove_box(falling);
    world.add_static_prim(platform, framework::PrimType::Prism, {2, 2, 1}, {128, 128, 23},
                          {0, 0, 0, 1}, 3, .5f);
    state.position = {128, 128, 25};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.05f) < .05f,
          "box falls through a matching triangular hollow");

    world.remove_box(platform);
    world.remove_box(falling);
    world.add_static_prim(platform, framework::PrimType::Box, {2, 2, 1}, {128, 128, 23},
                          {0, 0, 0, 1}, 4, .5f);
    state.position = {128, 128, 25};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.1f) < .05f,
          "box falls through a matching square hollow");

    world.remove_box(platform);
    world.remove_box(falling);
    framework::LinearPrimPath mixed;
    mixed.hollow_segments = 24;
    world.add_static_prim(platform, framework::PrimType::Box, {2, 2, 1}, {128, 128, 23},
                          {0, 0, 0, 1}, 4, .9f, mixed);
    state.position = {128.55f, 128.55f, 25};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.05f) < .05f,
          "a wide circular opening stays clear near the curved wall");
    world.remove_box(falling);
    state.position = {128.82f, 128.82f, 25};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.55f) < .05f,
          "the corner between a square exterior and circular opening supports a box");

    world.remove_box(platform);
    world.remove_box(falling);
    mixed.hollow_segments = 3;
    world.add_static_prim(platform, framework::PrimType::Box, {2, 2, 1}, {128, 128, 23},
                          {0, 0, 0, 1}, 4, .75f, mixed);
    state.position = {128, 128, 25};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.05f) < .05f,
          "box falls through a triangular opening in a square exterior");

    world.remove_box(platform);
    world.remove_box(falling);
    mixed.hollow_segments = 4;
    world.add_static_prim(platform, framework::PrimType::Cylinder, {2, 2, 1}, {128, 128, 23},
                          {0, 0, 0, 1}, 24, .5f, mixed);
    state.position = {128.55f, 128, 25};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.05f) < .05f,
          "box falls through the corner of a cylinder's square opening");
    world.remove_box(falling);
    state.position = {128.5f, 128.5f, 25};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.55f) < .05f,
          "the cylinder cap supports a box outside its square opening");
}

static void check_ring_mesh(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    const framework::Uuid platform("88f8f560-a0e4-4e08-bc88-c8592dc9a71b");
    const framework::Uuid falling("88f8f560-a0e4-4e08-bc88-c8592dc9a71c");
    framework::LinearPrimPath sweep;
    sweep.hole_y = .25f;
    const framework::Quaternion horizontal{0, -.70710678f, 0, .70710678f};
    world.add_static_prim(platform, framework::PrimType::Torus, {1, 2, 2}, {128, 128, 23},
                          horizontal, 24, 0.f, sweep);
    physics::UbodeBodyState state;
    state.position = {128, 128, 25};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.1f) < .05f,
          "box falls through the center of a torus");
    world.remove_box(falling);
    state.position.y = 128.75f;
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    bool contacted_torus = false;
    for (int frame = 0; frame < 90; ++frame) {
        world.step();
        const auto sample = world.body_state(falling);
        contacted_torus |= std::abs(sample.position.z - 23.6f) < .08f && sample.velocity.z > -.5f;
    }
    check(contacted_torus,
          "the torus sweep supports a box above the ring");
    world.remove_box(platform);
    world.remove_box(falling);
    world.add_static_prim(platform, framework::PrimType::Ring, {1, 2, 2}, {128, 128, 23},
                          horizontal, 3, 0.f, sweep);
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.35f) < .05f,
          "the swept triangular profile supports a box on its flat face");
    world.remove_box(platform);
    world.remove_box(falling);
    sweep.end = .5f;
    world.add_static_prim(platform, framework::PrimType::Tube, {1, 2, 2}, {128, 128, 23},
                          horizontal, 4, 0.f, sweep);
    state.position = {128.75f, 128, 25};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.1f) < .05f,
          "box falls through the removed half of a curved tube");
    world.remove_box(falling);
    state.position = {128.4f, 128.75f, 23};
    state.velocity = {-4, 0, 0};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(12);
    check(world.body_state(falling).position.x > 128.f,
          "a curved path cut exposes a collidable end cap");
    world.remove_box(platform);
    world.remove_box(falling);
    sweep = {};
    sweep.hole_x = .5f;
    sweep.hole_y = .25f;
    sweep.twist_begin = sweep.twist_end = 1.57079633f;
    world.add_static_prim(platform, framework::PrimType::Tube, {1, 2, 2}, {128, 128, 23},
                          horizontal, 4, 0.f, sweep);
    state.position = {128, 128.75f, 25};
    state.velocity = {};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.225f) < .05f,
          "curved tube twist rotates the narrow profile dimension into the vertical axis");
    world.remove_box(platform);
    world.remove_box(falling);
    sweep = {};
    sweep.hole_y = .25f;
    sweep.end = .5f;
    sweep.radius_offset = 1.f;
    world.add_static_prim(platform, framework::PrimType::Tube, {1, 2, 2}, {128, 128, 23},
                          horizontal, 4, 0.f, sweep);
    state.position = {127.06f, 128, 25};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.05f) < .05f,
          "radius offset pulls the outer contact surface inward");
    world.remove_box(falling);
    state.position.x = 127.44f;
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.55f) < .05f,
          "radius-offset tube supports a box at the new path radius");
    world.remove_box(platform);
    world.remove_box(falling);
    sweep = {};
    sweep.hole_y = .25f;
    sweep.shear_y = .8f;
    world.add_static_prim(platform, framework::PrimType::Tube, {1, 2, 2}, {128, 128, 23},
                          horizontal, 4, 0.f, sweep);
    state.position = {127.462f, 128.9f, 23.7f};
    world.add_dynamic_box(falling, {.05f, .05f, .05f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.525f) < .05f,
          "curved Y shear shifts contact outside the original tube");
    world.remove_box(platform);
    world.remove_box(falling);
    sweep = {};
    sweep.hole_x = .2f;
    sweep.hole_y = .25f;
    sweep.skew = .5f;
    sweep.revolutions = 2.5f;
    world.add_static_prim(platform, framework::PrimType::Tube, {1, 2, 2}, {128, 128, 23},
                          horizontal, 4, 0.f, sweep);
    state.position = {127.25f, 128, 24};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 23.3f) < .05f,
          "the third skewed turn supports a box above the earlier turns");
    world.remove_box(platform);
    world.remove_box(falling);
    sweep = {};
    sweep.hole_y = .25f;
    sweep.profile_end = .25f;
    world.add_static_prim(platform, framework::PrimType::Tube, {1, 2, 2}, {128, 128, 23},
                          horizontal, 4, 0.f, sweep);
    state.position = {128, 128.55f, 24};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.05f) < .05f,
          "a profile cut removes the inner part of a curved tube");
    world.remove_box(falling);
    state.position = {128, 128.9f, 23.55f};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    bool hit_cut_face = false;
    for (int frame = 0; frame < 40; ++frame) {
        world.step();
        const auto sample = world.body_state(falling);
        hit_cut_face |= sample.position.z > 23.05f && std::abs(sample.velocity.y) > .2f;
    }
    check(hit_cut_face, "a swept profile cut exposes a collidable sloped face");
    world.remove_box(platform);
    world.remove_box(falling);
    sweep = {};
    sweep.hole_y = .25f;
    sweep.end = .5f;
    world.add_static_prim(platform, framework::PrimType::Tube, {1, 2, 2}, {128, 128, 23},
                          horizontal, 4, .8f, sweep);
    state.position = {128.3f, 128.75f, 23};
    state.velocity = {-5, 0, 0};
    world.add_dynamic_box(falling, {.1f, .1f, .1f}, 1.f, state);
    world.step(7);
    check(world.body_state(falling).position.x < 127.8f, "the cut end leaves the curved tube bore open");
    world.remove_box(falling);
    state.position.z = 23.45f;
    world.add_dynamic_box(falling, {.08f, .08f, .08f}, 1.f, state);
    world.step(7);
    check(world.body_state(falling).position.x > 127.95f, "the hollow cut end retains its collidable rim");
    world.remove_box(platform);
    world.remove_box(falling);

    struct MixedHollow {
        framework::PrimType type;
        std::uint8_t exterior, interior;
        float rim_height;
    };
    const MixedHollow mixed_profiles[] = {
        {framework::PrimType::Tube, 4, 24, 23.45f},
        {framework::PrimType::Tube, 4, 3, 23.45f},
        {framework::PrimType::Ring, 3, 24, 23.23f},
        {framework::PrimType::Ring, 3, 4, 23.23f},
        {framework::PrimType::Torus, 24, 4, 23.45f},
        {framework::PrimType::Torus, 24, 3, 23.45f}
    };
    for (const auto& mixed : mixed_profiles) {
        sweep.hollow_segments = mixed.interior;
        world.add_static_prim(platform, mixed.type, {1, 2, 2}, {128, 128, 23},
                              horizontal, mixed.exterior, .45f, sweep);
        state.position = {128.22f, 128.75f, 23};
        state.velocity = {-4, 0, 0};
        world.add_dynamic_box(falling, {.06f, .06f, .06f}, 1.f, state);
        world.step(5);
        check(world.body_state(falling).position.x < 127.9f,
              "a mixed curved hollow leaves the cut-end bore open");
        world.remove_box(falling);

        state.position.x = 128.2f;
        state.position.z = mixed.rim_height;
        state.velocity.x = -3.f;
        world.add_dynamic_box(falling, {.08f, .08f, .08f}, 1.f, state);
        world.step(5);
        const auto rim = world.body_state(falling);
        if (rim.position.x <= 127.95f)
            throw std::runtime_error("Missing mixed curved rim contact for " +
                                     std::to_string(mixed.exterior) + "/" + std::to_string(mixed.interior) +
                                     " at x=" + std::to_string(rim.position.x));
        world.remove_box(falling);
        world.remove_box(platform);
    }
}

static void check_ellipsoid_mesh(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    const framework::Uuid ellipsoid("88f8f560-a0e4-4e08-bc88-c8592dc9a715");
    const framework::Uuid falling("88f8f560-a0e4-4e08-bc88-c8592dc9a716");
    world.add_static_prim(ellipsoid, framework::PrimType::Sphere, {4, 2, 1}, {128, 128, 23},
                          {0, 0, 0, 1}, 24);
    physics::UbodeBodyState state;
    state.position = {129.8f, 128.8f, 26};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    world.step(120);
    check(std::abs(world.body_state(falling).position.z - 21.1f) < .05f,
          "box clears the corner of the stretched sphere's bounds");

    world.remove_box(falling);
    state.position = {128, 128, 25};
    world.add_dynamic_box(falling, {.2f, .2f, .2f}, 1.f, state);
    bool contacted_top = false;
    for (int frame = 0; frame < 40; ++frame) {
        world.step();
        const auto box = world.body_state(falling);
        if (std::abs(box.position.z - 23.6f) < .06f && box.velocity.z > -.5f)
            contacted_top = true;
    }
    check(contacted_top, "falling box contacts the ellipsoid's flattened top");
}

static void check_compound_box_motion(const std::filesystem::path& path) {
    scene::Terrain terrain;
    physics::UbodeWorld world(path, terrain);
    const framework::Uuid id("88f8f560-a0e4-4e08-bc88-c8592dc9a710");
    const physics::UbodePrimPart parts[] = {
        {{1, 1, 1}, {0, 0, 0}, {0, 0, 0, 1}, 1},
        {{1, 2, 1}, {2, 0, 0}, {0, 0, .70710678f, .70710678f}, 3}
    };
    physics::UbodeBodyState initial;
    initial.position = {128, 128, 30};
    initial.rotation = {0, 0, .70710678f, .70710678f};
    initial.angular_velocity = {0, 0, 2};
    world.add_dynamic_linkset(id, parts, initial);
    const auto start = world.body_state(id);
    check(std::abs(start.position.x - 128.f) < .001f && std::abs(start.position.y - 128.f) < .001f,
          "compound body reports the rotated root pose");
    world.step(15);
    const auto moved = world.body_state(id);
    check(moved.position.x > 128.5f && moved.position.z < 30.f && moved.angular_velocity.z > 1.f,
          "compound root rotates around its offset center of mass");

    physics::UbodePrimForces forces;
    forces.inertia_override = framework::PrimInertiaOverride{
        12.f, {.5f, 0, 0}, {4, 5, 6}, {0, 0, .382683432f, .923879533f}};
    const auto pending = world.inertia_data(id, forces.inertia_override);
    const auto previous = world.inertia_data(id);
    check(pending && pending->mass == 12.f && previous && previous->mass == 4.f,
          "pending inertia readback leaves the current solver body untouched");

    world.remove_box(id);
    world.add_dynamic_linkset(id, parts, initial, forces);
    const auto applied = world.inertia_data(id);
    check(applied && applied->mass == pending->mass && applied->center == pending->center &&
              applied->diagonal == pending->diagonal && applied->products == pending->products,
          "the pending tensor matches the tensor installed on the solver body");
    const auto center = world.mass_properties(id);
    check(center && std::abs(center->center.x - 128.f) < .001f &&
              std::abs(center->center.y - 128.5f) < .001f,
          "the solver body uses the override's rotated center of mass");
}

static void check_rotation_axis_locks(const std::filesystem::path& path) {
    scene::Terrain terrain;
    physics::UbodeWorld world(path, terrain);
    const auto id = framework::Uuid::random();
    const physics::UbodePrimPart part{{1, 1, 1}, {.25f, 0, 0}, {0, 0, 0, 1}, 2.f};
    const physics::UbodeBodyState state{{128, 128, 30}, {0, 0, .70710678f, .70710678f}, {}, {2, 4, 1}};
    physics::UbodePrimForces forces;
    forces.rotation_axis_locks = 2;
    world.add_dynamic_linkset(id, {&part, 1}, state, forces);
    world.step();
    const auto locked = world.body_state(id).angular_velocity;
    check(std::abs(locked.y) < .01f && locked.x > 1.f && locked.z > .5f,
          "the motor locks the prim's rotated local X axis and allows the other axes");
    world.apply_prim_impulse(id, {400, 0, 0}, false);
    check(world.update_body_motion(id, {}, {}, state.angular_velocity, {}, 0), "unlock the existing body");
    world.step();
    const auto unlocked = world.body_state(id);
    check(unlocked.angular_velocity.y > 3.f, "removing the motor restores free rotation");
    check(unlocked.velocity.x > 1.f, "removing the motor preserves pending linear forces");

    check(world.update_body_motion(id, {}, {}, {}, {}, 4), "lock the existing body on its local Y axis");
    check(world.body_state(id).angular_velocity == framework::Vector3{}, "applying a lock clears angular velocity");
    check(world.body_state(id).position == unlocked.position, "applying a lock preserves the current pose");

    const framework::Quaternion upright{0, 0, 0, 1};
    check(world.update_body_motion(id, {}, {}, state.angular_velocity, {}, {}, upright),
          "rotate the existing body and reanchor its lock");
    const auto rotated = world.body_state(id);
    check(std::abs(rotated.position.x - unlocked.position.x) < .001f &&
          std::abs(rotated.position.y - unlocked.position.y) < .001f &&
          std::abs(rotated.position.z - unlocked.position.z) < .001f,
          "rotation preserves the prim origin with an offset center of mass");
    check(rotated.rotation == upright && rotated.velocity == unlocked.velocity,
          "rotating a live body applies its pose and preserves linear velocity");
    world.step();
    const auto reanchored = world.body_state(id).angular_velocity;
    check(std::abs(reanchored.y) < .01f && reanchored.x > 1.f && reanchored.z > .5f,
          "the motor follows the new local axis after rotation");
}

static void check_asset_mesh_centroid(const std::filesystem::path& path) {
    scene::Terrain terrain;
    physics::UbodeWorld world(path, terrain);
    const framework::Uuid id("88f8f560-a0e4-4e08-bc88-c8592dc9a71d");
    auto mesh = std::make_shared<physics::AssetPhysicsMesh>();
    mesh->vertices = {{0, -.25f, 0}, {1, -.25f, 0}, {1, .25f, 0}, {0, .25f, 0}, {10, 0, 0}};
    mesh->indices = {0, 1, 2, 0, 2, 3, 4, 4, 0};
    physics::UbodePrimPart part{{1, 1, 1}, {}, {0, 0, 0, 1}, 2.f};
    part.type = framework::PrimType::Sculpt;
    part.asset_mesh = mesh;
    physics::UbodeBodyState state;
    state.position = {128, 128, 30};
    state.angular_velocity = {0, 0, 2};
    world.add_dynamic_linkset(id, {&part, 1}, state);
    check(world.body_state(id).position == state.position, "asset body starts at its prim origin");
    world.step(15);
    const auto moved = world.body_state(id);
    check(moved.position.x > 128.04f && moved.position.y < 127.8f && moved.position.y > 127.65f,
          "asset prim rotates around its indexed geometry centroid");
}

static void check_prim_contact_materials(const std::filesystem::path& path) {
    struct Motion {
        float peak_upward_speed;
        float final_horizontal_speed;
    };
    scene::Terrain terrain;
    terrain.clear_land();
    const auto run = [&](framework::PrimContactMaterial material,
                         const physics::UbodeBodyState& initial) {
        physics::UbodeWorld world(path, terrain);
        const auto platform = framework::Uuid::random();
        const auto moving = framework::Uuid::random();
        world.add_static_prim(platform, framework::PrimType::Box, {20, 20, 1},
                              {128, 128, 23}, {0, 0, 0, 1}, 0, 0.f, {}, {}, {0.f, 0.f});
        const physics::UbodeColliderProperties platform_properties{{1.f, 1.f}};
        check(world.update_collider_properties(platform, {&platform_properties, 1}), "update a static collider");

        // The child hits the platform first. The root has a different material.
        physics::UbodePrimPart parts[] = {
            {{1, 1, 1}, {0, 0, 2}, {0, 0, 0, 1}, 1.f},
            {{1, 1, 1}, {}, {0, 0, 0, 1}, 1.f}
        };
        parts[0].contact_material = {0.f, 0.f};
        parts[1].contact_material = {0.f, 0.f};
        // Hold the linkset upright so the friction measurement isolates sliding from tipping.
        physics::UbodePrimForces forces;
        forces.rotation_axis_locks = 0x0e;
        world.add_dynamic_linkset(moving, parts, initial, forces);
        const physics::UbodeColliderProperties properties[] = {{{0.f, 0.f}}, {material}};
        check(world.update_collider_properties(moving, properties), "update a physical linkset's child collider");

        float peak_upward_speed = 0.f;
        for (int frame = 0; frame < 20; ++frame) {
            world.step();
            peak_upward_speed = std::max(peak_upward_speed, world.body_state(moving).velocity.z);
        }
        return Motion{peak_upward_speed, world.body_state(moving).velocity.x};
    };

    physics::UbodeBodyState falling;
    falling.position = {128, 128, 24.3f};
    falling.velocity = {0, 0, -8};
    const auto inelastic = run({0.f, 0.f}, falling);
    const auto elastic = run({0.f, 1.f}, falling);
    check(elastic.peak_upward_speed > inelastic.peak_upward_speed + 3.f,
          "prim restitution affects rebound speed");

    physics::UbodeBodyState sliding;
    sliding.position = {128, 128, 24};
    sliding.velocity = {3, 0, 0};
    const auto frictionless = run({0.f, 0.f}, sliding);
    const auto rough = run({1.f, 0.f}, sliding);
    check(frictionless.final_horizontal_speed > 2.5f && std::abs(rough.final_horizontal_speed) < 1.f,
          "prim friction slows tangential motion");
}

static void check_prim_look_at_updates(const std::filesystem::path& path) {
    scene::Terrain terrain;
    physics::UbodeWorld world(path, terrain);
    const auto id = framework::Uuid::random();
    const physics::UbodePrimPart part{{1, 1, 1}, {}, {0, 0, 0, 1}, 2.f};
    physics::UbodeBodyState state;
    state.position = {128, 128, 30};
    physics::UbodePrimForces forces;
    forces.gravity_modifier = 0.f;
    forces.look_at = {{0, 0, .70710678f, .70710678f}, 1.f};
    world.add_dynamic_linkset(id, {&part, 1}, state, forces);
    world.step();
    check(world.body_state(id).angular_velocity == framework::Vector3{},
          "look-at waits for a scene update");
    world.update_prim_look_at();
    const float first_speed = world.body_state(id).angular_velocity.z;
    check(first_speed > 4.f, "look-at scene update sets angular velocity");
    world.step(4);
    const float integrated_speed = world.body_state(id).angular_velocity.z;
    check(integrated_speed > first_speed * .98f, "physics substeps keep the scene's angular command");
    world.update_prim_look_at();
    check(world.body_state(id).angular_velocity.z < integrated_speed * .9f,
          "the next scene update recomputes the angular command");
}

static void check_prim_water_hover(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    world.set_water_height(26.f);
    const auto id = framework::Uuid::random();
    const physics::UbodePrimPart part{{1, 1, 1}, {}, {0, 0, 0, 1}, 2.f};
    physics::UbodeBodyState state;
    state.position = {128, 128, 25};
    physics::UbodePrimForces forces;
    forces.hover = {3.f, .25f, true};
    world.add_dynamic_linkset(id, {&part, 1}, state, forces);
    world.step(125);
    const auto hovering = world.body_state(id);
    check(std::abs(hovering.position.z - 29.f) < .03f && std::abs(hovering.velocity.z) < .12f,
          "prim hover uses water when it is above terrain");
}

static void check_prim_impulses(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    const auto id = framework::Uuid::random();
    const physics::UbodePrimPart part{{1, 1, 1}, {}, {0, 0, 0, 1}, 2.f};
    physics::UbodeBodyState initial;
    initial.position = {128, 128, 30};
    physics::UbodePrimForces forces;
    forces.gravity_modifier = 0.f;
    world.add_dynamic_linkset(id, {&part, 1}, initial, forces);
    world.apply_prim_impulse(id, {4, 0, 0}, false);
    world.apply_prim_impulse(id, {0, 0, 1}, true);
    world.step();
    const auto first = world.body_state(id);
    check(std::abs(first.velocity.x - 2.f) < .02f &&
              std::abs(first.angular_velocity.z - 3.f) < .03f,
          "prim impulses change linear and angular momentum");
    world.step();
    const auto second = world.body_state(id);
    check(std::abs(second.velocity.x - first.velocity.x) < .02f &&
              std::abs(second.angular_velocity.z - first.angular_velocity.z) < .03f,
          "prim impulses apply for one solver step");

    physics::UbodeWorld avatar(path, terrain);
    avatar.add_avatar({128, 128, 30}, {.45f, .6f, 1.9f});
    avatar.push_avatar({20, 0, 0});
    avatar.step();
    check(avatar.snapshot().body_velocity.x > 1.f, "avatar push applies momentum");
}

static void check_avatar_buoyancy(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld avatar(path, terrain);
    const framework::Vector3 start{128, 128, 30};
    const framework::Vector3 size{.45f, .6f, 1.9f};
    avatar.add_avatar(start, size);
    avatar.set_buoyancy(1.f);
    avatar.step(20);
    check(std::abs(avatar.snapshot().body_position.z - start.z) < .05f,
          "neutral buoyancy offsets gravity for an airborne avatar");

    avatar.remove_avatar();
    avatar.add_avatar(start, size);
    avatar.step(20);
    check(avatar.snapshot().body_position.z < start.z - .3f,
          "a recreated avatar actor starts with normal gravity");
}

static void check_avatar_contact_planes(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    for (const bool upper_first : {false, true}) {
        physics::UbodeWorldOptions options;
        options.report_prim_collisions = true;
        physics::UbodeWorld first(path, terrain, options);
        physics::UbodeWorld second(first);
        const framework::Vector3 size{.45f, .6f, 1.9f};
        const framework::Vector3 lower{128, 128, 25};
        const framework::Vector3 upper{128, 128, 26.7f};
        const physics::PrimCollisionActor first_actor{framework::Uuid::random(), 1};
        const physics::PrimCollisionActor second_actor{framework::Uuid::random(), 2};
        first.add_avatar(upper_first ? upper : lower, size, true, first_actor);
        second.add_avatar(upper_first ? lower : upper, size, true, second_actor);
        physics::UbodeWorld* peers[] = {&second};
        first.step_with(peers, 5);
        const auto reports = first.take_prim_collision_reports();
        check(std::ranges::any_of(reports, [&](const auto& report) {
            return report.avatar_contacts.contains({first_actor, second_actor}) &&
                   report.avatar_contacts.contains({second_actor, first_actor});
        }), "avatar collision reports both recipients");
        const auto top = (upper_first ? first : second).snapshot();
        const auto bottom = (upper_first ? second : first).snapshot();
        check(top.collision_plane[2] > .99f && top.collision_plane[3] > lower.z &&
                  top.collision_plane[3] < upper.z,
              "upper avatar reports the contact plane under its feet");
        check(bottom.collision_plane == std::array<float, 4>{0, 0, 0, 1},
              "lower avatar has no foot contact from the same collision");
    }
}

static void check_avatar_box_contacts(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    physics::UbodeWorld avatar(world);
    const framework::Uuid platform("88f8f560-a0e4-4e08-bc88-c8592dc9a70f");
    world.add_static_box(platform, {4, 4, 1}, {128, 128, 23}, {0, 0, 0, 1});
    avatar.add_avatar({128, 128, 27}, {.45f, .6f, 1.9f});
    check(avatar.snapshot().collision_plane == std::array<float, 4>{0, 0, 0, 1},
          "airborne avatar starts with the default collision plane");
    physics::UbodeWorld* peers[] = {&avatar};
    world.step_with(peers, 120);
    const auto standing = avatar.snapshot();
    check(std::abs(standing.body_position.z - 24.45f) < .05f && standing.colliding &&
              !standing.colliding_ground,
          "avatar stands on the box with an object contact");
    check(standing.collision_plane[2] > .99f && std::abs(standing.collision_plane[3] - 23.5f) < .05f,
          "avatar collision plane follows the platform top");
    world.remove_box(platform);
    world.step_with(peers, 120);
    const auto on_terrain = avatar.snapshot();
    check(on_terrain.body_position.z < 22.1f && on_terrain.colliding_ground,
          "avatar falls to terrain when a supporting box is removed");
    if (!(on_terrain.collision_plane[2] > .99f &&
          std::abs(on_terrain.collision_plane[3] - terrain.height_at(128, 128)) < .05f))
        throw std::runtime_error("Expected a terrain collision plane, got Z=" +
                                 std::to_string(on_terrain.collision_plane[2]) + " D=" +
                                 std::to_string(on_terrain.collision_plane[3]));

    avatar.remove_avatar();
    avatar.add_avatar({128, 128, 26}, {.45f, .6f, 1.9f});
    physics::UbodeBodyState moving;
    moving.position = {126, 128, 26};
    moving.velocity = {8, 0, 0};
    world.add_dynamic_box(platform, {1, 1, 1}, 4, moving);
    world.step_with(peers, 15);
    check(avatar.snapshot().body_position.x > 128.1f && world.body_state(platform).velocity.x < 7.f,
          "moving box transfers momentum to the avatar");
}

static void check_avatar_volume_detect(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorldOptions options;
    options.report_prim_collisions = true;
    physics::UbodeWorld world(path, terrain, options);
    const auto sensor = framework::Uuid::random();
    const physics::PrimCollisionActor avatar{framework::Uuid::random(), 1};
    world.add_static_prim(sensor, framework::PrimType::Box, {4, 4, 1}, {128, 128, 23},
                          {0, 0, 0, 1});
    world.add_avatar({128, 128, 27}, {.45f, .6f, 1.9f}, false, avatar);
    world.step(120);
    check(world.snapshot().body_position.z > 24.f, "solid collider supports the avatar");
    world.take_prim_collision_reports();

    const physics::UbodeColliderProperties detector{{}, true};
    check(world.update_collider_properties(sensor, {&detector, 1}), "enable volume detection on a live collider");
    world.step(120);

    const auto reports = world.take_prim_collision_reports();
    check(std::ranges::any_of(reports, [&](const auto& report) {
        return report.objects.contains({sensor, avatar});
    }), "volume detector reports an avatar crossing it");
    const auto on_terrain = world.snapshot();
    check(on_terrain.body_position.z < 22.1f && on_terrain.colliding_ground,
          "volume detector allows the avatar to fall to terrain");

    const physics::UbodeColliderProperties solid;
    check(world.update_collider_properties(sensor, {&solid, 1}), "disable volume detection on the same collider");
    world.remove_avatar();
    world.add_avatar({128, 128, 27}, {.45f, .6f, 1.9f}, false, avatar);
    world.step(120);
    check(world.snapshot().body_position.z > 24.f, "disabling volume detection restores solid contact");

    const auto moving = framework::Uuid::random();
    physics::UbodeBodyState motion;
    motion.position = {128, 128, 50};
    world.add_dynamic_box(moving, {1, 1, 1}, 2.f, motion);
    world.apply_prim_impulse(moving, {6, 0, 0}, false);
    check(world.update_collider_properties(moving, {&detector, 1}), "enable detection on a live body");
    world.step();
    check(world.body_state(moving).velocity.x > 2.f, "switching collision mode preserves a pending impulse");
}

int main(int argc, char** argv) {
    try {
        check(argc >= 2, "explicit library path argument required");
        if (std::string_view(argv[1]) == "--render-mesh") {
            check_mesh_asset_decoder(true);
            return 0;
        }
        std::filesystem::path path = argv[1];
        if (argc >= 3) {
            probe(path, std::stoi(argv[2]));
            return 0;
        }
        check_dynamic_boxes(path);
        check_mesh_asset_decoder();
        check_sphere_contacts(path);
        check_cylinder_mesh(path);
        check_linear_path_mesh(path);
        check_polygon_prims(path);
        check_ring_mesh(path);
        check_ellipsoid_mesh(path);
        check_compound_box_motion(path);
        check_rotation_axis_locks(path);
        check_asset_mesh_centroid(path);
        check_prim_contact_materials(path);
        check_prim_look_at_updates(path);
        check_prim_water_hover(path);
        check_prim_impulses(path);
        check_avatar_buoyancy(path);
        check_avatar_contact_planes(path);
        check_avatar_box_contacts(path);
        check_avatar_volume_detect(path);
        scene::Terrain terrain;
        terrain.clear_land();
        std::atomic<bool> failed = false;
        std::vector<std::thread> workers;
        for (int n = 0; n < 4; n++)
            workers.emplace_back([&] {
                try {
                    for (int round = 0; round < 20; round++) {
                        physics::UbodeWorld local(path, terrain);
                        local.add_avatar({128, 128, 23}, {.45f, .6f, 1.9f});
                        local.step(3);
                        check(std::isfinite(local.snapshot().body_position.z), "concurrent actual solver");
                    }
                } catch (...) {
                    failed = true;
                }
            });
        for (auto& thread : workers)
            thread.join();
        check(!failed, "concurrent global init/refcount/thread data");
        physics::UbodeWorld world(path, terrain);
        check(world.configuration().find("OS0.13.4") != std::string::npos, "ABI configuration");
        check(world.terrain_height(-10, -20) == 21 && world.terrain_height(300, 400) == 21, "padded terrain");
        world.add_avatar({128, 128, 23}, {.45f, .6f, 1.9f});
        world.step(100);
        auto start = world.snapshot();
        world.set_target_velocity({4.096f, 0, 0});
        world.step(100);
        auto moving = world.snapshot();
        check(moving.position.x > start.position.x + 3, "actual native walking");
        check(std::isfinite(moving.position.z) && moving.position.z > 21, "terrain support");
        world.set_flying(true);
        world.set_target_velocity({0, 0, 4});
        world.step(50);
        check(world.snapshot().position.z > moving.position.z + 1, "source flying controller");
        const auto before_momentum = world.snapshot().body_position;
        world.set_momentum({3, 0, 0});
        world.step(1);
        check(world.snapshot().body_position.x > before_momentum.x, "transferred momentum moves the native body");
        bool second = false;
        try {
            world.add_avatar({1, 2, 23}, {.45f, .6f, 1.9f});
        } catch (const std::logic_error&) {
            second = true;
        }
        check(second, "single avatar profile");
        bool wrong_thread = false;
        std::thread other([&] {
            try {
                world.snapshot();
            } catch (const std::logic_error&) {
                wrong_thread = true;
            }
        });
        other.join();
        check(wrong_thread, "thread ownership");
        {
            physics::UbodeWorld shared(path, terrain);
            shared.add_avatar({1, 2, 23}, {.45f, .6f, 1.9f});
            shared.step();
        }
        world.step();
        check(std::isfinite(world.snapshot().position.x), "shared library lifetime");
        world.remove_avatar();
        world.remove_avatar();
        world.add_avatar({5, 6, 23}, {.45f, .6f, 1.9f});
        world.step();
        check(world.snapshot().position.x == 5 && !world.snapshot().running && !world.snapshot().flying,
              "source avatar remove/relogin reset");
        world.step(100);
        auto before_jump = world.snapshot();
        world.avatar_jump();
        world.step(5);
        check(world.snapshot().body_position.z > before_jump.body_position.z + .3f,
              "actual source AvatarJump impulse");
        world.set_orientation({.1f, .2f, .3f, .4f});
        auto rotated = world.snapshot();
        check(rotated.orientation.x > 0 && rotated.body_orientation.x == 0 && rotated.body_orientation.y == 0,
              "source full normalized orientation and native yaw");
        world.set_orientation({0, 0, 0, 0});
        check(world.snapshot().orientation == framework::Quaternion{0, 0, 0, 1},
              "source zero normalization identity");
        bool invalid_yaw = false;
        try {
            world.set_orientation({1, 0, 0, 0});
        } catch (const std::invalid_argument&) {
            invalid_yaw = true;
        }
        check(invalid_yaw && world.snapshot().orientation == framework::Quaternion{0, 0, 0, 1},
              "zero-yaw native-abort profile rejected without mutation");
        std::cout << "PASS: actual ubODE ABI, walking, terrain, flying, thread/profile/lifetime checks\n";
        auto host = std::make_unique<physics::UbodeWorld>(path, terrain);
        physics::UbodeWorld peer(*host);
        check(peer.avatar_mass() == 0.f, "absent avatar has no actor mass");
        host->add_avatar({128, 128, 25}, {.45f, .6f, 1.9f}, true);
        peer.add_avatar({140, 140, 25}, {.45f, .6f, 1.9f}, true);
        check(std::abs(peer.avatar_mass() - 1.539f) < .0001f, "avatar reports the solver mass");
        const auto before_resize = peer.snapshot();
        peer.resize_avatar({.45f, .6f, 2.3f});
        check(std::abs(peer.avatar_mass() - 1.863f) < .0001f, "appearance resize updates avatar mass");
        const auto after_resize = peer.snapshot();
        check(std::abs(after_resize.body_position.z - before_resize.body_position.z - .2f) < .0001f &&
                  after_resize.flying == before_resize.flying &&
                  after_resize.orientation == before_resize.orientation,
              "live resize preserves feet position, flight, and orientation");
        peer.resize_avatar({.45f, .6f, 1.9f});
        host->set_target_velocity({4, 0, 0});
        peer.set_target_velocity({0, 4, 0});
        physics::UbodeWorld* peers[] = {&peer};
        host->step_with(peers, 100);
        check(host->snapshot().position.x > 130 && peer.snapshot().position.y > 142,
              "shared world updates both avatar controllers in the same step");
        host.reset();
        const auto before = peer.snapshot().position.y;
        peer.step(50);
        check(peer.snapshot().position.y > before, "peer keeps the shared world alive after host removal");
        const auto revision = terrain.revision();
        terrain.set(140, 140, 31.f);
        check(terrain.revision() != revision, "terrain edit advances the physics revision");
        physics::UbodeWorld another_peer(peer);
        peer.update_terrain(terrain);
        check(peer.terrain_height(140, 140) == 31.f && another_peer.terrain_height(140, 140) == 31.f,
              "terrain refresh updates existing peer controllers");
        terrain.set(140, 140, 21.f);
        physics::UbodeWorld first(path, terrain);
        physics::UbodeWorld second_avatar(first);
        first.add_avatar({128, 128, 25}, {.45f, .6f, 1.9f}, true);
        second_avatar.add_avatar({128.1f, 128, 25}, {.45f, .6f, 1.9f}, true);
        physics::UbodeWorld* collision_peers[] = {&second_avatar};
        first.step_with(collision_peers, 10);
        check(second_avatar.snapshot().body_position.x - first.snapshot().body_position.x > .2f,
              "shared solver separates overlapping avatar capsules");
        check(first.snapshot().collision_plane == std::array<float, 4>{0, 0, 0, 1} &&
                  second_avatar.snapshot().collision_plane == std::array<float, 4>{0, 0, 0, 1},
              "side contacts do not replace either avatar's collision plane");
        physics::UbodeWorld water_host(path, terrain, {.0909f, 35.f});
        physics::UbodeWorld water_avatar(water_host);
        physics::UbodeWorld ground_avatar(path, terrain, {.0909f, 35.f});
        for (auto* avatar : {&water_avatar, &ground_avatar})
            avatar->add_avatar({128, 128, 30}, {.45f, .6f, 1.9f}, true);
        water_avatar.set_hover(3.f, 1.f, physics::HoverReference::GroundAndWater, true);
        ground_avatar.set_hover(3.f, 1.f, physics::HoverReference::Ground, true);
        water_avatar.step(10);
        ground_avatar.step(10);
        check(water_avatar.snapshot().body_position.z > 30.f &&
                  ground_avatar.snapshot().body_position.z < 30.f,
              "water-relative hover uses the configured level inherited by a peer controller");
        water_host.set_water_height(5.f);
        water_avatar.remove_avatar();
        water_avatar.add_avatar({128, 128, 30}, {.45f, .6f, 1.9f}, true);
        water_avatar.set_hover(3.f, 1.f, physics::HoverReference::GroundAndWater, true);
        water_avatar.step(10);
        check(water_avatar.snapshot().body_position.z < 30.f,
              "an existing peer controller uses the updated water level");
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
