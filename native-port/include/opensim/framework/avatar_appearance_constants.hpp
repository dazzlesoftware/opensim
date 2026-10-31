// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <array>
#include <cstdint>
namespace opensim::framework::avatar_appearance {
inline constexpr int visual_param_count=218,texture_count=45,texture_count_pv7=29,bakes_count_pv7=6,max_wearable_pv7=16,max_wearable_legacy=15;
inline constexpr std::array<std::uint8_t,11> bake_indices={8,9,10,11,19,20,40,41,42,43,44};
}
