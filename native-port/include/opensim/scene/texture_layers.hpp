// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/framework/uuid.hpp"
#include "opensim/scene/map_tile.hpp"
#include <array>
#include <memory>
#include <optional>
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
using MapJpegEncoder = std::function<std::string(std::span<const std::uint8_t>)>;
MapJpegEncoder open_map_jpeg_encoder(const std::filesystem::path& library);
std::string encode_map_bitmap_jpeg(const MapBitmap& bitmap);
using MapTextureAverage = std::function<std::optional<std::array<std::uint8_t, 3>>(
    std::span<const std::uint8_t>)>;
MapTextureAverage open_map_texture_average(const std::filesystem::path& library);

using MapBitmapDecoder = std::function<MapBitmap(std::span<const std::uint8_t>)>;
MapBitmapDecoder open_map_bitmap_decoder(const std::filesystem::path& library);

using MapJpeg2000Encoder = std::function<std::vector<std::uint8_t>(
    std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height,
    std::size_t channels, bool lossless)>;
MapJpeg2000Encoder open_map_jpeg2000_encoder(const std::filesystem::path& library);

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
