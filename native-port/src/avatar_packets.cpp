// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/avatar_packets.hpp"
namespace opensim::scene {
network::lludp::Packet avatar_wearables(framework::Uuid agent, framework::Uuid session,
                                        const framework::AvatarAppearance::Wearables& wearables, int serial) {
    using namespace network;
    auto p = lludp::make_packet("AgentWearablesUpdate");
    auto& a = p.block("AgentData");
    a["AgentID"] = agent;
    a["SessionID"] = session;
    a["SerialNum"] = std::uint64_t(static_cast<std::uint32_t>(serial));
    for (std::size_t i = 0; i < wearables.size(); ++i) {
        if (!wearables[i])
            throw std::invalid_argument("Null wearable");
        for (std::size_t j = 0; j < wearables[i]->count(); ++j) {
            auto item = wearables[i]->at(static_cast<int>(j));
            auto b = lludp::make_block(p.spec->blocks.at(1));
            b["WearableType"] = std::uint64_t(static_cast<std::uint8_t>(i));
            b["AssetID"] = item.asset;
            b["ItemID"] = item.item;
            p.blocks["WearableData"].push_back(std::move(b));
        }
    }
    return p;
}
network::lludp::Packet avatar_appearance(framework::Uuid target, std::span<const std::uint8_t> visual,
                                         std::span<const std::uint8_t> texture, float hover) {
    using namespace network;
    auto p = lludp::make_packet("AvatarAppearance");
    p.block("Sender")["ID"] = target;
    p.block("ObjectData")["TextureEntry"] = std::vector<std::uint8_t>(texture.begin(), texture.end());
    for (auto v : visual) {
        auto b = lludp::make_block(p.spec->blocks.at(2));
        b["ParamValue"] = std::uint64_t(v);
        p.blocks["VisualParam"].push_back(std::move(b));
    }
    auto b = lludp::make_block(p.spec->blocks.at(4));
    b["HoverHeight"] = std::vector<double>{0, 0, hover};
    p.blocks["AppearanceHover"].push_back(std::move(b));
    return p;
}
network::lludp::Packet avatar_animations(framework::Uuid source, framework::Uuid recipient,
                                         std::span<const AvatarAnimationEntry> animations) {
    using namespace network;
    if (animations.size() > 255)
        throw std::out_of_range("Avatar animation block limit");
    // LLClientView.SendAnimations writes into the SDK's4096-byte UDP buffer.
    // Preserve that method's capacity boundary, in addition to the wire count.
    std::size_t objects = 0;
    if (source == recipient)
        for (const auto& a : animations)
            if (!a.object.is_zero() && a.object != source)
                ++objects;
    if (26 + 20 * animations.size() + 16 * objects > 4096)
        throw std::length_error("Avatar animation source UDP buffer capacity");
    auto p = lludp::make_packet("AvatarAnimation");
    p.block("Sender")["ID"] = source;
    auto add = [&](const AvatarAnimationEntry& a) {
        auto b = lludp::make_block(p.spec->blocks.at(1));
        b["AnimID"] = a.animation;
        b["AnimSequenceID"] = std::int64_t(a.sequence);
        p.blocks["AnimationList"].push_back(std::move(b));
    };
    if (source == recipient) {
        for (auto& a : animations)
            if (!a.object.is_zero() && a.object != source) {
                add(a);
                auto b = lludp::make_block(p.spec->blocks.at(2));
                b["ObjectID"] = a.object;
                p.blocks["AnimationSourceList"].push_back(std::move(b));
            }
        for (auto& a : animations)
            if (a.object.is_zero() || a.object == source)
                add(a);
    } else
        for (auto& a : animations)
            add(a);
    return p;
}
}
