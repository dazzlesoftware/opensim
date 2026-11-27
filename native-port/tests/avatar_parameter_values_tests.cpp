// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/avatar_parameter_values.hpp"
#include "parameter_value_canonical.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace opensim;
using namespace framework;
using namespace scene;
using namespace script;

namespace {
float rotation_component(unsigned seed, int mode, unsigned component) {
    if (mode == 8)
        return std::bit_cast<float>(0x3f000000u | ((seed * 104729u + component * 174763u) & 0x007fffffu));
    const auto index = (seed >> (component * 2)) & 3u;
    if (mode == 9)
        return std::bit_cast<float>(std::array{0x7fc00000u, 0xffc00000u, 0x7f800000u, 0xff800000u}[index]);
    if (mode == 10)
        return std::bit_cast<float>(std::array{0x7f800001u, 0xff800001u, 0x7fc12345u, 0xffc54321u}[index]);
    return std::bit_cast<float>(std::array{0x7f7fffffu, 0xff7fffffu, 0x00000001u, 0x80000001u}[index]);
}

std::string observe(int seed, int legacy, int seat_mode, int appearance_mode, int quaternion_mode, int code, int face,
                    int target_length) {
    AvatarParameterState state;
    state.name = "Avatar " + std::to_string(seed);
    state.offset_position = {seed / 10.f, -1.25f, 2.5f};
    state.absolute_position = {99.25f, -20.5f, seed / 8.f};
    state.world_rotation = {.125f, .25f, .5f, 1.f};
    state.legacy_sit_offsets = legacy != 0;
    switch (quaternion_mode) {
    case 1:
        state.rotation = {0, 0, .7071f, .7071f};
        break;
    case 2:
        state.rotation = {1, 2, 3, 4};
        break;
    case 3:
        state.rotation = {};
        break;
    case 4:
        state.rotation = {std::numeric_limits<float>::quiet_NaN(), 0, 0, 1};
        break;
    case 5:
        state.rotation = {std::numeric_limits<float>::infinity(), 0, 0, 1};
        break;
    case 6:
        state.rotation = {0, 0, 0, -2};
        break;
    case 7:
        state.rotation = {1.0e-20f, 0, 0, 1.0e-20f};
        break;
    case 8:
    case 9:
    case 10:
    case 11:
        state.rotation = {rotation_component(seed, quaternion_mode, 0), rotation_component(seed, quaternion_mode, 1),
                          rotation_component(seed, quaternion_mode, 2), rotation_component(seed, quaternion_mode, 3)};
        break;
    default:
        state.rotation = {0, 0, 0, 1};
        break;
    }
    std::string trace;
    state.seat = [&] {
        trace += "S;";
        if (seat_mode == 4)
            throw PrimitiveParameterValueError("Seat lookup failed");
        return AvatarParameterSeat{
            seat_mode != 1, seat_mode != 2, seat_mode != 3, {15.125f, 32.75f, -3.5f}, {.125f, -.25f, .5f, .75f}};
    };
    state.appearance = [&] {
        trace += "A;";
        if (appearance_mode == 2)
            throw PrimitiveParameterValueError("Appearance failed");
        return AvatarParameterAppearance{appearance_mode != 1, {.45f, .6f, seed / 64.f}, seed / 32.f - 2.f};
    };
    LslList rules, result{{std::string("prefix")}};
    const auto add = [&](int property) {
        rules.items.emplace_back(static_cast<std::int32_t>(property));
        if ((property >= 17 && property <= 20) || property == 22 || property == 25 ||
            (property >= 36 && property <= 38))
            rules.items.emplace_back(static_cast<std::int32_t>(face));
    };
    if (code == 0)
        for (const auto property : {2, 3, 4, 5, 6, 7, 8, 9, 17, 18, 19, 20, 21, 22, 23, 25, 26, 27, 28, 29, 33, 44})
            add(property);
    else if (code == 34) {
        rules.items.emplace_back(std::int32_t{34});
        for (int i = 0; i < target_length; ++i)
            rules.items.emplace_back(static_cast<std::int32_t>(i == 0 ? face : i == 1 ? 27 : 29));
    } else if (code != -1)
        add(code);
    try {
        const auto tail = read_primitive_avatar_parameters(state, rules, result);
        return test::parameter_value_canonical(result) + "|" + test::parameter_value_canonical(tail) + "|" + trace;
    } catch (const PrimitiveParameterValueError &error) {
        return test::parameter_value_canonical(result) + "|!" + error.source_type + ":" + error.what() + "|" + trace;
    }
}

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **) {
    try {
        if (argc > 1) {
            int seed, legacy, seat, appearance, quaternion, code, face, length;
            while (std::cin >> seed >> legacy >> seat >> appearance >> quaternion >> code >> face >> length)
                std::cout << observe(seed, legacy, seat, appearance, quaternion, code, face, length) << "\n";
            return 0;
        }
        check(observe(0, 0, 1, 1, 0, 27, 0, 0).ends_with("||S;"),
              "A name query looks up the seat without dereferencing a missing seat or appearance");
        check(observe(0, 0, 1, 0, 0, 6, 0, 0).find("System.NullReferenceException") != std::string::npos,
              "World position retains the source missing-seat exception");
        check(observe(0, 1, 0, 1, 3, 33, 0, 0).ends_with("||S;"),
              "Legacy local position does not inspect appearance or normalize rotation");
        check(observe(93, 0, 0, 0, 3, 33, 0, 3) ==
                  "s707265666978;vfff80000000000007ff80000000000007ff8000000000000;||S;A;",
              "Zero-rotation local offsets preserve managed NaN operand signs");
        check(observe(93, 0, 0, 0, 3, 6, 0, 3) ==
                  "s707265666978;vfff8000000000000fff8000000000000fff8000000000000;||S;A;",
              "Seated world offsets retain SDK coefficient-before-vector operand order");
        check(observe(0, 0, 3, 1, 0, 6, 0, 0).ends_with("||S;"),
              "A seat with a missing root uses AbsolutePosition without appearance access");
        check(observe(0, 0, 0, 0, 0, 17, 21, 0) == "s707265666978;||S;",
              "Avatar texture queries exclude face 21 rather than using the appearance texture array size");
        check(observe(0, 0, 0, 0, 0, 18, -2, 0).find(";v") != std::string::npos,
              "Avatar color queries retain default values even for negative single faces");
        check(observe(0, 0, 4, 0, 0, -1, 0, 0).find("Seat lookup failed") != std::string::npos,
              "An empty rule list still performs the source seat lookup");
        std::cout << "PASS: avatar parameter defaults, null boundaries, and seat lookup ordering\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
