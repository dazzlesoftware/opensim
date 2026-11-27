// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/primitive_parameter_values.hpp"
#include "parameter_value_canonical.hpp"
#include <bit>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace opensim;
using namespace framework;
using namespace scene;
using namespace script;

namespace {
Uuid id(int number) {
    auto bytes = Uuid{}.bytes();
    bytes.back() = static_cast<std::uint8_t>(number);
    return Uuid::from_bytes(bytes);
}

std::string canonical(const LslList &values) {
    return opensim::test::parameter_value_canonical(values);
}

std::string observe(int type, int seed, std::uint32_t flags, int code, int face, int mode) {
    SceneObject part;
    part.name = "Name " + std::to_string(seed);
    part.description = "Desc<&" + std::to_string(seed);
    part.floating_text = "Text";
    part.text_argb = (static_cast<std::uint32_t>(seed) << 24) | (static_cast<std::uint32_t>(seed) << 16) |
                     (static_cast<std::uint32_t>((seed * 3) & 255) << 8) | static_cast<std::uint32_t>((seed * 7) & 255);
    part.material = static_cast<std::uint8_t>(seed % 8);
    part.physics.shape = static_cast<std::uint8_t>(seed % 3);
    part.scale = {seed / 10.f, 2.25f, -3.5f};
    part.rotation = {.5f, .25f, .125f, 1.f};
    part.angular_velocity = seed % 3 == 0   ? Vector3{}
                            : seed % 3 == 1 ? Vector3{3, 4, 0}
                                            : Vector3{1.0e-7f, -1.0e-7f, 0};
    part.sit_target_position = {.25f, .5f, .75f};
    part.sit_target_orientation = {0, 0, 1, 0};
    PrimitiveBaseShapeCore shape([](const auto &) {});
    const std::uint8_t profiles[]{1, 0, 3, 5, 0, 1, 3, 0, 0};
    shape.profile_curve(static_cast<std::uint8_t>(profiles[type] | ((seed % 4) << 4)));
    shape.path_curve = type >= 3 && type <= 6 ? 32 : 16;
    shape.sculpt_entry = type >= 7;
    shape.sculpt_type = type == 8 ? 5 : 1;
    shape.sculpt_texture = id(40);
    shape.animesh_enabled((seed & 1) != 0);
    shape.profile_begin = static_cast<std::uint16_t>((seed % 5) * 1000);
    shape.profile_end = static_cast<std::uint16_t>((seed % 7) * 1100);
    shape.profile_hollow = static_cast<std::uint16_t>(seed * 100);
    shape.path_begin = static_cast<std::uint16_t>((seed % 3) * 2000);
    shape.path_end = static_cast<std::uint16_t>((seed % 4) * 1300);
    shape.path_twist_begin = static_cast<std::int8_t>(seed - 128);
    shape.path_twist = static_cast<std::int8_t>(127 - seed);
    shape.path_taper_x = static_cast<std::int8_t>(seed % 99 - 49);
    shape.path_taper_y = static_cast<std::int8_t>(49 - seed % 99);
    shape.path_radius_offset = static_cast<std::int8_t>(seed % 101 - 50);
    shape.path_skew = static_cast<std::int8_t>(seed % 97 - 48);
    shape.path_revolutions = static_cast<std::uint8_t>(seed);
    shape.path_scale_x = shape.path_shear_x = static_cast<std::uint8_t>(seed);
    shape.path_scale_y = shape.path_shear_y = static_cast<std::uint8_t>(255 - seed);
    shape.flexi_entry = (seed & 1) != 0;
    shape.flexi_softness = seed % 4;
    shape.flexi_gravity = -.25f * seed;
    shape.flexi_drag = .125f * seed;
    shape.flexi_wind = .5f;
    shape.flexi_tension = 2.25f;
    shape.flexi_force_x = 1.5f;
    shape.flexi_force_y = -2.25f;
    shape.flexi_force_z = 3.125f;
    shape.light_entry = (seed & 2) != 0;
    shape.light_color(0, seed / 255.f);
    shape.light_color(1, .25f);
    shape.light_color(2, .75f);
    shape.light_intensity = .625f;
    shape.light_radius = seed / 2.f;
    shape.light_falloff = 1.25f;
    shape.projection_entry = (seed & 4) != 0;
    shape.projection_texture = id(12);
    shape.projection_fov = 1.5f;
    shape.projection_focus = 2.5f;
    shape.projection_ambiance = -.25f;
    if (seed & 8)
        shape.reflection_probe = std::make_shared<ShapeReflectionProbe>(
            ShapeReflectionProbe{seed / 10.f, -.75f, static_cast<std::uint8_t>(seed)});
    TextureEntry source(id(10));
    source.default_texture->color({seed / 255.f, .25f, .75f, .5f});
    source.default_texture->repeat_u(1.25f);
    source.default_texture->repeat_v(-.75f);
    source.default_texture->offset_u(.3333f);
    source.default_texture->offset_v(-.2f);
    source.default_texture->rotation(.75f);
    source.default_texture->bump(static_cast<std::uint8_t>(seed % 32));
    source.default_texture->shiny(static_cast<std::uint8_t>((seed % 4) << 6));
    source.default_texture->fullbright((seed & 16) != 0);
    source.default_texture->mapping(static_cast<std::uint8_t>((seed % 3) * 2));
    source.default_texture->glow(seed / 255.f);
    source.default_texture->material_id(mode == 5 ? Uuid{} : id(20));
    source.create_face(2)->texture_id(id(11));
    source.create_face(2)->repeat_u(.75f);
    const auto encoded = source.bytes();
    TextureEntry textures(encoded);
    PrimitivePartParameterState state{part, shape, textures};
    state.absolute_position = {10.25f, -20.5f, seed / 8.f};
    state.local_position = {-1.125f, 2.f, seed / 16.f};
    state.rotation = {.125, .25, .5, 1};
    state.effective_flags = flags;
    state.sit_target_set = (seed & 16) != 0;
    const auto shared_material = std::make_shared<PrimitiveParameterMaterial>();
    if (mode != 0) {
        auto &material = *shared_material;
        material.normal_map = id(30);
        material.specular_map = id(31);
        material.normal_repeat_x = 1.25f;
        material.normal_repeat_y = -.75f;
        material.normal_offset_x = .125f;
        material.normal_offset_y = -.25f;
        material.normal_rotation = .75f;
        material.specular_repeat_x = 2;
        material.specular_repeat_y = 3;
        material.specular_offset_x = -.5f;
        material.specular_offset_y = .5f;
        material.specular_rotation = -.25f;
        material.specular_color = {static_cast<std::uint8_t>(seed), 128, 255};
        material.specular_exponent = material.alpha_cutoff = static_cast<std::uint8_t>(seed);
        material.environment_intensity = static_cast<std::uint8_t>(255 - seed);
        material.alpha_mode = static_cast<std::uint8_t>(seed % 4);
        state.material = [=](Uuid) { return mode == 4 ? nullptr : shared_material; };
    }
    state.material_texture_visible = [=](Uuid texture) {
        if (mode == 6) {
            shared_material->normal_repeat_x += 1.f;
            shared_material->specular_repeat_x += 1.f;
        }
        return mode == 2 || mode == 6 || (mode == 3 && texture == id(30));
    };
    LslList rules, result;
    const auto add = [&](int property) {
        rules.items.emplace_back(static_cast<std::int32_t>(property));
        if ((property >= 17 && property <= 20) || property == 22 || property == 25 ||
            (property >= 36 && property <= 38))
            rules.items.emplace_back(static_cast<std::int32_t>(face));
    };
    if (code == 0)
        for (const auto property : {2,  3,  4,  5,  6,  7,  8,  9,  17, 18, 19, 20, 21, 22, 23,
                                    25, 26, 27, 28, 29, 30, 32, 33, 35, 36, 37, 38, 41, 42, 44})
            add(property);
    else if (code == 34)
        rules.items = {std::int32_t{34}, static_cast<std::int32_t>(face), std::int32_t{27}};
    else
        add(code);
    try {
        const auto tail = read_primitive_part_parameters(state, rules, result);
        return canonical(result) + "|" + canonical(tail);
    } catch (const PrimitiveParameterValueError &error) {
        return canonical(result) + "|!" + error.source_type + ":" + error.what();
    }
}
} // namespace

