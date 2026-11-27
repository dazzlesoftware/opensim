// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/scene/world.hpp"
#include "opensim/script/primitive_parameter_rules.hpp"

namespace opensim::scene {
class PrimitiveParameterValueError : public std::runtime_error {
  public:
    explicit PrimitiveParameterValueError(std::string message, std::string type = "System.Exception")
        : std::runtime_error(std::move(message)), source_type(std::move(type)) {}

    const std::string source_type;
};

struct PrimitiveParameterMaterial {
    framework::Uuid normal_map, specular_map;
    float normal_repeat_x = 1, normal_repeat_y = 1, normal_offset_x = 0, normal_offset_y = 0, normal_rotation = 0;
    float specular_repeat_x = 1, specular_repeat_y = 1, specular_offset_x = 0, specular_offset_y = 0,
          specular_rotation = 0;
    std::array<std::uint8_t, 3> specular_color{255, 255, 255};
    std::uint8_t specular_exponent = 51, environment_intensity = 0, alpha_mode = 1, alpha_cutoff = 0;
};

struct PrimitivePartParameterState {
    const SceneObject &part;
    const framework::PrimitiveBaseShapeCore &shape;
    framework::TextureEntry &textures;
    // Prepared by the scene adapter using AbsolutePosition, GetPartLocalPos,
    // GetPartRot, GetEffectiveObjectFlags and IsSitTargetSet source semantics.
    framework::Vector3 absolute_position, local_position;
    script::LslRotation rotation;
    std::uint32_t effective_flags = 0;
    bool sit_target_set = false;
    std::function<std::shared_ptr<PrimitiveParameterMaterial>(framework::Uuid)> material;
    std::function<bool(framework::Uuid)> material_texture_visible;
};

script::LslList read_primitive_part_parameters(const PrimitivePartParameterState &state, const script::LslList &rules,
                                               script::LslList &result);
} // namespace opensim::scene
