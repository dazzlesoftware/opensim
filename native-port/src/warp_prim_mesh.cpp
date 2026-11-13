// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/map_tile.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/framework/texture_entry.hpp"
#include "opensim/physics/mesh_asset.hpp"
#include "opensim/scene/world.hpp"

#include <cmath>
#include <map>

namespace opensim::scene {
namespace {

using framework::Vector3;
constexpr std::array<Vector3, 8> corners{{
    {-.5f, -.5f, .5f}, {.5f, -.5f, .5f}, {.5f, .5f, .5f}, {-.5f, .5f, .5f},
    {-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f},
}};
constexpr std::array<std::array<unsigned, 4>, 6> face_corners{{
    {0, 1, 2, 3}, {0, 4, 1, 5}, {1, 5, 2, 6}, {2, 6, 3, 7}, {3, 7, 0, 4}, {6, 5, 4, 7},
}};
constexpr std::array<Vector3, 6> face_normals{{
    {0, 0, 1}, {0, -1, 0}, {1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, 0, -1},
}};
constexpr std::array<Vector3, 6> prism_corners{{
    {.5f, 0, .5f}, {-.25f, .4330127f, .5f}, {-.25f, -.4330127f, .5f},
    {.5f, 0, -.5f}, {-.25f, .4330127f, -.5f}, {-.25f, -.4330127f, -.5f},
}};
constexpr std::array<std::array<unsigned, 4>, 5> prism_face_corners{{
    {0, 1, 2, 0}, {0, 3, 1, 4}, {1, 4, 2, 5}, {2, 5, 0, 3}, {5, 4, 3, 5},
}};
constexpr std::array<Vector3, 5> prism_normals{{
    {0, 0, 1}, {.5f, .86602545f, 0}, {-1, 0, 0}, {.5f, -.86602545f, 0}, {0, 0, -1},
}};

constexpr float side_top = .999999f;

void discard_opaque_alpha(MapBitmap& texture) {
    if (texture.channels != 4 || texture.pixels.size() % 4 != 0)
        return;
    for (std::size_t alpha = 3; alpha < texture.pixels.size(); alpha += 4) {
        if (texture.pixels[alpha] != 255)
            return;
    }

    const auto pixel_count = texture.pixels.size() / 4;
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const auto source = pixel * 4;
        const auto target = pixel * 3;
        texture.pixels[target] = texture.pixels[source];
        texture.pixels[target + 1] = texture.pixels[source + 1];
        texture.pixels[target + 2] = texture.pixels[source + 2];
    }
    texture.pixels.resize(pixel_count * 3);
    texture.channels = 3;
}

Vector3 cylinder_radial(std::uint32_t sample, std::uint32_t segments) {
    constexpr std::array<float, 7> cosine{
        1.f, .9659258f, .8660254f, .70710677f, .5f, .25881904f, 0.f,
    };
    const auto angle = sample * (24 / segments) % 24;
    const auto step = angle % 6;
    const float x = cosine[step];
    const float y = cosine[6 - step];
    switch (angle / 6) {
    case 0: return {x, y, 0.f};
    case 1: return {-y, x, 0.f};
    case 2: return {-x, -y, 0.f};
    default: return {y, -x, 0.f};
    }
}

std::array<float, 2> planar_uv(Vector3 corner, Vector3 normal, Vector3 scale) {
    const Vector3 position{corner.x * scale.x, corner.y * scale.y, corner.z * scale.z};
    if (normal.x >= .5f)
        return {2.f * position.y, 2.f * (position.x * normal.z - position.z * normal.x)};
    if (normal.x <= -.5f)
        return {-2.f * position.y, 2.f * (-position.x * normal.z + position.z * normal.x)};
    if (normal.y > 0.f)
        return {-2.f * position.x, 2.f * (position.y * normal.z - position.z * normal.y)};
    return {2.f * position.x, 2.f * (-position.y * normal.z + position.z * normal.y)};
}

struct PrimTransform {
    Vector3 position;
    Vector3 scale;
    framework::Quaternion rotation;

