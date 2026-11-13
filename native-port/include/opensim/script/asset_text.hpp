// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/vector3d_text.hpp"
#include <span>
namespace opensim::script {
// Bundled SDK Utils.BytesToString(byte[]): removes exactly one final zero,
// decodes using .NET UTF8 replacement fallback, preserves embedded zeros/BOM.
// Returned string is valid native UTF-8. Null AssetBytes throws (SDK nullref);
// a nonnull zero-length array returns an empty string.
std::string asset_text(std::span<const std::uint8_t>);
std::string asset_text(const framework::AssetBytes&);
std::vector<std::string> notecard_lines(const framework::AssetBytes&);
struct LandmarkAsset {
    framework::Uuid region;
    framework::Vector3 position;
    std::uint64_t handle = 0;
};
LandmarkAsset parse_landmark(const framework::AssetBytes&);
framework::Vector3d landmark_position_relative_to(const framework::AssetBytes&, std::uint32_t region_x,
                                                 std::uint32_t region_y);
}
