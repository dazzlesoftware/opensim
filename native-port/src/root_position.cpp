// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/root_position.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace opensim::scene {
framework::Vector3 clamp_default_landing(framework::Vector3 pos, std::uint32_t width, std::uint32_t height) {
    constexpr float border = 5.f;
    return {std::clamp(pos.x, border, std::max(border, static_cast<float>(width) - border)),
            std::clamp(pos.y, border, std::max(border, static_cast<float>(height) - border)),
            std::clamp(pos.z, -100.f, 65536.f)};
}

framework::Vector3 local_teleport_position(framework::Vector3 pos, std::uint32_t width, std::uint32_t height,
                                          std::optional<float> avatar_height,
                                          const std::function<float(float, float)>& ground_height) {
    if (!ground_height)
        throw std::invalid_argument("Ground height backend required");
    if (pos.x < 0 || pos.y < 0 || pos.x >= static_cast<float>(width) || pos.y >= static_cast<float>(height))
        pos = {128.f, 128.f, 128.f};
    const float half_height = avatar_height ? *avatar_height / 2.f : .8f;
    const float clearance = ground_height(pos.x, pos.y) + (half_height + .1f);
    if (std::isfinite(clearance) && pos.z < clearance)
        pos.z = clearance;
    pos.z = std::clamp(pos.z, -100.f, 65536.f);
    return pos;
}

framework::Vector3 root_landing_position(framework::Vector3 pos, std::uint32_t width, std::uint32_t height,
                                         float avatar_height,
                                         const std::function<float(float, float)>& ground_height) {
    if (!ground_height)
        throw std::invalid_argument("Ground height backend required");
    if (pos.x < 0 || pos.y < 0 || pos.x >= static_cast<float>(width) || pos.y >= static_cast<float>(height)) {
        if (pos.x < 0)
            pos.x = .5f;
        else if (pos.x >= static_cast<float>(width))
            pos.x = static_cast<float>(width) - .5f;
        if (pos.y < 0)
            pos.y = .5f;
        else if (pos.y >= static_cast<float>(height))
            pos.y = static_cast<float>(height) - .5f;
    }
    float ground = ground_height(pos.x, pos.y) + .01f;
    float half = .8f;
    if (avatar_height > 0)
        half = .5f * avatar_height;
    ground += half;
    if (ground > pos.z)
        pos.z = ground;
    return pos;
}
}
