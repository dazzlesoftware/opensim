// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/quaternion_text.hpp"
#include "opensim/framework/sdk_float.hpp"
#include "opensim/framework/vector_text.hpp"
#include <cmath>
#include <numbers>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
namespace opensim::framework {
inline Quaternion sdk_multiply_quaternion(Quaternion a, Quaternion b) noexcept {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
            a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Vector3 sdk_rotate_vector(Vector3 v, Quaternion q) noexcept {
    // Bundled Vector3.op_Multiply(Vector3, Quaternion), retaining its float
    // intermediates and grouping. The cross-product shortcut rounds differently.
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(q.x) ||
        !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) {
        const float x2 = sdk_float_sum(q.x, q.x), y2 = sdk_float_sum(q.y, q.y), z2 = sdk_float_sum(q.z, q.z);
        const float wx2 = sdk_float_product(q.w, x2), wy2 = sdk_float_product(q.w, y2),
                    wz2 = sdk_float_product(q.w, z2);
        const float xx2 = sdk_float_product(q.x, x2), xy2 = sdk_float_product(q.x, y2),
                    xz2 = sdk_float_product(q.x, z2);
        const float yy2 = sdk_float_product(q.y, y2), yz2 = sdk_float_product(q.y, z2),
                    zz2 = sdk_float_product(q.z, z2);
        const auto row = [&](float x, float y, float z) {
            return sdk_float_sum(sdk_float_sum(sdk_float_product(x, v.x), sdk_float_product(y, v.y)),
                                 sdk_float_product(z, v.z));
        };
        return {row(sdk_float_difference(sdk_float_difference(1.f, yy2), zz2), sdk_float_difference(xy2, wz2),
                    sdk_float_sum(xz2, wy2)),
                row(sdk_float_sum(xy2, wz2), sdk_float_difference(sdk_float_difference(1.f, xx2), zz2),
                    sdk_float_difference(yz2, wx2)),
                row(sdk_float_difference(xz2, wy2), sdk_float_sum(yz2, wx2),
                    sdk_float_difference(sdk_float_difference(1.f, xx2), yy2))};
    }
    const float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    const float wx2 = q.w * x2, wy2 = q.w * y2, wz2 = q.w * z2;
    const float xx2 = q.x * x2, xy2 = q.x * y2, xz2 = q.x * z2;
    const float yy2 = q.y * y2, yz2 = q.y * z2, zz2 = q.z * z2;
    return {(1.f - yy2 - zz2) * v.x + (xy2 - wz2) * v.y + (xz2 + wy2) * v.z,
            (xy2 + wz2) * v.x + (1.f - xx2 - zz2) * v.y + (yz2 - wx2) * v.z,
            (xz2 - wy2) * v.x + (yz2 + wx2) * v.y + (1.f - xx2 - yy2) * v.z};
}

// Bundled SDK Quaternion.LengthSquared selects Sse41.DotProduct at runtime.
// DPPS groups adjacent products; its scalar fallback adds left to right.
inline bool sdk_sse41_supported() noexcept {
    static const bool supported = []() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        int info[4]{};
        __cpuid(info, 1);
        return (info[2] & (1 << 19)) != 0;
#elif defined(__x86_64__) || defined(__i386__)
        unsigned a = 0, b = 0, c = 0, d = 0;
        return __get_cpuid(1, &a, &b, &c, &d) && (c & bit_SSE4_1) != 0;
#else
        return false;
#endif
    }();
    return supported;
}

inline float sdk_quaternion_length_squared(const Quaternion &q) noexcept {
    if (sdk_sse41_supported())
        return (q.x * q.x + q.y * q.y) + (q.z * q.z + q.w * q.w);
    return ((q.x * q.x + q.y * q.y) + q.z * q.z) + q.w * q.w;
}

inline Quaternion sdk_slerp_quaternion(Quaternion a, Quaternion b, float amount) noexcept {
    const float x = a.x * b.x;
    const float y = a.y * b.y;
    const float z = a.z * b.z;
    const float w = a.w * b.w;
    float dot = sdk_sse41_supported() ? (x + y) + (z + w) : ((x + y) + z) + w;
    if (dot < 0) {
        dot = -dot;
        a = {-a.x, -a.y, -a.z, -a.w};
    }
    if (std::isnan(dot)) {
        const auto left = std::sin(std::numbers::pi_v<float> * (.5f - amount));
        const auto right = std::sin(std::numbers::pi_v<float> * amount);
        return {a.x * left - a.y * right, a.y * left + a.x * right, a.z * left - a.w * right, a.w * left + a.z * right};
    }
    auto left = 1.f - amount;
    auto right = amount;
    if (1.f - dot >= .05f) {
        const auto angle = std::acos(dot);
        const auto inverse = 1.f / std::sin(angle);
        left = std::sin(angle * left) * inverse;
        right = std::sin(angle * amount) * inverse;
    }
    return {a.x * left + b.x * right, a.y * left + b.y * right, a.z * left + b.z * right, a.w * left + b.w * right};
}
} // namespace opensim::framework
