#include "opensim/scene/pos_physics.hpp"
#include <bit>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace opensim;
using namespace scene;
static std::string h(float f) {
    std::ostringstream s;
    s << std::hex << std::bit_cast<std::uint32_t>(f);
    return s.str();
}
static std::string v(framework::Vector3 x) {
    return h(x.x) + "," + h(x.y) + "," + h(x.z);
}
static std::string probe(int seed, int mode) {
    PosPhysicsScene scene;
    std::vector<float> heights(65536);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            heights[y * 256 + x] = 21.f + float((x * 17 + y * 31) % 23) * .03125f;
    scene.set_terrain(heights);
    framework::Vector3 p{seed * .25f, 2, mode & 4 ? 21.f : 25.f};
    if (mode & 8)
        p.x = -.1f;
    if (mode & 16)
        p.y = 255.99f;
    auto c = scene.add_avatar(p, {1, 2, 3}, {.45f, .6f, 1.9f}, mode & 32);
    if (mode & 64)
        c->set_size({.45f, .6f, 1.9f});
    if (mode & 128)
        c->set_velocity({2, -3, 4});
    if (mode & 256)
        scene.add_prim(p, {1, 1, 2},
                       mode & 512 ? framework::Quaternion{.1f, .2f, .3f, .4f}
                                  : framework::Quaternion{0, 0, 0, 1});
    float dt = mode % 4 == 0 ? 0.f : mode % 4 == 1 ? .016f : mode % 4 == 2 ? .25f : -.125f;
    int callbacks = 0;
    c->on_terse_update = [&] { callbacks++; };
    float fps = 0;
    std::string error;
    try {
        for (int i = 0; i < (mode & 1024 ? 5 : 1); i++)
            fps = scene.simulate(dt);
    } catch (const std::out_of_range&) {
        error = "EX;";
    }
    return error + v(c->position) + "|" + v(c->velocity) + "|" + v(c->target_velocity) + "|" + v(c->size()) +
           "|" + std::to_string(c->is_colliding) + "|" + std::to_string(c->terse_update_count) + "|" +
           std::to_string(callbacks) + "|" + h(fps);
}
int main(int argc, char**) {
    try {
        if (argc > 1) {
            int seed, mode;
            while (std::cin >> seed >> mode)
                std::cout << probe(seed, mode) << "\n";
            return 0;
        }
        PosPhysicsScene scene;
        scene.set_terrain(std::vector<float>(65536, 21));
        auto c = scene.add_avatar({2, 3, 30}, {1, 2, 3}, {.45f, .6f, 1.9f}, false);
        if (c->size() != framework::Vector3{} || c->target_velocity != framework::Vector3{})
            throw std::runtime_error("source AddAvatar ignores size and velocity");
        c->set_size({.4f, .6f, 2});
        c->set_velocity({2, 3, 0});
        if (scene.simulate(.25f) != 1 || c->position.x != 2 || c->position.y != 3 || c->position.z >= 30)
            throw std::runtime_error("source X/Y clamp and gravity");
        scene.remove_avatar(c);
        auto before = c->position;
        scene.simulate(1);
        if (c->position != before)
            throw std::runtime_error("remove");
        std::cout << "PASS: POS avatar defaults, old-position clamps, gravity and lifecycle\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
