// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/map_tile.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace opensim::scene {
namespace {

struct RasterVertex {
    double x, y, depth, u, v;
};

RasterVertex at_row(const RasterVertex& a, const RasterVertex& b, double row) {
    const auto ratio = (row - a.y) / (b.y - a.y);
    return {std::lerp(a.x, b.x, ratio), row, std::lerp(a.depth, b.depth, ratio),
            std::lerp(a.u, b.u, ratio), std::lerp(a.v, b.v, ratio)};
}

} // namespace

WarpMapRaster::WarpMapRaster(std::uint32_t width, std::uint32_t height, float camera_height)
    : image_{width, height, {}}, camera_height_(camera_height) {
    const auto pixels = std::uint64_t(width) * height;
    if (pixels == 0 || pixels > std::numeric_limits<std::int32_t>::max() / 3 || !std::isfinite(camera_height))
        throw std::invalid_argument("Invalid Warp3D map raster dimensions");
    image_.pixels.resize(static_cast<std::size_t>(pixels) * 3, 255);
    depth_.resize(static_cast<std::size_t>(pixels), std::numeric_limits<std::int32_t>::max());
}

void WarpMapRaster::draw(const WarpTerrainMesh& mesh, const MapBitmap& texture) {
    rasterize(mesh.vertices, mesh.triangles, texture, 255, {255, 255, 255}, false, false);
}

void WarpMapRaster::draw_tinted(const WarpTerrainMesh& mesh, std::shared_ptr<const MapBitmap> texture,
                               std::array<std::uint8_t, 3> tint, unsigned opacity, bool repeat) {
    if (!texture || opacity == 0)
        return;
    if (opacity == 255 && texture->channels == 3) {
        rasterize(mesh.vertices, mesh.triangles, *texture, opacity, tint, repeat, false);
        return;
    }
    for (const auto& triangle : mesh.triangles) {
        const std::array points{mesh.vertices.at(triangle[0]), mesh.vertices.at(triangle[1]),
                                mesh.vertices.at(triangle[2])};
        const double elevation = double(points[0].elevation) + points[1].elevation + points[2].elevation;
        if (std::isfinite(elevation))
            transparent_.push_back({points, texture, tint, opacity, repeat, elevation});
    }
}

void WarpMapRaster::draw_water(float water_height) {
    const float half_width = image_.width * .5f;
    const float center_y = image_.height * .5f;
    const float south = center_y - half_width;
    const float north = center_y + half_width;
    const WarpTerrainMesh plane{
        0,
        {{0, water_height, north, 0, 0}, {float(image_.width), water_height, north, 0, 0},
         {float(image_.width), water_height, south, 0, 0}, {0, water_height, south, 0, 0}},
        {{0, 3, 2}, {0, 2, 1}},
    };
    static const auto texture = std::make_shared<const MapBitmap>(MapBitmap{1, 1, {29, 72, 96}});
    draw_tinted(plane, texture, {255, 255, 255}, 216, false);
}

void WarpMapRaster::finish_transparency() {
    std::stable_sort(transparent_.begin(), transparent_.end(), [](const auto& a, const auto& b) {
        return a.elevation < b.elevation;
    });
    constexpr std::array<std::array<std::uint32_t, 3>, 1> indices{{{0, 1, 2}}};
    for (const auto& triangle : transparent_)
        rasterize(triangle.vertices, indices, *triangle.texture, triangle.opacity,
                  triangle.tint, triangle.repeat, true);
    transparent_.clear();
}