    WarpTerrainVertex vertex(Vector3 point, float u, float v) const {
        const auto offset = framework::sdk_rotate_vector(
            {point.x * scale.x, point.y * scale.y, point.z * scale.z}, rotation);
        return {position.x + offset.x, position.z + offset.z, position.y + offset.y, u, v};
    }
};

PrimTransform prim_transform(const SceneObject& part, const SceneObject* root) {
    auto position = part.position;
    auto rotation = part.rotation;
    if (root && part.parent_id) {
        const auto offset = framework::sdk_rotate_vector(position, root->rotation);
        position = {root->position.x + offset.x, root->position.y + offset.y, root->position.z + offset.z};
        rotation = framework::sdk_multiply_quaternion(root->rotation, rotation);
    }
    return {position, part.scale, rotation};
}

physics::AssetMeshLod projected_lod(const PrimTransform& transform, const MapBitmap& image) {
    const auto x = framework::sdk_rotate_vector({transform.scale.x, 0.f, 0.f}, transform.rotation);
    const auto y = framework::sdk_rotate_vector({0.f, transform.scale.y, 0.f}, transform.rotation);
    const auto z = framework::sdk_rotate_vector({0.f, 0.f, transform.scale.z}, transform.rotation);
    const float width = std::abs(x.x) + std::abs(y.x) + std::abs(z.x);
    const float height = std::abs(x.y) + std::abs(y.y) + std::abs(z.y);
    const float viewport_width = static_cast<float>(image.width);
    const float viewport_height = static_cast<float>(image.height);
    const float screen_factor = 4.f * (width / viewport_width) * (height / viewport_height);
    if (screen_factor >= 65536.f)
        return physics::AssetMeshLod::Lowest;
    if (screen_factor >= 4096.f)
        return physics::AssetMeshLod::Low;
    if (screen_factor >= 256.f)
        return physics::AssetMeshLod::Medium;
    return physics::AssetMeshLod::High;
}

WarpTerrainMesh transform_render_face(const physics::AssetRenderFace& face, const PrimTransform& transform) {
    WarpTerrainMesh result;
    result.vertices.reserve(face.geometry.vertices.size());
    for (std::size_t index = 0; index < face.geometry.vertices.size(); ++index) {
        const auto uv = face.texcoords.empty() ? std::array{0.f, 0.f} : face.texcoords[index];
        result.vertices.push_back(transform.vertex(face.geometry.vertices[index], uv[0], uv[1]));
    }
    const auto& indices = face.geometry.indices;
    result.triangles.reserve(indices.size() / 3);
    for (std::size_t index = 0; index < indices.size(); index += 3)
        result.triangles.push_back({indices[index], indices[index + 1], indices[index + 2]});
    return result;
}

struct CylinderVertex {
    Vector3 position;
    Vector3 normal;
};

CylinderVertex cylinder_vertex(std::size_t face, std::uint32_t index, std::uint32_t segments) {
    if (face == 1) {
        const auto normal = cylinder_radial(index / 2, segments);
        return {{.5f * normal.x, .5f * normal.y, index % 2 ? -.5f : .5f}, normal};
    }

    const bool top = face == 0;
    const Vector3 normal{0.f, 0.f, top ? 1.f : -1.f};
    const float z = .5f * normal.z;
    if ((top && index == 0) || (!top && index == 2))
        return {{0.f, 0.f, z}, normal};

    // The bottom fan starts with ring samples 1 and 0, followed by its center.
    const auto sample = !top && index < 2 ? 1 - index : index - 1;
    const auto radial = cylinder_radial(sample, segments);
    return {{.5f * radial.x, .5f * radial.y, z}, normal};
}

std::array<WarpTerrainMesh, 3> build_cylinder_faces(const PrimTransform& transform, std::uint32_t segments) {
    std::array<WarpTerrainMesh, 3> faces;
    auto& top = faces[0];
    auto& side = faces[1];
    auto& bottom = faces[2];

    for (std::uint32_t index = 0; index < segments + 2; ++index) {
        const auto top_point = cylinder_vertex(0, index, segments).position;
        const auto bottom_point = cylinder_vertex(2, index, segments).position;
        top.vertices.push_back(transform.vertex(top_point, .5f + top_point.x, .5f + top_point.y));
        bottom.vertices.push_back(transform.vertex(bottom_point, .5f + bottom_point.x, .5f - bottom_point.y));
    }
    bottom.triangles.push_back({0, 1, 2});
    bottom.triangles.push_back({3, 0, 2});
    for (std::uint32_t edge = 0; edge < segments; ++edge)
        top.triangles.push_back({0, edge + 1, edge + 2});
    for (std::uint32_t index = 4; index < segments + 2; ++index)
        bottom.triangles.push_back({index, index - 1, 2});

    for (std::uint32_t sample = 0; sample <= segments; ++sample) {
        const auto index = sample * 2;
        const auto upper = cylinder_vertex(1, index, segments).position;
        const auto lower = cylinder_vertex(1, index + 1, segments).position;
        const float u = static_cast<float>(sample) / static_cast<float>(segments);
        side.vertices.push_back(transform.vertex(upper, u, side_top));
        side.vertices.push_back(transform.vertex(lower, u, 0.f));
        // The final pair connects the two coincident sides of the UV seam.
        const auto next = sample == segments ? 0 : index + 2;
        side.triangles.push_back({index, index + 1, next});
        side.triangles.push_back({next, index + 1, next + 1});
    }
    return faces;
}

} // namespace

