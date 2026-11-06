#include "opensim/physics/ubode.hpp"
#include "opensim/physics/mesh_asset.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/sdk_llsd_binary.hpp"
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
static void check_mesh_asset_decoder() {
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
                              {128, 128, 23}, {0, 0, 0, 1}, 0, 0.f, {}, {}, {1.f, 1.f});
        physics::UbodePrimPart part{{1, 1, 1}, {}, {0, 0, 0, 1}, 1.f};
        part.contact_material = material;
        world.add_dynamic_linkset(moving, {&part, 1}, initial);

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

static void check_avatar_box_contacts(const std::filesystem::path& path) {
    scene::Terrain terrain;
    terrain.clear_land();
    physics::UbodeWorld world(path, terrain);
    physics::UbodeWorld avatar(world);
    const framework::Uuid platform("88f8f560-a0e4-4e08-bc88-c8592dc9a70f");
    world.add_static_box(platform, {4, 4, 1}, {128, 128, 23}, {0, 0, 0, 1});
    avatar.add_avatar({128, 128, 27}, {.45f, .6f, 1.9f});
    physics::UbodeWorld* peers[] = {&avatar};
    world.step_with(peers, 120);
    const auto standing = avatar.snapshot();
    check(std::abs(standing.body_position.z - 24.45f) < .05f && standing.colliding &&
              !standing.colliding_ground,
          "avatar stands on the box with an object contact");
    world.remove_box(platform);
    world.step_with(peers, 120);
    const auto on_terrain = avatar.snapshot();
    check(on_terrain.body_position.z < 22.1f && on_terrain.colliding_ground,
          "avatar falls to terrain when a supporting box is removed");

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

int main(int argc, char** argv) {
    try {
        check(argc >= 2, "explicit library path argument required");
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
        check_asset_mesh_centroid(path);
        check_prim_contact_materials(path);
        check_avatar_buoyancy(path);
        check_avatar_box_contacts(path);
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
