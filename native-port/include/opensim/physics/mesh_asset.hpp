// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/vector_text.hpp"
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace opensim::physics {
struct AssetPhysicsMesh {
    std::vector<framework::Vector3> vertices;
    std::vector<std::uint32_t> indices;
};

std::optional<AssetPhysicsMesh> decode_asset_physics_mesh(std::string_view asset, bool convex = false);
}
