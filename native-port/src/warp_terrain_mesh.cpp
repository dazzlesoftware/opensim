// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/map_tile.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace opensim::scene {

WarpTerrainMesh build_warp_terrain_mesh(std::span<const float> terrain, std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || std::uint64_t(width) * height != terrain.size())
        throw std::invalid_argument("Invalid Warp3D terrain mesh dimensions");

    WarpTerrainMesh mesh;
    mesh.texture_size = std::min(256u, std::bit_floor(std::max(width, height)));
    const float step = float(width) / mesh.texture_size;
    const auto columns = mesh.texture_size;
    const auto row_count = double(height) / step;
    if (row_count < 1 || row_count >= std::numeric_limits<std::uint32_t>::max() / (columns + 1))
        throw std::invalid_argument("Unsupported Warp3D terrain mesh aspect ratio");
    const auto rows = static_cast<std::uint32_t>(row_count);

    const auto stride = columns + 1;
    const float inverse_x = 1.f / (columns * step);
    const float inverse_y = 1.f / (rows * step);
    mesh.vertices.reserve(std::size_t(stride) * (rows + 1));
    mesh.triangles.reserve(std::size_t(columns) * rows * 2);
    float y = 0;
    for (std::uint32_t row = 0; row <= rows; ++row, y += step) {
        // The closing row and column repeat the preceding elevation samples.
        const auto sample_y = std::min(height - 1, static_cast<std::uint32_t>(row == rows ? y - step : y));
        const float v = row == rows ? 1.f : y * inverse_y;
        float x = 0;
        for (std::uint32_t column = 0; column <= columns; ++column, x += step) {
            const auto sample_x = std::min(width - 1, static_cast<std::uint32_t>(column == columns ? x - step : x));
            const float u = column == columns ? 1.f : x * inverse_x;
            mesh.vertices.push_back({x, terrain[std::size_t(sample_y) * width + sample_x], y, u, v});
        }
    }

    for (std::uint32_t row = 0; row < rows; ++row) {
        for (std::uint32_t column = 0; column < columns; ++column) {
            const auto vertex = row * stride + column;
            mesh.triangles.push_back({vertex, vertex + 1, vertex + stride});
            mesh.triangles.push_back({vertex + stride + 1, vertex + stride, vertex + 1});
        }
    }
    return mesh;
}

WarpMapProjection project_warp_map_vertex(const WarpTerrainVertex& vertex, std::uint32_t region_height,
                                         float camera_height) {
    const auto truncate = [](float value) {
        if (!std::isfinite(value))
            return std::int32_t{0};
        return static_cast<std::int32_t>(std::clamp<double>(
            value, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
    };
    float camera_depth = camera_height - vertex.elevation;
    // The managed orthographic projection gives visible vertices a minimum depth.
    // Behind-camera vertices retain their signed depth for triangles crossing the plane.
    if (camera_depth >= 0.f)
        camera_depth = std::max(camera_depth, .001f);
    camera_depth = std::clamp(camera_depth, -32768.f, 32768.f);
    return {truncate(vertex.x), truncate(float(region_height) - vertex.y),
            truncate(camera_depth * 65536.f)};
}

} // namespace opensim::scene