int main(int argc, char **) {
    try {
        if (argc > 1) {
            int type, seed, code, face, mode;
            std::uint32_t flags;
            while (std::cin >> type >> seed >> flags >> code >> face >> mode)
                std::cout << observe(type, seed, flags, code, face, mode) << "\n";
            return 0;
        }
        const auto texture_outside = observe(0, 0, 0, 17, 44, 0);
        const auto color_outside = observe(0, 0, 0, 18, 44, 0);
        if (texture_outside != "|" || !color_outside.starts_with("v") || color_outside.find("|!") != std::string::npos)
            throw std::runtime_error("Texture and color queries lost their different source face bounds");
        if (!observe(0, 0, 0, 18, -2, 0).ends_with("|!System.Exception:4294967294 is outside the range of MAX_FACES"))
            throw std::runtime_error("Invalid color face did not reach the SDK face accessor");
        if (observe(0, 0, 0, 32, 0, 0) !=
            "v000000000000000000000000000000000000000000000000;f0000000000000000;f0000000000000000;|")
            throw std::runtime_error("Zero omega must report a zero axis, spin, and gain");
        if (observe(0, 0, 0, 37, 0, 0) != observe(0, 0, 0, 37, 0, 4))
            throw std::runtime_error("Missing material module and missing material must share fallback values");
        if (observe(0, 1, 0, 9, 0, 0) == observe(7, 1, 0, 9, 0, 0))
            throw std::runtime_error("Sculpt and geometric prim type result layouts were conflated");
        std::cout << "PASS: primitive parameter values, face bounds, and material fallbacks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