std::array<WarpTerrainMesh, 6> build_warp_box_faces(const SceneObject& part, const SceneObject* root) {
    constexpr std::array<std::array<float, 2>, 4> top_uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    constexpr std::array<std::array<float, 2>, 4> side_uv{{{0, 1}, {0, 0}, {1, 1}, {1, 0}}};
    constexpr std::array<std::array<float, 2>, 4> bottom_uv{{{1, 0}, {1, 1}, {0, 1}, {0, 0}}};

    const auto transform = prim_transform(part, root);

    std::array<WarpTerrainMesh, 6> faces;
    for (std::size_t index = 0; index < faces.size(); ++index) {
        auto& face = faces[index];
        const auto& uv = index == 0 ? top_uv : index == 5 ? bottom_uv : side_uv;
        face.vertices.reserve(4);
        for (std::size_t vertex = 0; vertex < 4; ++vertex) {
            const auto& point = corners[face_corners[index][vertex]];
            face.vertices.push_back(transform.vertex(point, uv[vertex][0], uv[vertex][1]));
        }
        if (index == 0)
            face.triangles = {{0, 1, 2}, {0, 2, 3}};
        else if (index == 5)
            face.triangles = {{0, 1, 2}, {3, 0, 2}};
        else
            face.triangles = {{0, 1, 2}, {2, 1, 3}};
    }
    return faces;
}

std::array<WarpTerrainMesh, 5> build_warp_prism_faces(const SceneObject& part, const SceneObject* root) {
    constexpr std::array<std::array<float, 2>, 4> side_uv{{{0, side_top}, {0, 0}, {1, side_top}, {1, 0}}};
    const auto transform = prim_transform(part, root);
    std::array<WarpTerrainMesh, 5> faces;
    for (std::size_t index = 0; index < faces.size(); ++index) {
        auto& face = faces[index];
        const bool top = index == 0;
        const bool bottom = index == faces.size() - 1;
        const bool cap = top || bottom;
        const unsigned count = cap ? 3 : 4;
        face.vertices.reserve(count);
        for (unsigned vertex = 0; vertex < count; ++vertex) {
            const auto point = prism_corners[prism_face_corners[index][vertex]];
            auto [u, v] = side_uv[vertex];
            if (cap) {
                u = point.x + .5f;
                v = top ? .5f + point.y : .5f - point.y;
            }
            face.vertices.push_back(transform.vertex(point, u, v));
        }
        face.triangles.push_back({0, 1, 2});
        if (!cap)
            face.triangles.push_back({2, 1, 3});
    }
    return faces;
}

