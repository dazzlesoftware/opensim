// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/primitive_parameter_values.hpp"
#include <bit>
#include <cmath>

namespace opensim::scene {
namespace {
using namespace framework;
using namespace script;

Vector3d vector(Vector3 value) {
    return {value.x, value.y, value.z};
}

LslRotation rotation(Quaternion value) {
    return {value.x, value.y, value.z, value.w};
}

void append_type(const PrimitiveBaseShapeCore &shape, LslList &result) {
    auto &out = result.items;
    const auto type = get_prim_type(shape.geometry());
    out.emplace_back(static_cast<std::int32_t>(type));
    if (type == PrimType::Sculpt) {
        out.emplace_back(shape.sculpt_texture.str());
        const auto flags = shape.animesh_enabled() ? shape.sculpt_type | 0x20 : shape.sculpt_type & ~0x20;
        out.emplace_back(static_cast<std::int32_t>(flags));
        return;
    }
    out.emplace_back(static_cast<std::int32_t>(shape.profile_curve() & 0xf0));
    const bool curved =
        type == PrimType::Sphere || type == PrimType::Torus || type == PrimType::Tube || type == PrimType::Ring;
    out.emplace_back(Vector3d{(curved ? shape.path_begin : shape.profile_begin) / 50000.0,
                              1.0 - (curved ? shape.path_end : shape.profile_end) / 50000.0, 0});
    out.emplace_back(shape.profile_hollow / 50000.0);
    out.emplace_back(Vector3d{shape.path_twist_begin / 100.0, shape.path_twist / 100.0, 0});
    if (type == PrimType::Sphere) {
        out.emplace_back(Vector3d{shape.profile_begin / 50000.0, 1.0 - shape.profile_end / 50000.0, 0});
        return;
    }
    out.emplace_back(Vector3d{1.0 - (shape.path_scale_x / 100.0 - 1.0), 1.0 - (shape.path_scale_y / 100.0 - 1.0), 0});
    out.emplace_back(Vector3d{std::bit_cast<std::int8_t>(shape.path_shear_x) / 100.0,
                              std::bit_cast<std::int8_t>(shape.path_shear_y) / 100.0, 0});
    if (!curved)
        return;
    out.emplace_back(Vector3d{shape.profile_begin / 50000.0, 1.0 - shape.profile_end / 50000.0, 0});
    out.emplace_back(Vector3d{shape.path_taper_x / 100.0, shape.path_taper_y / 100.0, 0});
    out.emplace_back(std::round(shape.path_revolutions * 0.015 * 100.0) / 100.0 + 1.0);
    out.emplace_back(shape.path_radius_offset / 100.0);
    out.emplace_back(shape.path_skew / 100.0);
}

void append_material(const PrimitivePartParameterState &state, int code, const TextureFace &face, LslList &result) {
    auto &out = result.items;
    std::shared_ptr<PrimitiveParameterMaterial> material;
    const auto id = face.material_id();
    if (state.material && !id.is_zero())
        material = state.material(id);
    if (!material) {
        if (code == 38) {
            out.emplace_back(std::int32_t{1});
            out.emplace_back(std::int32_t{0});
            return;
        }
        out.emplace_back(Uuid{}.str());
        out.emplace_back(Vector3d{1, 1, 0});
        out.emplace_back(Vector3d{});
        out.emplace_back(0.0);
        if (code == 36) {
            out.emplace_back(Vector3d{1, 1, 1});
            out.emplace_back(std::int32_t{51});
            out.emplace_back(std::int32_t{0});
        }
        return;
    }
    const auto &value = *material;
    if (code == 38) {
        out.emplace_back(static_cast<std::int32_t>(value.alpha_mode));
        out.emplace_back(static_cast<std::int32_t>(value.alpha_cutoff));
        return;
    }
    const bool normal = code == 37;
    const auto texture = normal ? value.normal_map : value.specular_map;
    out.emplace_back(state.material_texture_visible && state.material_texture_visible(texture) ? texture.str()
                                                                                               : Uuid{}.str());
    out.emplace_back(Vector3d{normal ? value.normal_repeat_x : value.specular_repeat_x,
                              normal ? value.normal_repeat_y : value.specular_repeat_y, 0});
    out.emplace_back(Vector3d{normal ? value.normal_offset_x : value.specular_offset_x,
                              normal ? value.normal_offset_y : value.specular_offset_y, 0});
    out.emplace_back(static_cast<double>(normal ? value.normal_rotation : value.specular_rotation));
    if (!normal) {
        constexpr float color_scale = 1.0f / 255.f;
        out.emplace_back(Vector3d{value.specular_color[0] * color_scale, value.specular_color[1] * color_scale,
                                  value.specular_color[2] * color_scale});
        out.emplace_back(static_cast<std::int32_t>(value.specular_exponent));
        out.emplace_back(static_cast<std::int32_t>(value.environment_intensity));
    }
}

void append_face(const PrimitivePartParameterState &state, int code, int face, int sides, LslList &result) {
    if (face == -1) {
        for (int index = 0; index < sides; ++index)
            append_face(state, code, index, sides, result);
        return;
    }
    if ((code == 17 || code == 36 || code == 37 || code == 38) && (face < 0 || face >= sides))
        return;
    if (static_cast<std::uint32_t>(face) >= TextureEntry::max_faces)
        throw PrimitiveParameterValueError(std::to_string(static_cast<std::uint32_t>(face)) +
                                           " is outside the range of MAX_FACES");
    const auto texture = state.textures.get_face(static_cast<std::uint32_t>(face));
    auto &out = result.items;
    switch (code) {
    case 17:
        out.emplace_back(texture->texture_id().str());
        out.emplace_back(Vector3d{texture->repeat_u(), texture->repeat_v(), 0});
        out.emplace_back(Vector3d{texture->offset_u(), texture->offset_v(), 0});
        out.emplace_back(static_cast<double>(texture->rotation()));
        break;
    case 18: {
        const auto color = texture->color();
        out.emplace_back(Vector3d{color[0], color[1], color[2]});
        out.emplace_back(static_cast<double>(color[3]));
        break;
    }
    case 19:
        out.emplace_back(static_cast<std::int32_t>(texture->shiny() >> 6));
        out.emplace_back(static_cast<std::int32_t>(texture->bump()));
        break;
    case 20:
        out.emplace_back(static_cast<std::int32_t>(texture->fullbright()));
        break;
    case 22:
        out.emplace_back(std::int32_t{texture->mapping() == 2 ? 1 : 0});
        break;
    case 25:
        out.emplace_back(static_cast<double>(texture->glow()));
        break;
    default:
        append_material(state, code, *texture, result);
        break;
    }
}
} // namespace

script::LslList read_primitive_part_parameters(const PrimitivePartParameterState &state, const script::LslList &rules,
                                               script::LslList &result) {
    using namespace framework;
    using namespace script;
    const auto sides = get_number_of_sides(state.shape.geometry());
    const auto &part = state.part;
    const auto &shape = state.shape;
    PrimitiveParameterRuleBackend backend;
    backend.append = [&](int code, std::optional<int> face, LslList &values) {
        if (face) {
            append_face(state, code, *face, sides, values);
            return;
        }
        auto &out = values.items;
        switch (code) {
        case 2:
            out.emplace_back(static_cast<std::int32_t>(part.material));
            break;
        case 3:
            out.emplace_back(std::int32_t{(state.effective_flags & 1u) ? 1 : 0});
            break;
        case 4:
            out.emplace_back(std::int32_t{(state.effective_flags & 0x20000000u) ? 1 : 0});
            break;
        case 5:
            out.emplace_back(std::int32_t{(state.effective_flags & 0x400u) ? 1 : 0});
            break;
        case 6:
            out.emplace_back(vector(state.absolute_position));
            break;
        case 7:
            out.emplace_back(vector(part.scale));
            break;
        case 8:
            out.emplace_back(state.rotation);
            break;
        case 9:
            append_type(shape, values);
            break;
        case 21:
            out.emplace_back(static_cast<std::int32_t>(shape.flexi_entry));
            out.emplace_back(static_cast<std::int32_t>(shape.flexi_softness));
            out.emplace_back(static_cast<double>(shape.flexi_gravity));
            out.emplace_back(static_cast<double>(shape.flexi_drag));
            out.emplace_back(static_cast<double>(shape.flexi_wind));
            out.emplace_back(static_cast<double>(shape.flexi_tension));
            out.emplace_back(Vector3d{shape.flexi_force_x, shape.flexi_force_y, shape.flexi_force_z});
            break;
        case 23:
            out.emplace_back(static_cast<std::int32_t>(shape.light_entry));
            out.emplace_back(Vector3d{shape.light_color(0), shape.light_color(1), shape.light_color(2)});
            out.emplace_back(static_cast<double>(shape.light_intensity));
            out.emplace_back(static_cast<double>(shape.light_radius));
            out.emplace_back(static_cast<double>(shape.light_falloff));
            break;
        case 26: {
            constexpr float scale = 1.f / 255.f;
            const auto channel = [&](unsigned shift) {
                return static_cast<float>((part.text_argb >> shift) & 255u) * scale;
            };
            out.emplace_back(part.floating_text);
            out.emplace_back(Vector3d{channel(16), channel(8), channel(0)});
            out.emplace_back(static_cast<double>(channel(24)));
            break;
        }
        case 27:
            out.emplace_back(part.name);
            break;
        case 28:
            out.emplace_back(part.description);
            break;
        case 29:
            out.emplace_back(rotation(part.rotation));
            break;
        case 30:
            out.emplace_back(static_cast<std::int32_t>(part.physics.shape));
            break;
        case 32: {
            auto axis = part.angular_velocity;
            float spin = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
            float gain = 1.f;
            if (spin < 1.0e-6) {
                axis = {};
                spin = gain = 0;
            } else {
                const float inverse = 1.f / spin;
                axis = {axis.x * inverse, axis.y * inverse, axis.z * inverse};
            }
            out.emplace_back(vector(axis));
            out.emplace_back(static_cast<double>(spin));
            out.emplace_back(static_cast<double>(gain));
            break;
        }
        case 33:
            out.emplace_back(vector(state.local_position));
            break;
        case 35: {
            const auto type = get_prim_type(shape.geometry());
            const bool profile =
                type == PrimType::Sphere || type == PrimType::Torus || type == PrimType::Tube || type == PrimType::Ring;
            out.emplace_back(Vector3d{(profile ? shape.profile_begin : shape.path_begin) / 50000.0,
                                      1.0 - (profile ? shape.profile_end : shape.path_end) / 50000.0, 0});
            break;
        }
        case 41:
            out.emplace_back(static_cast<std::int32_t>(state.sit_target_set));
            out.emplace_back(state.sit_target_set ? vector(part.sit_target_position) : Vector3d{});
            out.emplace_back(state.sit_target_set ? rotation(part.sit_target_orientation) : LslRotation{});
            break;
        case 42:
            out.emplace_back(shape.projection_entry ? shape.projection_texture.str() : Uuid{}.str());
            out.emplace_back(shape.projection_entry ? static_cast<double>(shape.projection_fov) : 0.0);
            out.emplace_back(shape.projection_entry ? static_cast<double>(shape.projection_focus) : 0.0);
            out.emplace_back(shape.projection_entry ? static_cast<double>(shape.projection_ambiance) : 0.0);
            break;
        case 44:
            out.emplace_back(static_cast<std::int32_t>(bool(shape.reflection_probe)));
            out.emplace_back(shape.reflection_probe ? static_cast<double>(shape.reflection_probe->ambiance) : 0.0);
            out.emplace_back(shape.reflection_probe ? static_cast<double>(shape.reflection_probe->clip_distance) : 0.0);
            out.emplace_back(shape.reflection_probe ? static_cast<double>(shape.reflection_probe->flags) : 0.0);
            break;
        }
    };
    return read_primitive_parameter_rules(backend, rules, result);
}
} // namespace opensim::scene
