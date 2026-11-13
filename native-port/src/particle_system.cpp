// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// SDK compatibility model. See protocol/LIBOPENMETAVERSE-LICENSE.txt.
#include "opensim/framework/particle_system.hpp"
#include <algorithm>
#include <cmath>

namespace opensim::framework {
namespace {
void append(std::vector<std::uint8_t>& buffer, std::uint32_t value, int size) {
    for (int byte = 0; byte < size; ++byte) {
        buffer.push_back(static_cast<std::uint8_t>(value));
        value >>= 8;
    }
}
void append_fixed(std::vector<std::uint8_t>& buffer, float value, int integer_bits, int fraction_bits,
                  bool signed_value = false) {
    const auto limit = static_cast<float>(1 << integer_bits);
    const auto adjusted = signed_value ? value + limit : value;
    const auto maximum = signed_value ? limit * 2 : limit;
    const auto packed = adjusted > 1e-6f
        ? static_cast<std::uint32_t>(std::min(adjusted, maximum) * static_cast<float>(1 << fraction_bits))
        : 0u;
    append(buffer, packed, integer_bits + fraction_bits + signed_value > 8 ? 2 : 1);
}
std::uint8_t color_byte(float value) {
    if (!(value > 0))
        return 0;
    return static_cast<std::uint8_t>(255.f * std::min(value, 1.f));
}
std::uint8_t glow_byte(float value) {
    const auto scaled = value * 255.f;
    if (!std::isfinite(scaled))
        return 0;
    return static_cast<std::uint8_t>(static_cast<int>(std::fmod(std::trunc(scaled), 256.f)));
}
}

std::vector<std::uint8_t> ParticleSystem::bytes() const {
    constexpr std::uint32_t source_size = 68, particle_size = 18;
    constexpr std::uint32_t glow_flag = 0x10000, blend_flag = 0x20000;
    const bool glow = start_glow > 0 || end_glow > 0;
    const bool blend = blend_source != 7 || blend_destination != 9;
    const bool extended = glow || blend;
    std::vector<std::uint8_t> result;
    result.reserve(source_size + particle_size + (extended ? 8 : 0) + (glow ? 2 : 0) + (blend ? 2 : 0));
    if (extended)
        append(result, source_size, 4);
    append(result, crc, 4);
    append(result, source_flags, 4);
    result.push_back(pattern);
    append_fixed(result, source_age, 8, 8);
    append_fixed(result, start_age, 8, 8);
    append_fixed(result, inner_angle, 3, 5);
    append_fixed(result, outer_angle, 3, 5);
    for (const auto value : {burst_rate, burst_radius, speed_min, speed_max})
        append_fixed(result, value, 8, 8);
    result.push_back(burst_count);
    for (const auto value : {angular_velocity.x, angular_velocity.y, angular_velocity.z,
                             acceleration.x, acceleration.y, acceleration.z})
        append_fixed(result, value, 8, 7, true);
    for (const auto& id : {texture, target})
        result.insert(result.end(), id.bytes().begin(), id.bytes().end());

    if (extended)
        append(result, particle_size + (glow ? 2 : 0) + (blend ? 2 : 0), 4);
    append(result, particle_flags | (glow ? glow_flag : 0u) | (blend ? blend_flag : 0u), 4);
    append_fixed(result, particle_age, 8, 8);
    for (const auto& color : {start_color, end_color})
        for (const auto value : color)
            result.push_back(color_byte(value));
    for (const auto& scale : {start_scale, end_scale})
        for (const auto value : scale)
            append_fixed(result, value, 3, 5);
    if (glow) {
        result.push_back(glow_byte(start_glow));
        result.push_back(glow_byte(end_glow));
    }
    if (blend) {
        result.push_back(blend_source);
        result.push_back(blend_destination);
    }
    return result;
}
}
