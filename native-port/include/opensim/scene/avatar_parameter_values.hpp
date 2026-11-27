// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/primitive_parameter_values.hpp"

namespace opensim::scene {
struct AvatarParameterSeat {
    bool part_exists = false, group_exists = false, root_exists = false;
    framework::Vector3 root_position;
    framework::Quaternion root_rotation;
};

struct AvatarParameterAppearance {
    bool exists = false;
    framework::Vector3 size;
    float height = 0;
};

struct AvatarParameterState {
    std::string name;
    framework::Vector3 offset_position, absolute_position;
    framework::Quaternion rotation, world_rotation;
    bool legacy_sit_offsets = false;
    // The original performs the seat lookup once, before reading any rules.
    std::function<AvatarParameterSeat()> seat;
    std::function<AvatarParameterAppearance()> appearance;
};

script::LslList read_primitive_avatar_parameters(const AvatarParameterState &state, const script::LslList &rules,
                                                 script::LslList &result);
} // namespace opensim::scene
