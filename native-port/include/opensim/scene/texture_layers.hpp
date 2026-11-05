// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/framework/uuid.hpp"
#include <memory>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace opensim::framework { class IAssetCache; }
namespace opensim::scene {
struct TextureLayer {
    std::int32_t start, end;
    bool operator==(const TextureLayer&) const = default;
};
using TextureLayerDecoder = std::function<std::vector<TextureLayer>(std::span<const std::uint8_t>)>;
using CachedTextureLayerDecoder = std::function<std::vector<std::int32_t>(
    const framework::Uuid&, std::span<const std::uint8_t>)>;
TextureLayerDecoder open_texture_layer_decoder(const std::filesystem::path& library,
                                               bool use_next_layer_boundary);
CachedTextureLayerDecoder cache_texture_layer_decoder(
    TextureLayerDecoder decoder,
    std::function<std::shared_ptr<framework::IAssetCache>()> asset_cache = {},
    framework::Uuid creator = {});
}
