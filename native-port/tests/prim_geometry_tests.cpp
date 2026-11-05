// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/prim_geometry.hpp"
#include <array>
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace opensim::framework;
int main(int argc, char** argv) {
    try {
        std::ios::sync_with_stdio(false);
        std::cin.tie(nullptr);
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            std::array<int, 16> v;
            std::string line;
            while (std::getline(std::cin, line)) {
                const char* cursor = line.data();
                const char* end = cursor + line.size();
                for (auto& item : v) {
                    while (cursor < end && *cursor == ' ')
                        ++cursor;
                    auto parsed = std::from_chars(cursor, end, item);
                    if (parsed.ec != std::errc{})
                        throw std::runtime_error("Incomplete geometry row");
                    cursor = parsed.ptr;
                }
                PrimGeometry s;
                s.sculpt_entry = v[0] != 0;
                s.sculpt_type = static_cast<std::uint8_t>(v[1]);
                s.profile_curve = static_cast<std::uint8_t>(v[2]);
                s.path_curve = static_cast<std::uint8_t>(v[3]);
                s.profile_begin = static_cast<std::uint16_t>(v[4]);
                s.profile_end = static_cast<std::uint16_t>(v[5]);
                s.profile_hollow = static_cast<std::uint16_t>(v[6]);
                s.path_begin = static_cast<std::uint16_t>(v[7]);
                s.path_end = static_cast<std::uint16_t>(v[8]);
                s.path_skew = static_cast<std::int8_t>(v[9]);
                s.path_twist_begin = static_cast<std::int8_t>(v[10]);
                s.path_twist = static_cast<std::int8_t>(v[11]);
                s.path_taper_x = static_cast<std::int8_t>(v[12]);
                s.path_taper_y = static_cast<std::int8_t>(v[13]);
                s.path_radius_offset = static_cast<std::int8_t>(v[14]);
                s.path_revolutions = static_cast<std::uint8_t>(v[15]);
                std::cout << static_cast<int>(get_prim_type(s)) << ' '
                          << static_cast<int>(get_prim_type(s, true)) << ' ' << get_number_of_sides(s)
                          << '\n';
            }
            return 0;
        }
        PrimGeometry s;
        s.profile_curve = 1;
        s.path_curve = 16;
        if (get_number_of_sides(s) != 6)
            throw std::runtime_error("Box");
        s.profile_begin = s.profile_end = 65535;
        if (get_number_of_sides(s) != -2)
            throw std::runtime_error("Raw cut domain was clamped");
        s.sculpt_entry = true;
        s.sculpt_type = 133;
        if (get_number_of_sides(s) != 1)
            throw std::runtime_error("Mesh flag equality");
        s.sculpt_type = 5;
        if (get_number_of_sides(s) != 8)
            throw std::runtime_error("Legacy mesh faces");
        s.profile_curve = 0x31;
        if (get_number_of_sides(s) != -2)
            throw std::runtime_error("Triangle encoded mesh faces");
        std::cout << "PASS: prim geometry getter closure\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
