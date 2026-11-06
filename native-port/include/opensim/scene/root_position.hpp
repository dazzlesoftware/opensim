// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <functional>
namespace opensim::scene {
// MakeRootAgent terrain/bounds placement after the landing policy has run.
// Object/sitting/raycast placement remains a separate scene backend operation.
framework::Vector3 root_landing_position(framework::Vector3 position, std::uint32_t width, std::uint32_t height,
                                         float avatar_height,
                                         const std::function<float(float, float)>& ground_height);
}
