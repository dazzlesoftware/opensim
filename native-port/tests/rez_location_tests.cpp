#include "opensim/scene/rez_location.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace opensim;
using namespace scene;
static std::string hex(float f) {
    std::ostringstream s;
    s << std::hex << std::bit_cast<std::uint32_t>(f);
    return s.str();
}
static std::string vec(framework::Vector3 v) {
    return hex(v.x) + "," + hex(v.y) + "," + hex(v.z);
}
static std::string probe(int seed, int mode) {
    RezLocationRequest r;
    r.ray_start = {seed * .25f, 3, 30};
    r.ray_end = {seed * .5f, -7, 23};
    r.scale = {.5f, 2, 3};
    r.bypass_raycast = mode & 255;
    r.ray_end_is_intersection = mode & 1 ? 1 : 2;
    r.front_faces_only = mode & 2;
    r.face_center = mode & 4;
    if (mode & 2)
        r.ray_end = r.ray_start;
    if (mode & 4)
        r.target = framework::Uuid("00112233-4455-6677-8899-aabbccddeeff");
    if (mode & 256)
        r.ray_end.z = 10;
    if (mode & 512)
        r.scale = {-2, -3, -4};
    RezLocationBackend b;
    std::string trace;
    auto intersection = [&](bool hit) {
        return RezIntersection{hit,
                               {seed * .125f, 2, 15},
                               {.25f, -.5f, 1},
                               mode & 2048   ? framework::Vector3{}
                               : mode & 1024 ? framework::Vector3{1, 0, 0}
                                             : framework::Vector3{1, 1, float(mode & 512 ? 1 : 0)}};
    };
    if (mode & 8)
        b.filtered_raycast = [&](auto start, auto direction, float length, int count, std::uint16_t flags) {
            trace += "P:" + vec(start) + ":" + vec(direction) + ":" + hex(length) + ":" +
                     std::to_string(count) + ":" + std::to_string(flags) + ";";
            if (!(mode & 16))
                return std::vector<RezContact>{};
            return std::vector<RezContact>{{11, {1, 2, seed * .25f}, {1, 0, 0}},
                                           {0, {4, 5, 16}, {0, 0, 1}},
                                           {22, {7, 8, 18}, {0, 1, 0}}};
        };
    b.part_uuid = [&](std::uint32_t id) -> std::optional<framework::Uuid> {
        trace += "L:" + std::to_string(id) + ";";
        if (id == 0)
            return {};
        if (id == 22 && (mode & 128))
            return r.target;
        return framework::Uuid("11223344-5566-7788-99aa-bbccddeeff00");
    };
    b.target_intersection = [&](auto, auto start, auto direction, auto q, bool front,
                                bool center) -> std::optional<RezIntersection> {
        trace += "T;";
        if (!(mode & 32))
            return {};
        trace += "O:" + vec(start) + ":" + vec(direction) + ":" + hex(q.w) + ":" + std::to_string(front) +
                 ":" + std::to_string(center) + ";";
        return intersection(mode & 64);
    };
    b.closest_intersection = [&](auto start, auto direction, bool front, bool center) {
        trace += "C:" + vec(start) + ":" + vec(direction) + ":" + std::to_string(front) + ":" +
                 std::to_string(center) + ";";
        return intersection(mode & 128);
    };
    return vec(get_new_rez_location(r, b)) + "|" + trace;
}
int main(int argc, char**) {
    try {
        if (argc > 1) {
            int seed, mode;
            while (std::cin >> seed >> mode)
                std::cout << probe(seed, mode) << "\n";
            return 0;
        }
        const float half_turn = std::sqrt(.5f);
        const framework::Quaternion rotation{0, 0, half_turn, half_turn};
        const auto rotated =
            intersect_rez_box({10, 10, 10}, rotation, {4, 2, 2}, {10, 15, 10}, {0, -1, 0}, true, false);
        if (!rotated.hit || std::abs(rotated.point.y - 12.f) > .001f ||
            std::abs(rotated.normal.y - 1.f) > .001f ||
            rotated.axis_aligned_face_normal != framework::Vector3{1, 0, 0})
            throw std::runtime_error("Rotated rez box did not return its world and local face normals");
        const auto front =
            intersect_rez_box({10, 10, 10}, rotation, {4, 2, 2}, {10, 10, 10}, {0, 1, 0}, true, false);
        const auto exit =
            intersect_rez_box({10, 10, 10}, rotation, {4, 2, 2}, {10, 10, 10}, {0, 1, 0}, false, false);
        if (front.hit || !exit.hit || std::abs(exit.point.y - 12.f) > .001f)
            throw std::runtime_error(
                "Rez box did not respect front-face filtering for a ray starting inside");
        RezLocationRequest r;
        r.ray_end = {1, 2, 30};
        auto p = get_new_rez_location(r, {});
        if (p != framework::Vector3{1, 2, 30.25f})
            throw std::runtime_error("endpoint fallback");
        r.target = framework::Uuid("00112233-4455-6677-8899-aabbccddeeff");
        bool threw = false;
        try {
            get_new_rez_location(r, {});
        } catch (const std::logic_error&) {
            threw = true;
        }
        if (!threw)
            throw std::runtime_error("missing backend");
        std::cout << "PASS: rez fallback and explicit missing-backend failure\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
