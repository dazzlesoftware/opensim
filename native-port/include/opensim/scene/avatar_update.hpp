// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/lludp.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include <array>
namespace opensim::scene {
struct AvatarUpdate {
    framework::Uuid id;
    std::uint32_t local_id = 0, parent_id = 0;
    framework::Vector3 size{.45f, .6f, 1.9f}, position, velocity;
    framework::Quaternion rotation{0, 0, 0, 1};
    std::array<float, 4> collision_plane{};
    std::string first_name, last_name, group_title;
    bool flying = false, sitting = false, hide_title = false;
};
// Active LLClientView CreateAvatarUpdateBlock(LLUDPZeroEncoder) projection.
// Position is the source OffsetPosition; parent_id is the parent GROUP local ID.
network::lludp::Packet avatar_object_update(const AvatarUpdate& avatar, std::uint64_t region_handle,
                                            float time_dilation);
}
