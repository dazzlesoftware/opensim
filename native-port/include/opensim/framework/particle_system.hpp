// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/vector_text.hpp"
#include <array>
#include <vector>

namespace opensim::framework {
struct ParticleSystem {
    std::uint32_t crc = 1, source_flags = 0, particle_flags = 0;
    std::uint8_t pattern = 0, burst_count = 1, blend_source = 7, blend_destination = 9;
    float source_age = 0, start_age = 0, inner_angle = 0, outer_angle = 0;
    float burst_rate = .1f, burst_radius = 0, speed_min = 1, speed_max = 1, particle_age = 10;
    Vector3 angular_velocity{}, acceleration{};
    Uuid texture, target;
    std::array<float, 4> start_color{1, 1, 1, 1}, end_color{1, 1, 1, 1};
    std::array<float, 2> start_scale{1, 1}, end_scale{1, 1};
    float start_glow = 0, end_glow = 0;

    std::vector<std::uint8_t> bytes() const;
};
}
