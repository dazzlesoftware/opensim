#include "opensim/scene/terrain_layer.hpp"
#include "opensim/scene/map_tile.hpp"
#include "opensim/scene/world.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/framework/texture_entry.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <bit>
using namespace opensim::scene;
static void fill(Terrain &t, int mode, int seed) {
    for (unsigned y = 0; y < t.height(); y++)
        for (unsigned x = 0; x < t.width(); x++) {
            float f = 21.f;
            if (mode == 1)
                f = float(int((x * 17 + y * 31 + seed * 13) % 397) - 198) * .25f;
            if (mode == 2)
                f = 21.f + float((x + y + seed) % 3) * .001f;
            if (mode == 3)
                f = float(x) * .125f - float(y) * .0625f + seed;
            t.set(x, y, f);
        }
}
int main(int argc, char **) {
    try {
        if (argc > 1) {
            unsigned w, h, n;
            int mode, seed;
            while (std::cin >> w >> h >> mode >> seed >> n) {
                Terrain t(w, h);
                fill(t, mode, seed);
                std::vector<TerrainPatchCoordinate> p(n);
                for (auto &v : p)
                    std::cin >> v.x >> v.y;
                auto result = terrain_layer_packets(t, p);
                for (const auto &packet : result) {
                    std::cout << unsigned(packet.type) << ":";
                    for (auto b : packet.data)
                        std::cout << std::hex << std::setfill('0') << std::setw(2) << unsigned(b);
                    std::cout << std::dec << ";";
                }
                std::cout << "\n";
            }
            return 0;
        }
        if (parse_map_color("  liGHtGreY ") != MapColor{211, 211, 211} ||
            parse_map_color("DarkSlateBlue") != MapColor{72, 61, 139} ||
            parse_map_color("buttonface") != MapColor{240, 240, 240} ||
            parse_map_color("#1d4") != MapColor{17, 221, 68} ||
            parse_map_color("#1d4 ") != MapColor{0, 1, 212} ||
            parse_map_color("0X112233") != MapColor{17, 34, 51} ||
            parse_map_color("128, 0xff, 128, 64") != MapColor{255, 128, 64} ||
            parse_map_color("grey") || parse_map_color("256, 0, 0"))
            throw std::runtime_error("Managed map color configuration values differ");
        MapBitmap canvas{2, 1, {0, 0, 139, 0, 0, 139}};
        composite_map_bitmap(canvas, {2, 1, {255, 255, 255, 0, 200, 50, 20, 128}, 4}, -1, 0, 2, 1);
        if (canvas.pixels != std::vector<std::uint8_t>{100, 25, 79, 0, 0, 139})
            throw std::runtime_error("World map composition does not clip or blend tiles");
        MapBitmap label_canvas{256, 140, std::vector<std::uint8_t>(256 * 140 * 3)};
        draw_map_label(label_canvas, "768m x 768m", 10, 4, false);
        draw_map_label(label_canvas, "R\xc3\xa9gion", 10, 136, true);
        const auto middle = label_canvas.pixels.begin() + 256 * 70 * 3;
        if (std::find(label_canvas.pixels.begin(), middle, 255) == middle ||
            std::find(middle, label_canvas.pixels.end(), 255) == label_canvas.pixels.end())
            throw std::runtime_error("World map scale or region label was not drawn");
        std::vector<float> mesh_heights(512 * 256, 20.f);
        mesh_heights[254 * 512 + 510] = 73.f;
        const auto terrain_mesh = build_warp_terrain_mesh(mesh_heights, 512, 256);
        if (terrain_mesh.texture_size != 256 || terrain_mesh.vertices.size() != 257 * 129 ||
            terrain_mesh.triangles.size() != 256 * 128 * 2 ||
            terrain_mesh.triangles[0] != std::array<std::uint32_t, 3>{0, 1, 257} ||
            terrain_mesh.triangles[1] != std::array<std::uint32_t, 3>{258, 257, 1})
            throw std::runtime_error("Warp3D terrain mesh topology differs");
        const auto& mesh_edge = terrain_mesh.vertices.back();
        if (mesh_edge.x != 512.f || mesh_edge.y != 256.f || mesh_edge.elevation != 73.f ||
            mesh_edge.u != 1.f || mesh_edge.v != 1.f)
            throw std::runtime_error("Warp3D terrain mesh closing edge differs");
        const auto southwest = project_warp_map_vertex(terrain_mesh.vertices.front(), 256);
        const auto projected_middle = project_warp_map_vertex({255.5f, 4000.25f, 127.5f, 0.f, 0.f}, 256);
        const auto on_camera_plane = project_warp_map_vertex({0, 4096.f, 0, 0, 0}, 256);
        const auto behind_camera_plane = project_warp_map_vertex({0, 4096.00048828125f, 0, 0, 0}, 256);
        if (southwest.x != 0 || southwest.y != 256 || southwest.depth != 267124736 ||
            projected_middle.x != 255 || projected_middle.y != 128 || projected_middle.depth != 6275072 ||
            on_camera_plane.depth != 65 || behind_camera_plane.depth != -32)
            throw std::runtime_error("Warp3D orthographic projection differs from managed camera");

        WarpTerrainMesh raster_triangle;
        raster_triangle.vertices = {{1, 20, 7, 0, 0}, {3, 20, 2, 0, 1}, {6, 20, 7, 1, 0}};
        raster_triangle.triangles = {{0, 1, 2}};
        WarpMapRaster triangle_raster(8, 8);
        triangle_raster.draw(raster_triangle, {1, 1, {255, 0, 0}});
        const std::array<std::string_view, 8> managed_coverage{
            "........", ".#####..", ".####...", ".###....",
            "..##....", "..#.....", "........", "........",
        };
        for (std::size_t row = 0; row < managed_coverage.size(); ++row)
            for (std::size_t column = 0; column < 8; ++column)
                if ((triangle_raster.image().pixels[(row * 8 + column) * 3 + 1] == 0) !=
                    (managed_coverage[row][column] == '#'))
                    throw std::runtime_error("Warp3D triangle scanline coverage differs from managed raster");

        const std::vector<float> flat_terrain(16, 20.f);
        auto raster_mesh = build_warp_terrain_mesh(flat_terrain, 4, 4);
        WarpMapRaster depth_raster(4, 4);
        depth_raster.draw(raster_mesh, {2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0}});
        const auto raster_pixel = [&](std::size_t x, std::size_t y) {
            const auto offset = (y * 4 + x) * 3;
            const auto& pixels = depth_raster.image().pixels;
            return MapColor{pixels[offset], pixels[offset + 1], pixels[offset + 2]};
        };
        if (raster_pixel(0, 0) != MapColor{0, 0, 255} || raster_pixel(3, 3) != MapColor{255, 0, 0})
            throw std::runtime_error("Warp3D texture coordinates differ");
        WarpMapRaster horizontal_raster(4, 4);
        horizontal_raster.draw(raster_mesh, {4, 1, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0}});
        const std::array<std::uint8_t, 12> managed_texture_row{255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
        if (!std::equal(managed_texture_row.begin(), managed_texture_row.end(),
                        horizontal_raster.image().pixels.begin()))
            throw std::runtime_error("Warp3D raster texture sampling differs from the managed renderer");

        WarpTerrainMesh camera_triangle{
            0,
            {{2, 4100, 2, 0, 0}, {6, 20, 2, 0, 0}, {6, 20, 6, 0, 0}},
            {{0, 1, 2}},
        };
        const auto camera_triangle_coverage = [&] {
            WarpMapRaster raster(8, 8);
            raster.draw(camera_triangle, {1, 1, {255, 0, 0}});
            const auto& pixels = raster.image().pixels;
            std::size_t covered = 0;
            for (std::size_t green = 1; green < pixels.size(); green += 3) {
                if (pixels[green] == 0)
                    ++covered;
            }
            return covered;
        };
        if (camera_triangle_coverage() != 6)
            throw std::runtime_error("Warp3D camera-plane crossing triangle coverage differs");
        for (auto& point : camera_triangle.vertices)
            point.elevation = 4096.f;
        if (camera_triangle_coverage() != 6)
            throw std::runtime_error("Warp3D triangle on the camera plane was clipped");
        for (auto& point : camera_triangle.vertices)
            point.elevation = 4100.f;
        if (camera_triangle_coverage() != 0)
            throw std::runtime_error("Warp3D triangle behind the camera plane was drawn");
        for (auto& point : camera_triangle.vertices)
            point.elevation = -28672.f;
        if (camera_triangle_coverage() != 0)
            throw std::runtime_error("Warp3D triangle at the depth buffer limit was drawn");
        for (auto& point : camera_triangle.vertices)
            point.elevation = -28671.f;
        if (camera_triangle_coverage() != 6)
            throw std::runtime_error("Warp3D triangle inside the depth buffer limit was clipped");
        camera_triangle.vertices[0].elevation = -28672.f;
        if (camera_triangle_coverage() != 6)
            throw std::runtime_error("Warp3D triangle crossing the depth buffer limit was clipped");

        auto near_mesh = raster_mesh;
        for (auto& vertex : near_mesh.vertices)
            vertex.elevation = 30.f;
        depth_raster.draw(near_mesh, {1, 1, {20, 40, 60}});
        depth_raster.draw(raster_mesh, {1, 1, {90, 90, 90}});
        if (raster_pixel(0, 0) != MapColor{20, 40, 60} || raster_pixel(3, 3) != MapColor{20, 40, 60})
            throw std::runtime_error("Warp3D raster depth ordering differs");

        WarpTerrainMesh water_fixture;
        water_fixture.vertices = {{0, 10, 0, 0, 0}, {4, 10, 0, 1, 0},
                                  {0, 10, 8, 0, 1}, {4, 10, 8, 1, 1}};
        water_fixture.triangles = {{0, 1, 2}, {3, 2, 1}};
        WarpMapRaster water_raster(8, 8);
        water_raster.draw(water_fixture, {1, 1, {255, 0, 0}});
        auto dry_fixture = water_fixture;
        for (auto& vertex : dry_fixture.vertices) {
            vertex.x = vertex.x * .5f + 4;
            vertex.elevation = 30;
        }
        water_raster.draw(dry_fixture, {1, 1, {0, 255, 0}});
        water_raster.draw_water(20);
        water_raster.finish_transparency();
        for (std::size_t y = 0; y < 8; ++y) {
            for (std::size_t x = 0; x < 8; ++x) {
                const auto offset = (y * 8 + x) * 3;
                const auto& pixels = water_raster.image().pixels;
                const MapColor expected = x < 4 ? MapColor{64, 60, 81} :
                    x < 6 ? MapColor{0, 255, 0} : MapColor{64, 100, 120};
                if (!std::equal(expected.begin(), expected.end(), pixels.begin() + offset))
                    throw std::runtime_error("Warp3D water transparency or depth differs from the managed renderer");
            }
        }

        const auto box = SceneObject::primitive({}, {4.f, 4.f, 22.f}, {4.f, 4.f, 2.f});
        const auto box_faces = build_warp_box_faces(box);
        if (box_faces[0].vertices[0].x != 2.f || box_faces[0].vertices[0].y != 2.f ||
            box_faces[0].vertices[0].elevation != 23.f || box_faces[0].vertices[2].u != 1.f ||
            box_faces[1].vertices[0].v != 1.f || box_faces[5].vertices[0].v != 0.f ||
            box_faces[5].triangles[1] != std::array<std::uint32_t, 3>{3, 0, 2})
            throw std::runtime_error("Warp3D box faces differ from the managed mesher");
        WarpMapRaster translucent_box_raster(8, 8);
        const auto translucent_texture = std::make_shared<const MapBitmap>(
            MapBitmap{1, 1, {40, 100, 200, 128}, 4});
        translucent_box_raster.draw_tinted(box_faces[0], translucent_texture, {255, 255, 255}, 128);
        translucent_box_raster.finish_transparency();
        const auto center = (3 * 8 + 3) * 3;
        const MapColor blended_box{201, 216, 241};
        if (!std::equal(blended_box.begin(), blended_box.end(),
                        translucent_box_raster.image().pixels.begin() + center))
            throw std::runtime_error("Warp3D material and texture alpha differ from the managed renderer");

        WarpMapRaster sorted_box_raster(8, 8);
        auto farther_face = box_faces[0];
        for (auto& point : farther_face.vertices)
            point.elevation -= 10;
        const auto white_texture = std::make_shared<const MapBitmap>(MapBitmap{1, 1, {255, 255, 255}});
        sorted_box_raster.draw_tinted(box_faces[0], white_texture, {255, 0, 0}, 128);
        sorted_box_raster.draw_tinted(farther_face, white_texture, {0, 0, 255}, 128);
        sorted_box_raster.finish_transparency();
        const MapColor sorted_box{191, 63, 127};
        if (!std::equal(sorted_box.begin(), sorted_box.end(), sorted_box_raster.image().pixels.begin() + center))
            throw std::runtime_error("Warp3D translucent triangles are not drawn back to front");
        WarpMapRaster box_raster(8, 8);
        for (std::size_t face = 0; face < box_faces.size(); ++face)
            box_raster.draw(box_faces[face], {1, 1, face == 0 ?
                std::vector<std::uint8_t>{40, 100, 200} : std::vector<std::uint8_t>{255, 0, 0}});
        for (std::size_t row = 0; row < 8; ++row) {
            for (std::size_t column = 0; column < 8; ++column) {
                const auto offset = (row * 8 + column) * 3;
                const bool covered = row >= 2 && row < 6 && column >= 2 && column < 6;
                const MapColor expected = covered ? MapColor{40, 100, 200} : MapColor{255, 255, 255};
                if (!std::equal(expected.begin(), expected.end(), box_raster.image().pixels.begin() + offset))
                    throw std::runtime_error("Warp3D top-down box footprint or visible face differs");
            }
        }
        auto warp_root = SceneObject::primitive({}, {4.f, 4.f, 20.f});
        warp_root.rotation = {0.f, 0.f, .70710678f, .70710678f};
        auto warp_child = SceneObject::primitive({}, {2.f, 0.f, 3.f}, {2.f, 4.f, 6.f});
        warp_child.parent_id = 1;
        const auto linked_faces = build_warp_box_faces(warp_child, &warp_root);
        const auto& linked_corner = linked_faces[0].vertices[0];
        if (std::abs(linked_corner.x - 6.f) > .00001f || std::abs(linked_corner.y - 5.f) > .00001f ||
            linked_corner.elevation != 26.f)
            throw std::runtime_error("Warp3D child box transform differs");

        auto textured_box = box;
        opensim::framework::TextureEntry warp_textures(opensim::framework::TextureEntry::white_texture);
        warp_textures.get_face(0)->color({1.f, 0.f, 0.f, 1.f});
        textured_box.texture_entry = warp_textures.bytes();
        WarpMapRaster textured_box_raster(8, 8);
        draw_warp_prims(textured_box_raster, std::span(&textured_box, 1),
                            [](const auto&) { return MapBitmap{1, 1, {40, 100, 200}}; },
                            true, -100.f, 4086.f);
        const auto& textured_pixels = textured_box_raster.image().pixels;
        constexpr auto box_center = (3 * 8 + 3) * 3;
        if (textured_pixels[box_center] != 40 || textured_pixels[box_center + 1] != 0 ||
            textured_pixels[box_center + 2] != 0)
            throw std::runtime_error("Warp3D box face texture tint differs from the managed renderer");

        WarpMapRaster opaque_rgba_raster(8, 8);
        draw_warp_prims(opaque_rgba_raster, std::span(&textured_box, 1),
                       [](const auto&) { return MapBitmap{2, 1, {40, 100, 200, 255, 40, 100, 200, 255}, 4}; },
                       true, -100.f, 4086.f);
        opaque_rgba_raster.draw(box_faces[0], {1, 1, {0, 255, 0}});
        opaque_rgba_raster.finish_transparency();
        const auto& opaque_pixels = opaque_rgba_raster.image().pixels;
        if (opaque_pixels[box_center] != 40 || opaque_pixels[box_center + 1] != 0 ||
            opaque_pixels[box_center + 2] != 0)
            throw std::runtime_error("Warp3D fully opaque RGBA texture did not write depth in the opaque pass");

        auto mesh_fallback = textured_box;
        const auto mesh_shape = opensim::framework::PrimitiveBaseShapeCore::create_mesh(
            6, opensim::framework::TextureEntry::white_texture, [](const std::string&) {});
        mesh_fallback.shape.profile_curve = mesh_shape.profile_curve();
        mesh_fallback.extra_params = mesh_shape.extra_params();
        WarpMapRaster missing_texture_raster(8, 8);
        draw_warp_prims(missing_texture_raster, std::span(&mesh_fallback, 1), {}, true, -100.f, 4086.f);
        const auto& fallback_pixels = missing_texture_raster.image().pixels;
        if (fallback_pixels[box_center] != 127 || fallback_pixels[box_center + 1] != 0 ||
            fallback_pixels[box_center + 2] != 0)
            throw std::runtime_error("Warp3D mesh shape or missing texture fallback differs");

        auto offset_box = box;
        opensim::framework::TextureEntry offset_textures(opensim::framework::TextureEntry::white_texture);
        const auto offset_face = offset_textures.get_face(0);
        offset_face->offset_u(16545.f / 32767.f);
        offset_face->repeat_u(.009857654571533203f);
        offset_box.texture_entry = offset_textures.bytes();
        WarpMapRaster offset_raster(8, 8);
        draw_warp_prims(offset_raster, std::span(&offset_box, 1),
                       [](const auto&) { return MapBitmap{2, 1, {20, 0, 0, 200, 0, 0}}; },
                       true, -100.f, 4086.f);
        constexpr auto box_top_left = (2 * 8 + 2) * 3;
        // Adding the face offset to the UV center first gives exactly 1.0 at this vertex.
        if (offset_raster.image().pixels[box_top_left] != 200)
            throw std::runtime_error("Warp3D face offset rounds to the wrong texel");

        warp_textures.get_face(0)->color({1.f, 1.f, 1.f, 1.f});
        warp_textures.get_face(0)->mapping(2);
        textured_box.texture_entry = warp_textures.bytes();
        MapBitmap planar_texture{4, 4, std::vector<std::uint8_t>(4 * 4 * 3)};
        for (std::size_t pixel = 0; pixel < 16; ++pixel)
            planar_texture.pixels[pixel * 3] = static_cast<std::uint8_t>((pixel + 1) * 10);
        WarpMapRaster planar_raster(8, 8);
        draw_warp_prims(planar_raster, std::span(&textured_box, 1),
                            [&](const auto&) { return planar_texture; }, true, -100.f, 4086.f);
        constexpr std::array<std::uint8_t, 16> managed_planar{
            110, 90, 100, 120,
            30, 10, 20, 40,
            70, 50, 60, 80,
            150, 130, 140, 160,
        };
        for (std::size_t y = 0; y < 4; ++y)
            for (std::size_t x = 0; x < 4; ++x)
                if (planar_raster.image().pixels[((y + 2) * 8 + x + 2) * 3] != managed_planar[y * 4 + x])
                    throw std::runtime_error("Warp3D planar texture coordinates differ from the managed renderer");

        auto prism = textured_box;
        prism.shape.profile_curve = 3;
        warp_textures.get_face(0)->color({1.f, 0.f, 0.f, 1.f});
        warp_textures.get_face(0)->mapping(0);
        prism.texture_entry = warp_textures.bytes();
        const auto prism_faces = build_warp_prism_faces(prism);
        const auto& top = prism_faces.front();
        const auto& bottom = prism_faces.back();
        if (top.vertices.size() != 3 || top.triangles.size() != 1 ||
            bottom.vertices.size() != 3 || bottom.triangles.size() != 1 ||
            top.vertices[0].u != 1.f || top.vertices[0].v != .5f ||
            std::abs(top.vertices[1].v - .9330127f) > .00001f ||
            std::abs(bottom.vertices[0].v - .9330127f) > .00001f)
            throw std::runtime_error("Warp3D prism caps differ from the managed mesher");
        for (std::size_t face = 1; face < 4; ++face) {
            const auto& side = prism_faces[face];
            if (side.vertices.size() != 4 || side.triangles.size() != 2 ||
                side.vertices[0].u != 0.f || side.vertices[0].v != .999999f ||
                side.vertices[3].u != 1.f || side.vertices[3].v != 0.f)
                throw std::runtime_error("Warp3D prism sides differ from the managed mesher");
        }

        WarpMapRaster prism_raster(8, 8);
        draw_warp_prims(prism_raster, std::span(&prism, 1), {}, false, -100.f, 4086.f);
        const auto& prism_pixels = prism_raster.image().pixels;
        constexpr auto prism_center = (4 * 8 + 4) * 3;
        constexpr auto outside_prism = (4 * 8 + 2) * 3;
        if (prism_pixels[prism_center] != 255 || prism_pixels[prism_center + 1] != 0 ||
            prism_pixels[prism_center + 2] != 0 || prism_pixels[outside_prism + 1] != 255)
            throw std::runtime_error("Warp3D prism footprint or visible face differs");

        auto cylinder = prism;
        cylinder.shape.profile_curve = 0;
        cylinder.scale = {6.f, 6.f, 2.f};
        WarpMapRaster cylinder_raster(8, 8);
        draw_warp_prims(cylinder_raster, std::span(&cylinder, 1), {}, false, -100.f, 4086.f);
        const auto& cylinder_pixels = cylinder_raster.image().pixels;
        constexpr auto cylinder_center = (4 * 8 + 4) * 3;
        constexpr auto cylinder_corner = (1 * 8 + 6) * 3;
        if (cylinder_pixels[cylinder_center] != 255 || cylinder_pixels[cylinder_center + 1] != 0 ||
            cylinder_pixels[cylinder_center + 2] != 0 || cylinder_pixels[cylinder_corner + 1] != 255)
            throw std::runtime_error(
                "Warp3D cylinder footprint or visible face differs: center green " +
                std::to_string(cylinder_pixels[cylinder_center + 1]) + ", corner green " +
                std::to_string(cylinder_pixels[cylinder_corner + 1]));

        WarpTerrainSplat splat;
        splat.width = 3;
        splat.height = 2;
        splat.region_x = splat.region_y = 256000;
        splat.start_heights = {10.f, 15.f, 20.f, 25.f};
        splat.height_ranges = {40.f, 45.f, 50.f, 55.f};
        const std::array<float, 6> splat_heights{15.f, 25.f, 35.f, 45.f, 55.f, 30.f};
        const auto blended_splat = render_warp_terrain_splat(splat_heights, 3, 2, splat);
        const std::vector<std::uint8_t> managed_splat{
            107, 129, 150, 85, 113, 119, 58, 95, 81,
            130, 138, 144, 130, 137, 142, 111, 132, 156,
        };
        if (blended_splat.pixels != managed_splat)
            throw std::runtime_error("Warp3D Perlin terrain blend differs from the managed reference");

        splat.textured = true;
        splat.height_ranges = {};
        std::array<MapBitmap, 4> splat_textures;
        auto& patch = splat_textures[0];
        patch = MapBitmap{16, 16, std::vector<std::uint8_t>(16 * 16 * 3, 50)};
        patch.pixels[0] = 100;
        patch.pixels[16 * 3] = 200;
        const auto tiled = render_warp_terrain_splat(splat_heights, 3, 2, splat, splat_textures);
        splat.average_textures = true;
        const auto averaged = render_warp_terrain_splat(splat_heights, 3, 2, splat, splat_textures);
        if (tiled.pixels[0] != 100 || tiled.pixels[9] != 200 ||
            averaged.pixels != std::vector<std::uint8_t>(18, 50))
            throw std::runtime_error("Warp3D terrain patch sampling or averaging differs");
        const std::array<float, 12> map_heights{
            20.f, 30.f, 20.f, 20.f,
            80.f, 20.f, 25.f, 20.f,
            20.f, 25.f, 20.f, 35.f,
        };
        const auto tile = render_shaded_map_tile(map_heights, 4, 3, 20.f);
        const auto pixel = [&](std::size_t x, std::size_t row) {
            const auto offset = (row * 4 + x) * 3;
            return std::array{tile[offset], tile[offset + 1], tile[offset + 2]};
        };
        if (tile.size() != 36 ||
            pixel(3, 0) != std::array<std::uint8_t, 3>{35, 100, 35} ||
            pixel(0, 1) != std::array<std::uint8_t, 3>{80, 100, 80} ||
            pixel(1, 2) != std::array<std::uint8_t, 3>{0, 54, 0} ||
            pixel(0, 2) != std::array<std::uint8_t, 3>{29, 71, 95})
            throw std::runtime_error("Shaded map tile colors or terrain orientation differ");
        const std::array<float, 4> map_elevations{5.f, 45.f, 15.f, 120.f};
        const std::array<float, 4> lower{90.f, 90.f, 90.f, 90.f};
        const std::array<float, 4> upper{100.f, 100.f, 100.f, 100.f};
        TexturedMapPalette palette;
        palette.land.front() = {80, 80, 80};
        palette.land.back() = {220, 220, 220};
        const auto textured = render_textured_map_tile(map_elevations, 2, 2, 20.f, lower, upper, palette);
        const std::vector<std::uint8_t> expected_textured{
            29, 71, 95, 220, 220, 220,
            0, 0, 0, 80, 80, 80,
        };
        if (textured != expected_textured)
            throw std::runtime_error("Textured map terrain bands or orientation differ");
        const auto volume_prim = [](std::uint32_t id, opensim::framework::Vector3 position,
                                    std::array<float, 4> tint) {
            auto part = SceneObject::primitive({}, position, {4.f, 4.f, 2.f});
            part.local_id = id;
            opensim::framework::TextureEntry textures(opensim::framework::TextureEntry::white_texture);
            textures.default_texture->color(tint);
            part.texture_entry = textures.bytes();
            return part;
        };
        auto root = volume_prim(1, {4.f, 5.f, 21.f}, {0.f, 1.f, 1.f, 1.f});
        auto child = volume_prim(2, {2.f, 0.f, 4.f}, {1.f, 0.f, 1.f, 1.f});
        child.parent_id = 1;
        auto airborne = volume_prim(3, {4.f, 5.f, 276.f}, {1.f, 1.f, 0.f, 1.f});
        auto tree = volume_prim(4, {4.f, 5.f, 27.f}, {1.f, 1.f, 0.f, 1.f});
        tree.shape.pcode = 255;
        const std::array volumes{airborne, child, tree, root};
        const std::vector<float> volume_heights(16 * 16, 20.f);
        std::vector<std::uint8_t> volume_pixels(16 * 16 * 3, 73);
        draw_map_object_volumes(volume_pixels, volume_heights, 16, 16, volumes);
        const auto volume_pixel = [&](std::size_t x, std::size_t y) {
            const auto offset = (y * 16 + x) * 3;
            return MapColor{volume_pixels[offset], volume_pixels[offset + 1], volume_pixels[offset + 2]};
        };
        if (volume_pixel(2, 8) != MapColor{255, 0, 0} ||
            volume_pixel(4, 8) != MapColor{0, 255, 0} ||
            volume_pixel(7, 11) != MapColor{0, 255, 0} ||
            volume_pixel(8, 11) != MapColor{73, 73, 73} ||
            volume_pixel(4, 12) != MapColor{73, 73, 73})
            throw std::runtime_error("Legacy map object volume projection differs");
        auto rotated = volume_prim(5, {8.f, 8.f, 25.f}, {0.f, 1.f, 1.f, 1.f});
        rotated.rotation = {0.f, 0.f, .38268343f, .92387953f};
        std::fill(volume_pixels.begin(), volume_pixels.end(), 73);
        draw_map_object_volumes(volume_pixels, volume_heights, 16, 16, std::span(&rotated, 1));
        if (volume_pixel(8, 4) != MapColor{73, 73, 73} ||
            volume_pixel(7, 5) != MapColor{255, 0, 0} ||
            volume_pixel(8, 5) != MapColor{255, 0, 0} ||
            volume_pixel(9, 5) != MapColor{73, 73, 73})
            throw std::runtime_error("Rotated map volume scanlines differ");
        const std::array<std::uint8_t, 4> sale_cells{1, 0, 0, 1};
        const auto overlay = render_parcel_map_tile(sale_cells, 8, 8);
        const std::array<std::uint8_t, 4> yellow{249, 223, 9, 192};
        if (overlay.size() != 8 * 8 * 4)
            throw std::runtime_error("Parcel overlay dimensions differ");
        for (std::size_t y = 0; y < 8; ++y) {
            for (std::size_t x = 0; x < 8; ++x) {
                const auto expected = (x < 4) != (y < 4) ? yellow : std::array<std::uint8_t, 4>{};
                if (!std::equal(expected.begin(), expected.end(), overlay.begin() + (y * 8 + x) * 4))
                    throw std::runtime_error("Parcel overlay colors or orientation differ");
            }
        }
        const std::array<std::uint8_t, 4> no_sale_cells{};
        if (!render_parcel_map_tile(no_sale_cells, 8, 8).empty())
            throw std::runtime_error("Empty parcel overlay should have no asset");
        const std::vector<std::uint8_t> flat_pixels(512 * 256 * 3, 73);
        const auto resized = resize_map_tile(flat_pixels, 512, 256, 256, 128);
        if (resized != std::vector<std::uint8_t>(256 * 128 * 3, 73))
            throw std::runtime_error("Resizing a rectangular map tile changed its flat color");
        Terrain t(256, 256);
        t.clear_land();
        std::vector<TerrainPatchCoordinate> p{{0, 0}};
        auto a = terrain_layer_packets(t, p);
        if (a.size() != 1 || a[0].type != 76 || a[0].data.size() != 14 || a[0].data[0] != 8 ||
            a[0].data[1] != 1 || a[0].data[2] != 16 || a[0].data[3] != 76)
            throw std::runtime_error("flat patch framing");
        auto empty = terrain_layer_packets(t, {});
        if (empty[0].data.size() != 5 || empty[0].data.back() != 97)
            throw std::runtime_error("empty stream");
        Terrain offset_patch(48, 32);
        offset_patch.clear_land();
        offset_patch.set(16, 31, -100.f);
        offset_patch.set(17, 31, -9.f);
        offset_patch.set(31, 16, 37.f);
        offset_patch.set(32, 16, 99.f);
        const auto [patch_low, patch_high] = offset_patch.patch_min_max(1, 1);
        if (patch_low != -9.f || patch_high != 37.f || offset_patch.patch(1, 1)[15 * 16] != -100.f)
            throw std::runtime_error(
                "Offset patch must use region stride and preserve the source first-column omission");
        offset_patch.clear_taint();
        Terrain replacement(offset_patch);
        replacement.set(20, 18, 47.f);
        replacement.set(21, 18, 48.f);
        const auto revision = offset_patch.revision();
        offset_patch.replace_heights(replacement);
        offset_patch.replace_heights(replacement);
        if (offset_patch.get(20, 18) != 47.f || offset_patch.get(21, 18) != 48.f ||
            offset_patch.revision() != revision + 2 || offset_patch.tainted_count() != 1 ||
            !offset_patch.tainted_patch(1, 1))
            throw std::runtime_error("Terrain replacement must mark only changed patches and revisions");
        p = {{16, 0}};
        bool threw = false;
        try {
            terrain_layer_packets(t, p);
        } catch (const std::out_of_range &) {
            threw = true;
        }
        if (!threw)
            throw std::runtime_error("patch bounds");
        Terrain stored(32, 16);
        stored.set(6, 7, 12.3456f);
        stored.set(7, 6, -9.8766f);
        stored.set(0, 0, 0.0625f);
        stored.set(1, 0, -0.0625f);
        stored.set(2, 0, -0.0001f);
        stored.set(3, 0, 100000000.f);
        stored.set(4, 0, 99999992.f);
        Terrain loaded(32, 16);
        loaded.load_database_blob(Terrain::database_format, stored.database_blob());
        if (loaded.get(6, 7) != 12.346f || loaded.get(7, 6) != -9.877f || loaded.tainted())
            throw std::runtime_error(
                "revision 23 terrain roundtrip preserves orientation and millimeter rounding");
        // MathF.Round(height, 3, MidpointRounding.AwayFromZero) reference bits.
        const std::uint32_t rounded[]{0x3d810625, 0xbd810625, 0x80000000, 0x4cbebc20, 0x4cbebc1f};
        for (int x = 0; x < 5; ++x)
            if (std::bit_cast<std::uint32_t>(loaded.get(x, 0)) != rounded[x])
                throw std::runtime_error("revision 23 height rounding differs from MathF.Round");
        // Revision 27 stores signed 16-bit heights in row order after four int32 fields.
        const unsigned char compressed_fixture[]{27,  0, 0, 0, 2,   0, 0,  0,   2, 0,   0,   0,
                                                 100, 0, 0, 0, 125, 0, 56, 255, 0, 128, 255, 127};
        Terrain compressed(16, 16);
        compressed.load_database_blob(
            27, {reinterpret_cast<const char *>(compressed_fixture), sizeof compressed_fixture});
        if (compressed.get(0, 0) != 1.25f || compressed.get(1, 0) != -2.f ||
            compressed.get(0, 1) != -327.68f || compressed.get(1, 1) != 327.67f ||
            compressed.get(2, 0) != Terrain::default_height || compressed.tainted())
            throw std::runtime_error("revision 27 terrain signed heights, orientation or default fill");
        const char oversized_header[]{1, 32, 0, 0, 1, 0, 0, 0, 0, 0, 0, 64};
        Terrain narrow(1, 1);
        narrow.load_database_blob(22, {oversized_header, sizeof oversized_header});
        if (narrow.get(0, 0) != 2.f)
            throw std::runtime_error("revision 22 dimensions must clamp to the region");
        const char empty_header[]{0, 0, 0, 0, 1, 0, 0, 0};
        narrow.load_database_blob(22, {empty_header, sizeof empty_header});
        if (narrow.get(0, 0) != Terrain::default_height)
            throw std::runtime_error("revision 22 empty dimensions must restore default height");
        std::string legacy(256 * 256 * 8, '\0');
        const auto legacy_sample = [&](unsigned x, unsigned y, double height) {
            const auto bits = std::bit_cast<std::uint64_t>(height);
            const auto offset = (x * 256 + y) * 8;
            for (unsigned byte = 0; byte < 8; ++byte)
                legacy[offset + byte] = static_cast<char>(bits >> (8 * byte));
        };
        legacy_sample(2, 9, -17.125);
        legacy_sample(9, 2, 124.5);
        legacy_sample(255, 255, 15.25);
        Terrain original(272, 256);
        original.load_database_blob(1, legacy);
        if (original.get(2, 9) != -17.125f || original.get(9, 2) != 124.5f ||
            original.get(255, 255) != 15.25f || original.get(256, 255) != Terrain::default_height ||
            original.tainted())
            throw std::runtime_error("legacy terrain column order, heights or default fill");
        const std::uint8_t gray16[]{137, 80,  78,  71,  13,  10,  26, 10, 0,  0,   0,   13, 73, 72,
                                    68,  82,  0,   0,   0,   2,   0,  0,  0,  1,   16,  0,  0,  0,
                                    0,   129, 217, 252, 21,  0,   0,  0,  13, 73,  68,  65, 84, 120,
                                    156, 99,  16,  50,  105, 104, 0,  0,  2,  105, 1,   71, 78, 219,
                                    211, 79,  0,   0,   0,   0,   73, 69, 78, 68,  174, 66, 96, 130};
        const std::uint8_t gray_alpha16[]{
            137, 80,  78, 71,  13,  10, 26, 10, 0,  0,  0,   13,  73,  72,  68, 82, 0,  0,  0,
            2,   0,   0,  0,   1,   16, 4,  0,  0,  0,  14,  187, 107, 66,  0,  0,  0,  17, 73,
            68,  65,  84, 120, 156, 99, 16, 50, 97, 96, 104, 104, 104, 104, 0,  0,  7,  5,  2,
            71,  128, 81, 48,  6,   0,  0,  0,  0,  73, 69,  78,  68,  174, 66, 96, 130};
        const std::uint8_t rgba16[]{137, 80,  78,  71,  13,  10,  26,  10,  0,   0,   0,   13,  73,  72,
                                    68,  82,  0,   0,   0,   2,   0,   0,   0,   1,   16,  6,   0,   0,
                                    0,   164, 178, 163, 201, 0,   0,   0,   25,  73,  68,  65,  84,  120,
                                    156, 99,  16,  50,  9,   171, 152, 181, 135, 129, 161, 161, 161, 225,
                                    127, 3,   67,  67,  3,   0,   49,  80,  6,   106, 176, 151, 200, 102,
                                    0,   0,   0,   0,   73,  69,  78,  68,  174, 66,  96,  130};
        const auto check_png = [](std::span<const std::uint8_t> png, std::uint32_t first) {
            Terrain terrain(16, 16);
            terrain.clear_land();
            terrain.merge_png(png);
            // Windows System.Drawing GetPixel().GetBrightness() * 128, before storage rounding.
            if (std::bit_cast<std::uint32_t>(terrain.get(0, 0)) != first ||
                std::bit_cast<std::uint32_t>(terrain.get(1, 0)) != 0x42808081 ||
                terrain.get(2, 0) != Terrain::default_height)
                throw std::runtime_error("16-bit PNG terrain conversion");
        };
        check_png(gray16, 0x41109091);
        check_png(gray_alpha16, 0x41109091);
        check_png(rgba16, 0x422cacad);
        Terrain raw(16, 16);
        raw.clear_land();
        std::vector<std::uint8_t> raw32(256 * 256 * 4);
        const std::uint32_t words[]{0x7fc00000, 0x7f800000, 0xff800000, 0x42280000};
        for (unsigned x = 0; x < 4; ++x)
            for (unsigned byte = 0; byte < 4; ++byte)
                raw32[4 * x + byte] = static_cast<std::uint8_t>(words[x] >> (8 * byte));
        raw.merge_raw(raw32, true);
        if (raw.get(0, 0) != 0.f || raw.get(1, 0) != 0.f || raw.get(2, 0) != 0.f || raw.get(3, 0) != 42.f)
            throw std::runtime_error("RAW32 terrain must zero non-finite samples and retain finite samples");
        Terrain llraw(256, 256);
        std::vector<std::uint8_t> compact(256 * 256 * 13 - 11);
        compact[0] = 20;
        compact[1] = 128;
        compact[compact.size() - 2] = 42;
        compact.back() = 128;
        llraw.merge_raw(compact, false);
        if (llraw.get(0, 255) != 20.f || llraw.get(255, 0) != 42.f)
            throw std::runtime_error("LLRAW terrain must read the last height pair without unused channels");
        compact.pop_back();
        bool truncated = false;
        try {
            llraw.merge_raw(compact, false);
        } catch (const std::runtime_error &) {
            truncated = true;
        }
        if (!truncated || llraw.get(255, 0) != 42.f)
            throw std::runtime_error("Incomplete LLRAW height pair must leave terrain intact");
        Terrain raw_export(16, 16);
        const float raw_heights[]{0, 21, 24, 27, 42, 10.75f, 60, 127.9f, 508, 4094};
        const std::uint16_t raw_pairs[]{0x2b00, 0x1c60, 0x3040, 0x2460, 0x20a8,
                                        0x202b, 0x20f0, 0x996b, 0xfffe, 0xffff};
        for (int x = 0; x < 10; ++x)
            raw_export.set(x, 15, raw_heights[x]);
        const auto raw_bytes = raw_export.llraw();
        for (int x = 0; x < 10; ++x) {
            const auto *sample = raw_bytes.data() + 13 * x;
            if ((sample[0] | (sample[1] << 8)) != raw_pairs[x] || sample[11] != sample[0] ||
                sample[12] != sample[1])
                throw std::runtime_error(
                    "LLRAW quantization pair differs from .NET Array.Sort and BinarySearch");
        }
        Terrain terragen(16, 16);
        terragen.clear_land(10.f);
        terragen.set(3, 7, 60.f);
        const auto ter = terragen.terragen();
        if (ter.size() != 68 + 16 * 16 * 2 || ter.substr(56, 4) != "ALTW" ||
            ter.substr(ter.size() - 4) != "EOF ")
            throw std::runtime_error("Terragen export framing");
        Terrain decoded_terragen(16, 16);
        decoded_terragen.merge_terragen({reinterpret_cast<const std::uint8_t *>(ter.data()), ter.size()},
                                        true);
        if (decoded_terragen.get(0, 0) != 10.f || decoded_terragen.get(3, 7) != 60.f - 50.f / 65536.f)
            throw std::runtime_error("Terragen export preserves orientation and signed elevation endpoints");
        Terrain terragen_ties(16, 16);
        const float elevations[]{-32.f, 32.f, 0.5f / 1024.f, 1.5f / 1024.f, -1.5f / 1024.f};
        for (int x = 0; x < 5; ++x)
            terragen_ties.set(x, 0, elevations[x]);
        const auto tie_export = terragen_ties.terragen();
        const std::string reference_elevations("\0\x80\xff\x7f\0\0\x02\0\xfe\xff", 10);
        if (tie_export.substr(60, 4) != std::string("\x40\0\0\0", 4) ||
            tie_export.substr(64, 10) != reference_elevations)
            throw std::runtime_error("Terragen elevation encoding differs from Convert.ToInt16");
        terragen_ties.clear_land(33.25f);
        const auto flat_export = terragen_ties.terragen();
        if (flat_export.substr(60, 6) != std::string("\0\0\x21\0\xff\x7f", 6))
            throw std::runtime_error(
                "Terragen flat heightfield preserves the source zero scale and floored base");
        Terrain bitmap(16, 16);
        bitmap.set(0, 0, 10.75f);
        bitmap.set(1, 0, -2.f);
        bitmap.set(0, 1, 140.f);
        bitmap.set(1, 1, 1.5f);
        // Windows System.Drawing Bitmap.Save header and bottom-up BGRA pixel order.
        const char bitmap_header[] = "BM\x36\x04\0\0\0\0\0\0\x36\0\0\0\x28\0\0\0\x10\0\0\0\x10\0\0\0"
                                     "\x01\0\x20\0\0\0\0\0\0\0\0\0\xc4\x0e\0\0\xc4\x0e\0\0\0\0\0\0\0\0\0\0";
        std::string reference_bmp(bitmap_header, sizeof bitmap_header - 1);
        for (int i = 0; i < 16 * 16; ++i)
            reference_bmp.append("\0\0\0\xff", 4);
        reference_bmp.replace(54, 4, "\x14\x14\x14\xff", 4);
        reference_bmp.replace(54 + 16 * 4, 8, "\xff\xff\xff\xff\x02\x02\x02\xff", 8);
        if (bitmap.bitmap() != reference_bmp)
            throw std::runtime_error("Terrain BMP output differs from Windows System.Drawing");
        const auto png = bitmap.png();
        Terrain decoded_png(16, 16);
        decoded_png.merge_png({reinterpret_cast<const std::uint8_t *>(png.data()), png.size()}, true);
        if (decoded_png.get(0, 0) != 20.f / 255.f * 128.f || decoded_png.get(1, 0) != 0.f ||
            decoded_png.get(0, 1) != 128.f || decoded_png.get(1, 1) != 2.f / 255.f * 128.f)
            throw std::runtime_error(
                "Terrain PNG roundtrip preserves orientation and source grayscale quantization");
        std::cout << "PASS: terrain layer framing, database roundtrip and 16-bit PNG conversion\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
