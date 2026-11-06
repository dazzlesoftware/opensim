// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
namespace opensim::framework {
struct PrimContactMaterial {
    float friction = .6f;
    float restitution = .5f;
    bool operator==(const PrimContactMaterial&) const = default;
};

enum class PrimType : int {
    Box = 0,
    Cylinder = 1,
    Prism = 2,
    Sphere = 3,
    Torus = 4,
    Tube = 5,
    Ring = 6,
    Sculpt = 7
};
struct LinearPrimPath {
    float begin = 0.f, end = 1.f;
    float taper_x = 0.f, taper_y = 0.f;
    float shear_x = 0.f, shear_y = 0.f;
    float twist_begin = 0.f, twist_end = 0.f;
    float profile_begin = 0.f, profile_end = 1.f;
    std::uint8_t hollow_segments = 0;
    float hole_x = 1.f, hole_y = 1.f;
    float radius_offset = 0.f, skew = 0.f;
    float revolutions = 1.f;
    bool operator==(const LinearPrimPath&) const = default;
};
// Exact values exposed by PrimitiveBaseShape getters. This is not a packet decoder:
// ProfileCurve setter normalization and sculpt extra-parameter decoding belong to callers.
// The existing scene::PrimShape omits sculpt fields, so it cannot represent this closure.
struct PrimGeometry {
    bool sculpt_entry = false;
    std::uint8_t sculpt_type = 0, profile_curve = 0, path_curve = 0;
    std::uint16_t profile_begin = 0, profile_end = 0, profile_hollow = 0, path_begin = 0, path_end = 0;
    std::int8_t path_skew = 0, path_twist_begin = 0, path_twist = 0, path_taper_x = 0, path_taper_y = 0,
                path_radius_offset = 0;
    std::uint8_t path_revolutions = 0;
};
PrimType get_prim_type(const PrimGeometry&, bool ignore_sculpt = false) noexcept;
// Preserves source results even for out-of-viewer-range ushort cuts (including negative counts).
int get_number_of_sides(const PrimGeometry&) noexcept;
}
