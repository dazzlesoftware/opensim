// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/quaternion_text.hpp"
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
    const Vector3 cross{2 * (q.y * v.z - q.z * v.y), 2 * (q.z * v.x - q.x * v.z),
                        2 * (q.x * v.y - q.y * v.x)};
    return {v.x + q.w * cross.x + q.y * cross.z - q.z * cross.y,
            v.y + q.w * cross.y + q.z * cross.x - q.x * cross.z,
            v.z + q.w * cross.z + q.x * cross.y - q.y * cross.x};
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
inline float sdk_quaternion_length_squared(const Quaternion& q) noexcept {
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
        return {a.x * left - a.y * right, a.y * left + a.x * right,
                a.z * left - a.w * right, a.w * left + a.z * right};
    }
    auto left = 1.f - amount;
    auto right = amount;
    if (1.f - dot >= .05f) {
        const auto angle = std::acos(dot);
        const auto inverse = 1.f / std::sin(angle);
        left = std::sin(angle * left) * inverse;
        right = std::sin(angle * amount) * inverse;
    }
    return {a.x * left + b.x * right, a.y * left + b.y * right,
            a.z * left + b.z * right, a.w * left + b.w * right};
}
}