void draw_warp_prims(WarpMapRaster& raster, std::span<const SceneObject> objects,
                     const std::function<MapBitmap(const framework::Uuid&)>& load_texture,
                     bool texture_prims, float minimum_height, float maximum_height,
                     const std::function<std::shared_ptr<framework::AssetBase>(framework::Uuid)>& load_mesh) {
    std::map<std::uint32_t, const SceneObject*> roots;
    for (const auto& object : objects)
        if (!object.parent_id && !object.is_attachment)
            roots.emplace(object.local_id, &object);

    static const framework::Uuid invisible("e97cf410-8e61-7005-ec06-629eba4cd1fb");
    static const auto white = std::make_shared<const MapBitmap>(MapBitmap{1, 1, {255, 255, 255}});
    std::map<framework::Uuid, std::shared_ptr<const MapBitmap>> textures;
    for (const auto& part : objects) {
        const auto& shape = part.shape;
        constexpr unsigned profile_mask = 0x0f;
        constexpr unsigned square_profile = 1;
        constexpr unsigned triangle_profile = 3;
        constexpr unsigned circle_profile = 0;
        constexpr unsigned primitive_pcode = 9;
        constexpr unsigned linear_path = 16;
        constexpr unsigned full_path_scale = 100;
        const auto profile = shape.profile_curve & profile_mask;
        const bool prism = profile == triangle_profile;
        const bool cylinder = profile == circle_profile;
        const bool supported_profile = profile == square_profile || prism || cylinder;
        const bool cut_or_hollow = shape.path_begin || shape.path_end ||
            shape.profile_begin || shape.profile_end || shape.profile_hollow;
        const bool deformed_path = shape.path_scale_x != full_path_scale ||
            shape.path_scale_y != full_path_scale || shape.path_shear_x || shape.path_shear_y ||
            shape.path_twist_begin || shape.path_twist || shape.path_taper_x || shape.path_taper_y ||
            shape.path_skew || shape.path_radius_offset || shape.path_revolutions;
        if (part.is_attachment || shape.pcode != primitive_pcode)
            continue;

        const SceneObject* root = nullptr;
        float elevation = part.position.z;
        if (part.parent_id) {
            const auto parent = roots.find(part.parent_id);
            if (parent == roots.end())
                continue;
            root = parent->second;
            elevation = root->position.z + framework::sdk_rotate_vector(part.position, root->rotation).z;
        }
        if (!std::isfinite(elevation) || elevation < minimum_height || elevation > maximum_height)
            continue;

        try {
            const auto transform = prim_transform(part, root);
            std::optional<std::vector<physics::AssetRenderFace>> render_faces;
            if (load_mesh && part.extra_params.size() > 1) {
                auto geometry = framework::PrimitiveBaseShapeCore::create([](const std::string&) {});
                geometry.read_extra_params(std::make_shared<framework::ShapeByteArray>(part.extra_params));
                constexpr unsigned mesh_sculpt_type = 5;
                if (geometry.sculpt_entry && !geometry.sculpt_texture.is_zero()) {
                    const auto lod = projected_lod(transform, raster.image());
                    if ((geometry.sculpt_type & 7) == mesh_sculpt_type) {
                        const auto asset = load_mesh(geometry.sculpt_texture);
                        if (asset && asset->data && !asset->data->empty()) {
                            const std::string_view bytes{reinterpret_cast<const char*>(asset->data->data()),
                                                         asset->data->size()};
                            render_faces = physics::decode_asset_render_mesh(bytes, lod);
                        }
                    } else if (load_texture) {
                        const auto bitmap = load_texture(geometry.sculpt_texture);
                        if (auto face = physics::build_sculpt_render_face(
                                bitmap.pixels, bitmap.width, bitmap.height, bitmap.channels, geometry.sculpt_type, lod)) {
                            render_faces.emplace();
                            render_faces->push_back(std::move(*face));
                        }
                    }
                }
            }
            if (!render_faces && (shape.path_curve != linear_path || !supported_profile || cut_or_hollow || deformed_path))
                continue;

            framework::TextureEntry entry(part.texture_entry);
            if (!entry.default_texture)
                continue;
            std::array<WarpTerrainMesh, 6> box_faces;
            std::array<WarpTerrainMesh, 5> prism_faces;
            std::array<WarpTerrainMesh, 3> cylinder_faces;
            std::vector<WarpTerrainMesh> asset_faces;
            std::uint32_t segments = 0;
            std::span<WarpTerrainMesh> faces;
            if (render_faces) {
                asset_faces.reserve(render_faces->size());
                for (const auto& face : *render_faces)
                    asset_faces.push_back(transform_render_face(face, transform));
                faces = asset_faces;
            } else if (cylinder) {
                const auto lod = projected_lod(transform, raster.image());
                constexpr std::array<std::uint32_t, 4> segments_by_lod{6, 12, 24, 24};
                segments = segments_by_lod[static_cast<std::size_t>(lod)];
                cylinder_faces = build_cylinder_faces(transform, segments);
                faces = cylinder_faces;
            } else if (prism) {
                prism_faces = build_warp_prism_faces(part, root);
                faces = prism_faces;
            } else {
                box_faces = build_warp_box_faces(part, root);
                faces = box_faces;
            }
            for (std::size_t index = 0; index < faces.size(); ++index) {
                if (faces[index].triangles.empty())
                    continue;
                const auto face = entry.get_face(static_cast<unsigned>(index));
                const auto rgba = face->color();
                const auto opacity = static_cast<unsigned>(rgba[3] * 255.f);
                if (opacity == 0)
                    continue;
                const auto id = face->texture_id();
                if (opacity == 255 && id == invisible)
                    break;
                MapColor tint;
                for (std::size_t channel = 0; channel < tint.size(); ++channel)
                    tint[channel] = static_cast<std::uint8_t>(rgba[channel] * 255.f);

                if (!texture_prims) {
                    raster.draw_tinted(faces[index], white, tint, opacity, false);
                    continue;
                }
                auto texture = textures.find(id);
                if (texture == textures.end()) {
                    auto bitmap = load_texture && !id.is_zero() ? load_texture(id) : MapBitmap{};
                    // Warp3D puts RGBA textures with fully opaque pixels in the opaque pass.
                    discard_opaque_alpha(bitmap);
                    texture = textures.emplace(id, std::make_shared<const MapBitmap>(std::move(bitmap))).first;
                }
                if (texture->second->pixels.empty()) {
                    // Warp3D multiplies the face color by its 0x7f gray material.
                    for (auto& channel : tint)
                        channel = static_cast<std::uint8_t>((channel * 127u + 127u) / 255u);
                    raster.draw_tinted(faces[index], white, tint, opacity, false);
                    continue;
                }
                constexpr std::uint8_t planar = 2;
                const float rotation = face->rotation();
                const float cosine = std::cos(rotation);
                const float sine = std::sin(rotation);
                const float repeat_u = face->repeat_u();
                const float repeat_v = face->repeat_v();
                const float offset_u = face->offset_u() + .5f;
                const float offset_v = face->offset_v() + .5f;
                auto& vertices = faces[index].vertices;
                for (std::size_t point = 0; point < vertices.size(); ++point) {
                    auto& vertex = vertices[point];
                    std::array uv{vertex.u - .5f, .5f - vertex.v};
                    if (face->mapping() == planar) {
                        if (render_faces) {
                            const auto& render_face = (*render_faces)[index];
                            const auto normal = render_face.normals.empty() ? Vector3{} : render_face.normals[point];
                            uv = planar_uv(render_face.geometry.vertices[point], normal, part.scale);
                        } else if (cylinder) {
                            const auto local = cylinder_vertex(index, static_cast<std::uint32_t>(point), segments);
                            uv = planar_uv(local.position, local.normal, part.scale);
                        } else if (prism) {
                            const auto corner = prism_corners[prism_face_corners[index][point]];
                            uv = planar_uv(corner, prism_normals[index], part.scale);
                        } else {
                            const auto corner = corners[face_corners[index][point]];
                            uv = planar_uv(corner, face_normals[index], part.scale);
                        }
                    }
                    const auto [u, v] = uv;
                    const float rotated_u = rotation != 0.f ? u * cosine - v * sine : u;
                    const float rotated_v = rotation != 0.f ? u * sine + v * cosine : v;
                    vertex.u = rotated_u * repeat_u + offset_u;
                    vertex.v = rotated_v * repeat_v + offset_v;
                }
                raster.draw_tinted(faces[index], texture->second, tint, opacity);
            }
        } catch (const std::exception&) {
            // The managed renderer isolates errors to a single prim.
        }
    }
}

} // namespace opensim::scene
