#include "opensim/physics/ubode.hpp"
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
int main(int argc, char** argv) {
    try {
        check(argc >= 2, "explicit library path argument required");
        std::filesystem::path path = argv[1];
        if (argc >= 3) {
            probe(path, std::stoi(argv[2]));
            return 0;
        }
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
        host->add_avatar({128, 128, 25}, {.45f, .6f, 1.9f}, true);
        peer.add_avatar({140, 140, 25}, {.45f, .6f, 1.9f}, true);
        const auto before_resize = peer.snapshot();
        peer.resize_avatar({.45f, .6f, 2.3f});
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