void WarpMapRaster::rasterize(std::span<const WarpTerrainVertex> points,
                              std::span<const std::array<std::uint32_t, 3>> triangles,
                              const MapBitmap& texture, unsigned opacity, std::array<std::uint8_t, 3> tint,
                              bool repeat, bool transparent) {
    const auto texels = std::uint64_t(texture.width) * texture.height;
    if ((texture.channels != 3 && texture.channels != 4) || texels == 0 ||
        texels > std::numeric_limits<std::size_t>::max() / texture.channels ||
        texture.pixels.size() != texels * texture.channels)
        throw std::invalid_argument("Invalid Warp3D map texture");

    std::vector<RasterVertex> vertices;
    vertices.reserve(points.size());
    for (const auto& vertex : points) {
        const auto projected = project_warp_map_vertex(vertex, image_.height, camera_height_);
        const bool finite = std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.elevation);
        const double depth = finite ? double(projected.depth) : std::numeric_limits<double>::quiet_NaN();
        vertices.push_back({double(projected.x), double(projected.y), depth, vertex.u, vertex.v});
    }

    const auto texel = [repeat](double coordinate, std::uint32_t size) {
        if (!repeat)
            return static_cast<std::uint32_t>(std::clamp(coordinate, 0.0, 1.0) * (size - 1));
        const double wrapped = std::fmod(std::trunc(coordinate * (size - 1)), size);
        return static_cast<std::uint32_t>(wrapped < 0 ? wrapped + size : wrapped);
    };
    for (const auto& triangle : triangles) {
        const auto& a = vertices.at(triangle[0]);
        const auto& b = vertices.at(triangle[1]);
        const auto& c = vertices.at(triangle[2]);
        const auto area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        // Warp3D rejects triangles wholly behind the orthographic camera.
        // A triangle crossing its plane keeps its projected vertices and signed depth.
        const bool behind_camera = a.depth < 0 && b.depth < 0 && c.depth < 0;
        if (area >= 0 || behind_camera ||
            !std::isfinite(a.depth) || !std::isfinite(b.depth) || !std::isfinite(c.depth) ||
            !std::isfinite(a.u) || !std::isfinite(a.v) || !std::isfinite(b.u) ||
            !std::isfinite(b.v) || !std::isfinite(c.u) || !std::isfinite(c.v))
            continue;

        std::array<const RasterVertex*, 3> corners{&a, &b, &c};
        std::sort(corners.begin(), corners.end(), [](const auto* left, const auto* right) {
            return left->y < right->y;
        });
        const auto& top = *corners[0];
        const auto& middle = *corners[1];
        const auto& bottom = *corners[2];
        const auto first_row = static_cast<std::uint32_t>(std::clamp(top.y, 0.0, double(image_.height)));
        const auto end_row = static_cast<std::uint32_t>(std::clamp(bottom.y, 0.0, double(image_.height)));
        for (auto row = first_row; row < end_row; ++row) {
            auto left = at_row(top, bottom, row);
            auto right = row < middle.y ? at_row(top, middle, row) : at_row(middle, bottom, row);
            if (right.x < left.x)
                std::swap(left, right);
            left.x = std::floor(left.x);
            right.x = std::floor(right.x);
            const auto first = static_cast<std::uint32_t>(std::clamp(left.x, 0.0, double(image_.width)));
            const auto end = static_cast<std::uint32_t>(std::clamp(right.x, 0.0, double(image_.width)));
            for (auto column = first; column < end; ++column) {
                const auto ratio = (column - left.x) / (right.x - left.x);
                const auto z = std::lerp(left.depth, right.depth, ratio);
                const auto pixel = std::size_t(row) * image_.width + column;
                if (z >= depth_[pixel])
                    continue;
                const auto u = texel(std::lerp(left.u, right.u, ratio), texture.width);
                const auto v = texel(std::lerp(left.v, right.v, ratio), texture.height);
                const auto offset = (std::size_t(v) * texture.width + u) * texture.channels;
                auto* target = image_.pixels.data() + pixel * 3;
                const auto* source = texture.pixels.data() + offset;
                const unsigned alpha = texture.channels == 4 ? (source[3] * opacity + 255) >> 8 : opacity;
                if (alpha == 0)
                    continue;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const int color = (source[channel] * tint[channel] + 255) >> 8;
                    // Warp3D blends transparent materials with a denominator of 256.
                    const int transparency = alpha == 255 ? 0 : 256 - static_cast<int>(alpha);
                    const int delta = (int(target[channel]) - color) * transparency;
                    target[channel] = static_cast<std::uint8_t>(color + (delta >> 8));
                }
                if (!transparent)
                    depth_[pixel] = z;
            }
        }
    }
}

} // namespace opensim::scene
