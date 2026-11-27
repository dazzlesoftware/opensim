// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <bit>
#include <cmath>
#include <cstdint>

namespace opensim::framework {
// Preserve managed scalar operand order when both inputs are NaNs. C++ may
// exchange commutative operands even with precise or strict floating-point
// settings. Quiet signaling operands without discarding their sign or payload.
inline float sdk_quiet_nan(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) | 0x00400000u);
}

inline float sdk_float_product(float left, float right) noexcept {
    if (std::isnan(left))
        return sdk_quiet_nan(left);
    if (std::isnan(right))
        return sdk_quiet_nan(right);
    return left * right;
}

inline float sdk_float_sum(float left, float right) noexcept {
    if (std::isnan(left))
        return sdk_quiet_nan(left);
    if (std::isnan(right))
        return sdk_quiet_nan(right);
    return left + right;
}

inline float sdk_float_difference(float left, float right) noexcept {
    if (std::isnan(left))
        return sdk_quiet_nan(left);
    if (std::isnan(right))
        return sdk_quiet_nan(right);
    return left - right;
}
} // namespace opensim::framework
