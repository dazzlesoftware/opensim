// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/avatar_parameter_values.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include <cmath>

namespace opensim::scene {
namespace {
using namespace framework;
using namespace script;

[[noreturn]] void null_reference() {
    throw PrimitiveParameterValueError("Object reference not set to an instance of an object.",
                                       "System.NullReferenceException");
}

Vector3 z_rotation(Quaternion rotation) {
    double magnitude =
        rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
    if (std::abs(1.0 - magnitude) > 0.000001) {
        const auto inverse = static_cast<float>(1.0 / std::sqrt(magnitude));
        rotation.x *= inverse;
        rotation.y *= inverse;
        rotation.z *= inverse;
        rotation.w *= inverse;
    }
    const double x =
        2 * sdk_float_sum(sdk_float_product(rotation.x, rotation.z), sdk_float_product(rotation.y, rotation.w));
    const double y =
        2 * sdk_float_sum(sdk_float_product(-rotation.x, rotation.w), sdk_float_product(rotation.y, rotation.z));
    const float negative_x_squared = sdk_float_product(-rotation.x, rotation.x);
    const float y_squared = sdk_float_product(rotation.y, rotation.y);
    const float z_squared = sdk_float_product(rotation.z, rotation.z);
    const float w_squared = sdk_float_product(rotation.w, rotation.w);
    const double z =
        sdk_float_sum(sdk_float_sum(sdk_float_difference(negative_x_squared, y_squared), z_squared), w_squared);
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

AvatarParameterAppearance appearance(const AvatarParameterState &state) {
    const auto value = state.appearance ? state.appearance() : AvatarParameterAppearance{};
    if (!value.exists)
        null_reference();
    return value;
}

Vector3 local_position(const AvatarParameterState &state) {
    auto position = state.offset_position;
    if (!state.legacy_sit_offsets) {
        const auto axis = z_rotation(state.rotation);
        const float amount = appearance(state).height * 0.02638f * 2.0f;
        position.x -= axis.x * amount;
        position.y -= axis.y * amount;
        position.z -= axis.z * amount;
    }
    return position;
}

Vector3d vector(Vector3 value) {
    return {value.x, value.y, value.z};
}

LslRotation rotation(Quaternion value) {
    return {value.x, value.y, value.z, value.w};
}

void face_values(int code, int face, LslList &result) {
    if (code == 17 && face != -1 && (face < 0 || face >= 21))
        return;
    const int count = face == -1 ? 21 : 1;
    for (int i = 0; i < count; ++i) {
        auto &out = result.items;
        switch (code) {
        case 17:
            out.emplace_back(std::string{});
            out.emplace_back(Vector3d{});
            out.emplace_back(Vector3d{});
            out.emplace_back(0.0);
            break;
        case 18:
            out.emplace_back(Vector3d{});
            out.emplace_back(0.0);
            break;
        case 19:
            out.emplace_back(std::int32_t{0});
            out.emplace_back(std::int32_t{0});
            break;
        case 20:
        case 22:
            out.emplace_back(std::int32_t{0});
            break;
        case 25:
            out.emplace_back(0.0);
            break;
        }
    }
}
} // namespace

script::LslList read_primitive_avatar_parameters(const AvatarParameterState &state, const script::LslList &rules,
                                                 script::LslList &result) {
    using namespace framework;
    using namespace script;
    if (!state.seat)
        throw std::invalid_argument("Avatar parameter seat lookup is missing");
    const auto seat = state.seat();
    PrimitiveParameterRuleBackend backend;
    backend.avatar = true;
    backend.append = [&](int code, std::optional<int> face, LslList &values) {
        if (face) {
            face_values(code, *face, values);
            return;
        }
        auto &out = values.items;
        switch (code) {
        case 2:
            out.emplace_back(std::int32_t{4});
            break;
        case 3:
        case 4:
        case 5:
            out.emplace_back(std::int32_t{0});
            break;
        case 6: {
            if (!seat.part_exists || !seat.group_exists)
                null_reference();
            auto position = state.absolute_position;
            if (seat.root_exists) {
                position = sdk_rotate_vector(local_position(state), seat.root_rotation);
                position = {seat.root_position.x + position.x, seat.root_position.y + position.y,
                            seat.root_position.z + position.z};
            }
            out.emplace_back(vector(position));
            break;
        }
        case 7:
            out.emplace_back(vector(appearance(state).size));
            break;
        case 8:
            out.emplace_back(rotation(state.world_rotation));
            break;
        case 9:
            out.emplace_back(std::int32_t{0});
            out.emplace_back(std::int32_t{0});
            out.emplace_back(Vector3d{0, 1, 0});
            out.emplace_back(0.0);
            out.emplace_back(Vector3d{});
            out.emplace_back(Vector3d{1, 1, 0});
            out.emplace_back(Vector3d{});
            break;
        case 21:
            out.emplace_back(std::int32_t{0});
            out.emplace_back(std::int32_t{0});
            for (int i = 0; i < 4; ++i)
                out.emplace_back(0.0);
            out.emplace_back(Vector3d{});
            break;
        case 23:
            out.emplace_back(std::int32_t{0});
            out.emplace_back(Vector3d{});
            for (int i = 0; i < 3; ++i)
                out.emplace_back(0.0);
            break;
        case 26:
            out.emplace_back(std::string{});
            out.emplace_back(Vector3d{});
            out.emplace_back(1.0);
            break;
        case 27:
            out.emplace_back(state.name);
            break;
        case 28:
            out.emplace_back(std::string{});
            break;
        case 29:
            out.emplace_back(rotation(state.rotation));
            break;
        case 33:
            out.emplace_back(vector(local_position(state)));
            break;
        case 44:
            out.emplace_back(std::int32_t{0});
            for (int i = 0; i < 3; ++i)
                out.emplace_back(0.0);
            break;
        }
    };
    return read_primitive_parameter_rules(backend, rules, result);
}
} // namespace opensim::scene
