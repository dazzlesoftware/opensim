// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/prim_geometry.hpp"
namespace opensim::framework {
PrimType get_prim_type(const PrimGeometry& s, bool ignore) noexcept {
    if (s.sculpt_entry && !ignore)
        return PrimType::Sculpt;
    const auto profile = s.profile_curve & 7;
    const bool straight = s.path_curve == 16 || s.path_curve == 128;
    if (profile == 1) {
        if (straight)
            return PrimType::Box;
        if (s.path_curve == 32)
            return PrimType::Tube;
    } else if (profile == 0) {
        if (straight)
            return PrimType::Cylinder;
        if (s.path_curve == 32)
            return PrimType::Torus;
    } else if (profile == 5) {
        if (s.path_curve == 32 || s.path_curve == 48)
            return PrimType::Sphere;
    } else if (profile == 3) {
        if (straight)
            return PrimType::Prism;
        if (s.path_curve == 32)
            return PrimType::Ring;
    }
    return PrimType::Box;
}
int get_number_of_sides(const PrimGeometry& s) noexcept {
    if (s.sculpt_entry) {
        if (s.sculpt_type != 5)
            return 1;
        if ((s.profile_curve & 0xf0) != 48)
            return 8;
    }
    const auto type = get_prim_type(s, true);
    const bool profile_cut = s.profile_begin > 0 || s.profile_end > 0;
    const bool path_cut =
        s.path_begin > 0 || s.path_end > 0 || s.path_skew != 0 || s.path_twist_begin != s.path_twist;
    const bool curved_cut = path_cut || s.path_taper_x != 0 || s.path_taper_y != 0 ||
                            s.path_revolutions > 0 || s.path_radius_offset != 0;
    const auto square_cut = [&] {
        return profile_cut ? 2 - s.profile_begin / 12500 - s.profile_end / 12500 : 0;
    };
    const auto triangle_cut = [&] {
        return profile_cut ? 2 - (s.profile_begin >= 16667) - (s.profile_begin >= 33333) -
                                 (s.profile_end >= 16667) - (s.profile_end >= 33333)
                           : 0;
    };
    int result = 0;
    switch (type) {
    case PrimType::Box:
        result = 6 + square_cut();
        break;
    case PrimType::Prism:
        result = 5 + triangle_cut();
        break;
    case PrimType::Cylinder:
        result = 3 + (profile_cut ? 2 : 0);
        break;
    case PrimType::Sphere:
        result = 1 + (path_cut ? 2 : 0) + ((profile_cut || s.profile_hollow > 0) ? 2 : 0);
        break;
    case PrimType::Torus:
        result = 1 + (curved_cut ? 2 : 0) + (profile_cut ? 2 : 0);
        break;
    case PrimType::Tube:
        result = 4 + (curved_cut ? 2 : 0) + square_cut();
        break;
    case PrimType::Ring:
        result = 3 + (curved_cut ? 2 : 0) + triangle_cut();
        break;
    case PrimType::Sculpt:
        break;
    }
    return result + (s.profile_hollow > 0 ? 1 : 0);
}
}
