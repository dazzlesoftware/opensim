// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/network/lludp.hpp"
namespace opensim::scene {
network::lludp::Packet avatar_wearables(framework::Uuid agent, framework::Uuid session,
                                        const framework::AvatarAppearance::Wearables& wearables, int serial);
network::lludp::Packet avatar_appearance(framework::Uuid target, std::span<const std::uint8_t> visual,
                                         std::span<const std::uint8_t> texture, float hover);
struct AvatarAnimationEntry {
    framework::Uuid animation, object;
    std::int32_t sequence = 0;
};
network::lludp::Packet avatar_animations(framework::Uuid source, framework::Uuid recipient,
                                         std::span<const AvatarAnimationEntry> animations);
}
