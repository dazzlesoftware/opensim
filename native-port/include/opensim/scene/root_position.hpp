// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <functional>
#include <optional>
namespace opensim::scene {
framework::Vector3 clamp_default_landing(framework::Vector3 position, std::uint32_t width, std::uint32_t height);
framework::Vector3 local_teleport_position(framework::Vector3 position, std::uint32_t width, std::uint32_t height,
                                          std::optional<float> avatar_height,
                                          const std::function<float(float, float)>& ground_height);
// MakeRootAgent terrain/bounds placement after the landing policy has run.
// Object/sitting/raycast placement remains a separate scene backend operation.
framework::Vector3 root_landing_position(framework::Vector3 position, std::uint32_t width, std::uint32_t height,
                                         float avatar_height,
                                         const std::function<float(float, float)>& ground_height);
}
