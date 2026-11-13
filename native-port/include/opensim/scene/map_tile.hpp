// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace opensim::framework {
class Uuid;
class AssetBase;
}
namespace opensim::scene {

// Takes terrain heights in southwest-origin row order and returns RGB pixels
// in northwest-origin row order, as used by the original shaded renderer.
std::vector<std::uint8_t> render_shaded_map_tile(std::span<const float> heights, std::uint32_t width,
                                                 std::uint32_t height, float water_height,
                                                 std::array<std::uint8_t, 3> water_color = {29, 71, 95});

struct MapBitmap {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
    std::size_t channels = 3;
};

MapBitmap load_static_map_bitmap(const std::filesystem::path& path);
void write_map_bitmap_png(const MapBitmap& bitmap, const std::filesystem::path& path);

struct WarpTerrainVertex {
    float x, elevation, y;
    float u, v;
};

struct WarpTerrainMesh {
    std::uint32_t texture_size = 0;
    std::vector<WarpTerrainVertex> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

WarpTerrainMesh build_warp_terrain_mesh(std::span<const float> terrain, std::uint32_t width,
                                        std::uint32_t height);

struct WarpMapProjection {
    std::int32_t x, y, depth;
};

// Orthographic map camera with a viewport of one pixel per region meter.
WarpMapProjection project_warp_map_vertex(const WarpTerrainVertex& vertex, std::uint32_t region_height,
                                          float camera_height = 4096.f);

class WarpMapRaster {
  public:
    WarpMapRaster(std::uint32_t width, std::uint32_t height, float camera_height = 4096.f);
    void draw(const WarpTerrainMesh& mesh, const MapBitmap& texture);
    void draw_tinted(const WarpTerrainMesh& mesh, std::shared_ptr<const MapBitmap> texture,
                     std::array<std::uint8_t, 3> tint, unsigned opacity = 255, bool repeat = true);
    void draw_water(float water_height);
    void finish_transparency();
    const MapBitmap& image() const { return image_; }
    MapBitmap take_image() && { return std::move(image_); }

  private:
    struct TransparentTriangle {
        std::array<WarpTerrainVertex, 3> vertices;
        std::shared_ptr<const MapBitmap> texture;
        std::array<std::uint8_t, 3> tint;
        unsigned opacity;
        bool repeat;
        double elevation;
    };
    void rasterize(std::span<const WarpTerrainVertex> points,
                   std::span<const std::array<std::uint32_t, 3>> triangles, const MapBitmap& texture,
                   unsigned opacity, std::array<std::uint8_t, 3> tint, bool repeat, bool transparent);
    MapBitmap image_;
    std::vector<double> depth_;
    std::vector<TransparentTriangle> transparent_;
    float camera_height_;
};

struct WarpTerrainSplat {
    std::uint32_t width = 0, height = 0;
    std::uint32_t region_x = 0, region_y = 0;
    std::array<float, 4> start_heights{}, height_ranges{};
    bool textured = false, average_textures = false;
};

// The splat texture uses the southwest-origin row order expected by Warp3D.
MapBitmap render_warp_terrain_splat(std::span<const float> terrain, std::uint32_t terrain_width,
                                    std::uint32_t terrain_height, const WarpTerrainSplat& options,
                                    std::array<MapBitmap, 4> textures = {});

struct SceneObject;

// Face order and UVs follow the managed mesher for an uncut box.
std::array<WarpTerrainMesh, 6> build_warp_box_faces(const SceneObject& part,
                                                    const SceneObject* root = nullptr);
std::array<WarpTerrainMesh, 5> build_warp_prism_faces(const SceneObject& part,
                                                      const SceneObject* root = nullptr);

void draw_warp_prims(
    WarpMapRaster& raster, std::span<const SceneObject> objects,
    const std::function<MapBitmap(const framework::Uuid&)>& load_texture, bool texture_prims,
    float minimum_height, float maximum_height,
    const std::function<std::shared_ptr<framework::AssetBase>(framework::Uuid)>& load_mesh = {});

void draw_map_object_volumes(std::span<std::uint8_t> pixels, std::span<const float> heights,
                             std::uint32_t width, std::uint32_t height, std::span<const SceneObject> objects);

using MapColor = std::array<std::uint8_t, 3>;

std::optional<MapColor> parse_map_color(std::string_view text);

struct TexturedMapPalette {
    std::array<MapColor, 4> land{{{165, 137, 118}, {69, 89, 49}, {162, 154, 141}, {200, 200, 200}}};
    MapColor water{29, 71, 95};
};

// Elevation corners are in southwest, southeast, northwest, northeast order.
std::vector<std::uint8_t> render_textured_map_tile(std::span<const float> heights, std::uint32_t width,
                                                   std::uint32_t height, float water_height,
                                                   const std::array<float, 4>& low,
                                                   const std::array<float, 4>& high,
                                                   const TexturedMapPalette& palette);

// Takes one sale flag per 4-meter land cell, starting at the southwest corner.
std::vector<std::uint8_t> render_parcel_map_tile(std::span<const std::uint8_t> sale_cells,
                                                 std::uint32_t width, std::uint32_t height);

std::vector<std::uint8_t> resize_map_tile(std::span<const std::uint8_t> pixels, std::uint32_t width,
                                          std::uint32_t height, std::uint32_t target_width,
                                          std::uint32_t target_height, std::size_t channels = 3);

void draw_map_label(MapBitmap& canvas, std::string_view text, std::int64_t x, std::int64_t y,
                    bool bottom_aligned);

void composite_map_bitmap(MapBitmap& canvas, MapBitmap tile, std::int64_t x, std::int64_t y,
                          std::uint32_t width, std::uint32_t height);

} // namespace opensim::scene
