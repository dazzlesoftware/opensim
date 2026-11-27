// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/primitive_parameter_rules.hpp"
#include "opensim/script/primitive_parameter_query.hpp"
#include <stdexcept>

namespace opensim::script {
namespace {
bool face_rule(std::int32_t code, bool avatar) {
    switch (code) {
    case 17: // PRIM_TEXTURE
    case 18: // PRIM_COLOR
    case 19: // PRIM_BUMP_SHINY
    case 20: // PRIM_FULLBRIGHT
    case 22: // PRIM_TEXGEN
    case 25: // PRIM_GLOW
        return true;
    case 36: // PRIM_SPECULAR
    case 37: // PRIM_NORMAL
    case 38: // PRIM_ALPHA_MODE
        return !avatar;
    default:
        return false;
    }
}

bool property_rule(std::int32_t code, bool avatar) {
    if (face_rule(code, avatar))
        return true;
    switch (code) {
    case 2:  // PRIM_MATERIAL
    case 3:  // PRIM_PHYSICS
    case 4:  // PRIM_TEMP_ON_REZ
    case 5:  // PRIM_PHANTOM
    case 6:  // PRIM_POSITION
    case 7:  // PRIM_SIZE
    case 8:  // PRIM_ROTATION
    case 9:  // PRIM_TYPE
    case 21: // PRIM_FLEXIBLE
    case 23: // PRIM_POINT_LIGHT
    case 26: // PRIM_TEXT
    case 27: // PRIM_NAME
    case 28: // PRIM_DESC
    case 29: // PRIM_ROT_LOCAL
    case 33: // PRIM_POS_LOCAL
    case 44: // PRIM_REFLECTION_PROBE
        return true;
    case 30: // PRIM_PHYSICS_SHAPE_TYPE
    case 32: // PRIM_OMEGA
    case 35: // PRIM_SLICE
    case 41: // PRIM_SIT_TARGET
    case 42: // PRIM_PROJECTOR
        return !avatar;
    default:
        return false;
    }
}
} // namespace

LslList read_primitive_parameter_rules(const PrimitiveParameterRuleBackend &backend, const LslList &rules,
                                       LslList &result) {
    if (!backend.append)
        throw std::invalid_argument("Primitive parameter property reader is missing");
    std::size_t index = 0;
    while (index < rules.items.size()) {
        const auto code = primitive_rule_integer(rules.items[index++]);
        const auto remaining = rules.items.size() - index;
        if (code == 34) { // PRIM_LINK_TARGET
            if (remaining < (backend.avatar ? 3u : 2u))
                return {};
            LslList tail;
            tail.items.assign(rules.items.begin() + index, rules.items.end());
            return tail;
        }
        if (!property_rule(code, backend.avatar))
            continue;
        std::optional<std::int32_t> face;
        if (face_rule(code, backend.avatar)) {
            if (remaining == 0)
                return {};
            face = primitive_rule_integer(rules.items[index++]);
        }
        backend.append(code, face, result);
    }
    return {};
}
} // namespace opensim::script
