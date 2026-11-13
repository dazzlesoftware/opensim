// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/vector_text.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace opensim::physics {
struct AssetPhysicsMesh {
    std::vector<framework::Vector3> vertices;
    std::vector<std::uint32_t> indices;
};

std::optional<AssetPhysicsMesh> decode_asset_physics_mesh(std::string_view asset, bool convex = false);

struct AssetRenderFace {
    AssetPhysicsMesh geometry;
    std::vector<std::array<float, 2>> texcoords;
    std::vector<framework::Vector3> normals;
};

enum class AssetMeshLod { Lowest, Low, Medium, High };

// Warp3D requests render faces with vertex normal decoding disabled.
std::optional<std::vector<AssetRenderFace>> decode_asset_render_mesh(std::string_view asset, AssetMeshLod lod);
std::optional<AssetRenderFace> build_sculpt_render_face(std::span<const std::uint8_t> pixels,
    std::uint32_t width, std::uint32_t height, std::size_t channels, std::uint8_t sculpt_type, AssetMeshLod lod);
}
