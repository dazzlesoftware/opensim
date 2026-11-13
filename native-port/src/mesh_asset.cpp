// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/physics/mesh_asset.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/sdk_llsd_binary.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>

namespace opensim::physics {
namespace {
using network::Value;
using framework::Vector3;

constexpr std::size_t max_mesh_depth = 32;

Value read_mesh_section(std::string_view payload, const Value& section) {
    if (section.kind != Value::Kind::Map)
        return {};
    const auto offset = network::osd_integer(section.get("offset"));
    const auto size = network::osd_integer(section.get("size"));
    if (offset < 0 || size <= 2 || static_cast<std::size_t>(offset) > payload.size() ||
        static_cast<std::size_t>(size) > payload.size() - static_cast<std::size_t>(offset))
        return {};
    return network::deserialize_zipped_llsd_binary(payload.substr(offset, size), max_mesh_depth);
}

bool read_vector(const Value& value, Vector3& result) {
    if (value.kind != Value::Kind::Array || value.array.size() != 3)
        return false;
    float* components[] = {&result.x, &result.y, &result.z};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& component = value.array[i];
        if (component.kind != Value::Kind::Real && component.kind != Value::Kind::Integer)
            return false;
        *components[i] = static_cast<float>(network::osd_real(component));
        if (!std::isfinite(*components[i]))
            return false;
    }
    return true;
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (std::uint16_t(bytes[offset + 1]) << 8));
}

bool read_texcoord(const Value& value, std::array<float, 2>& result) {
    if (value.kind != Value::Kind::Array || value.array.size() != result.size())
        return false;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto& component = value.array[index];
        if (component.kind != Value::Kind::Real && component.kind != Value::Kind::Integer)
            return false;
        result[index] = static_cast<float>(network::osd_real(component));
        if (!std::isfinite(result[index]))
            return false;
    }
    return true;
}

bool append_submesh(const Value& submesh, AssetPhysicsMesh& mesh) {
    if (submesh.kind != Value::Kind::Map || submesh.contains("NoGeometry"))
        return true;
    const auto positions = network::osd_binary(submesh.get("Position"));
    const auto triangles = network::osd_binary(submesh.get("TriangleList"));
    if (!positions || positions->empty() || !triangles || triangles->empty())
        return true;
    if (positions->size() % 6 || triangles->size() % 6)
        return false;

    Vector3 minimum{-.5f, -.5f, -.5f};
    Vector3 maximum{.5f, .5f, .5f};
    if (submesh.contains("PositionDomain")) {
        const auto& domain = submesh.get("PositionDomain");
        if (!read_vector(domain.get("Min"), minimum) || !read_vector(domain.get("Max"), maximum))
            return false;
    }
    constexpr float inverse_max = 1.f / 65535.f;
    const Vector3 range{(maximum.x - minimum.x) * inverse_max, (maximum.y - minimum.y) * inverse_max,
                        (maximum.z - minimum.z) * inverse_max};
    if (range.x < 0.f || range.y < 0.f || range.z < 0.f ||
        !std::isfinite(range.x) || !std::isfinite(range.y) || !std::isfinite(range.z))
        return false;

    const auto count = positions->size() / 6;
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t i = 0; i < positions->size(); i += 6) {
        const Vector3 vertex{minimum.x + read_u16(*positions, i) * range.x,
                             minimum.y + read_u16(*positions, i + 2) * range.y,
                             minimum.z + read_u16(*positions, i + 4) * range.z};
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
            return false;
        mesh.vertices.push_back(vertex);
    }
    for (std::size_t i = 0; i < triangles->size(); i += 2) {
        const auto index = read_u16(*triangles, i);
        if (index >= count)
            return false;
        mesh.indices.push_back(base + index);
    }
    return true;
}

bool read_render_face(const Value& submesh, AssetRenderFace& face) {
    if (submesh.kind != Value::Kind::Map || !append_submesh(submesh, face.geometry))
        return false;
    if (face.geometry.indices.empty())
        return false;

    const auto vertex_count = face.geometry.vertices.size();
    constexpr float inverse_max = 1.f / 65535.f;
    if (submesh.contains("TexCoord0")) {
        const auto data = network::osd_binary(submesh.get("TexCoord0"));
        if (!data || data->size() != vertex_count * 4)
            return false;
        std::array<float, 2> minimum{}, maximum{};
        const auto& domain = submesh.get("TexCoord0Domain");
        if (!read_texcoord(domain.get("Min"), minimum) || !read_texcoord(domain.get("Max"), maximum))
            return false;
        const float u_step = (maximum[0] - minimum[0]) * inverse_max;
        const float v_step = (maximum[1] - minimum[1]) * inverse_max;
        if (u_step < 0.f || v_step < 0.f || !std::isfinite(u_step) || !std::isfinite(v_step))
            return false;
        face.texcoords.reserve(vertex_count);
        for (std::size_t offset = 0; offset < data->size(); offset += 4) {
            const float u = minimum[0] + read_u16(*data, offset) * u_step;
            const float v = minimum[1] + read_u16(*data, offset + 2) * v_step;
            if (!std::isfinite(u) || !std::isfinite(v))
                return false;
            face.texcoords.push_back({u, v});
        }
    }
    return true;
}

// The fixed interior point lets every new face keep its outward winding.
bool append_hull(std::vector<Vector3> points, AssetPhysicsMesh& mesh) {
    for (const auto point : points)
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            return false;
    using Point = std::array<double, 3>;
    using Face = std::array<std::size_t, 3>;
    const auto less = [](Vector3 a, Vector3 b) {
        return std::array{a.x, a.y, a.z} < std::array{b.x, b.y, b.z};
    };
    if (points.size() != 3)
        std::sort(points.begin(), points.end(), less);
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3)
        return false;
    const auto point = [&](std::size_t i) {
        return Point{points[i].x, points[i].y, points[i].z};
    };
    const auto subtract = [](Point a, Point b) {
        return Point{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    };
    const auto cross = [](Point a, Point b) {
        return Point{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    const auto dot = [](Point a, Point b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const auto normal = [&](Face f) {
        return cross(subtract(point(f[1]), point(f[0])), subtract(point(f[2]), point(f[0])));
    };
    const auto distance = [&](Face f, Point p) {
        const auto n = normal(f);
        const double length = std::sqrt(dot(n, n));
        return length ? dot(n, subtract(p, point(f[0]))) / length : 0.;
    };
    const auto axis = subtract(point(points.size() - 1), point(0));
    const double epsilon = std::sqrt(dot(axis, axis)) * 1.e-8;
    const std::size_t a = 0, b = points.size() - 1;
    std::size_t c = a;
    double widest = 0.;
    for (std::size_t i = 1; i < b; ++i) {
        const auto side = cross(axis, subtract(point(i), point(a)));
        const double width = dot(side, side);
        if (width > widest) {
            widest = width;
            c = i;
        }
    }
    if (c == a)
        return false;
    std::size_t d = a;
    double farthest = epsilon;
    for (std::size_t i = 1; i < b; ++i) {
        const double height = std::abs(distance({a, b, c}, point(i)));
        if (height > farthest) {
            farthest = height;
            d = i;
        }
    }
    std::vector<Face> faces;
    if (d == a) {
        if (points.size() != 3)
            return false;
        faces.push_back({0, 1, 2});
    } else {
        Point interior{};
        for (const auto vertex : {a, b, c, d})
            for (std::size_t axis_index = 0; axis_index < 3; ++axis_index)
                interior[axis_index] += point(vertex)[axis_index] * .25;
        const auto add_face = [&](Face face) {
            if (distance(face, interior) > 0.)
                std::swap(face[1], face[2]);
            faces.push_back(face);
        };
        for (const Face face : {Face{a, b, c}, Face{a, d, b}, Face{a, c, d}, Face{b, d, c}})
            add_face(face);
        for (std::size_t i = 1; i < b; ++i) {
            if (i == c || i == d)
                continue;
            std::set<std::pair<std::size_t, std::size_t>> horizon;
            std::erase_if(faces, [&](Face face) {
                if (distance(face, point(i)) <= epsilon)
                    return false;
                for (std::size_t edge = 0; edge < 3; ++edge) {
                    const auto from = face[edge], to = face[(edge + 1) % 3];
                    if (!horizon.erase({to, from}))
                        horizon.emplace(from, to);
                }
                return true;
            });
            for (const auto& [from, to] : horizon)
                add_face({from, to, i});
        }
    }
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), points.begin(), points.end());
    for (const auto& face : faces)
        for (const auto index : face)
            mesh.indices.push_back(base + static_cast<std::uint32_t>(index));
    return !faces.empty();
}

std::optional<AssetPhysicsMesh> decode_convex(const Value& block, bool convex) {
    if (block.kind != Value::Kind::Map)
        return {};
    Vector3 minimum{-.5f, -.5f, -.5f};
    Vector3 maximum{.5f, .5f, .5f};
    if ((block.contains("Min") && !read_vector(block.get("Min"), minimum)) ||
        (block.contains("Max") && !read_vector(block.get("Max"), maximum)))
        return {};
    constexpr float inverse_max = 1.f / 65535.f;
    const Vector3 range{(maximum.x - minimum.x) * inverse_max, (maximum.y - minimum.y) * inverse_max,
                        (maximum.z - minimum.z) * inverse_max};
    if (range.x < 0.f || range.y < 0.f || range.z < 0.f ||
        !std::isfinite(range.x) || !std::isfinite(range.y) || !std::isfinite(range.z))
        return {};
    const auto points_at = [&](const std::vector<std::uint8_t>& data, std::size_t first, std::size_t count) {
        std::vector<Vector3> points;
        points.reserve(count);
        for (std::size_t i = first; i < first + count * 6; i += 6)
            points.push_back({minimum.x + read_u16(data, i) * range.x,
                              minimum.y + read_u16(data, i + 2) * range.y,
                              minimum.z + read_u16(data, i + 4) * range.z});
        return points;
    };
    AssetPhysicsMesh mesh;
    if (!convex) {
        const auto counts = network::osd_binary(block.get("HullList"));
        const auto data = network::osd_binary(block.get("Positions"));
        if (counts && data && data->size() % 6 == 0) {
            std::size_t total = 0;
            for (const auto count : *counts)
                total += count ? count : 256;
            if (total == data->size() / 6) {
                std::size_t offset = 0;
                for (const auto count : *counts) {
                    const std::size_t vertices = count ? count : 256;
                    append_hull(points_at(*data, offset, vertices), mesh);
                    offset += vertices * 6;
                }
                if (!mesh.indices.empty())
                    return mesh;
            }
        }
    }
    const auto bounds = network::osd_binary(block.get("BoundingVerts"));
    if (bounds && bounds->size() % 6 == 0 && bounds->size() <= 256 * 6 &&
        append_hull(points_at(*bounds, 0, bounds->size() / 6), mesh))
        return mesh;
    return {};
}
}

std::optional<AssetPhysicsMesh> decode_asset_physics_mesh(std::string_view asset, bool convex) {
    try {
        std::size_t header_size = 0;
        const auto header = network::deserialize_sdk_llsd_binary_prefix(asset, header_size, max_mesh_depth);
        if (header.kind != Value::Kind::Map)
            return {};
        const bool triangles = !convex && (header.contains("physics_shape") || header.contains("physics_mesh"));
        const auto& section = header.get(triangles ? (header.contains("physics_shape") ? "physics_shape" :
                                                      "physics_mesh") : "physics_convex");
        const auto decoded = read_mesh_section(asset.substr(header_size), section);
        if (!triangles)
            return decode_convex(decoded, convex);
        if (decoded.kind != Value::Kind::Array)
            return {};
        AssetPhysicsMesh mesh;
        for (const auto& submesh : decoded.array)
            if (!append_submesh(submesh, mesh))
                return {};
        if (mesh.indices.empty())
            return {};
        return mesh;
    } catch (const std::runtime_error&) {
        return {};
    } catch (const std::logic_error&) {
        return {};
    }
}

std::optional<std::vector<AssetRenderFace>> decode_asset_render_mesh(std::string_view asset, AssetMeshLod lod) {
    constexpr std::array sections{"lowest_lod", "low_lod", "medium_lod", "high_lod"};
    try {
        const auto index = static_cast<std::size_t>(lod);
        if (index >= sections.size())
            return {};
        std::size_t header_size = 0;
        const auto header = network::deserialize_sdk_llsd_binary_prefix(asset, header_size, max_mesh_depth);
        if (header.kind != Value::Kind::Map)
            return {};
        const auto decoded = read_mesh_section(asset.substr(header_size), header.get(sections[index]));
        if (decoded.kind != Value::Kind::Array || decoded.array.empty())
            return {};
        std::vector<AssetRenderFace> faces;
        faces.reserve(decoded.array.size());
        for (const auto& submesh : decoded.array) {
            if (submesh.contains("NoGeometry"))
                continue;
            if (!read_render_face(submesh, faces.emplace_back()))
                return {};
        }
        return faces;
    } catch (const std::runtime_error&) {
        return {};
    } catch (const std::logic_error&) {
        return {};
    }
}

std::optional<AssetRenderFace> build_sculpt_render_face(std::span<const std::uint8_t> pixels,
    std::uint32_t width, std::uint32_t height, std::size_t channels, std::uint8_t sculpt_type, AssetMeshLod lod) {
    constexpr std::array lod_edges{8u, 16u, 32u, 32u};
    const auto lod_index = static_cast<std::size_t>(lod);
    if (width == 0 || height == 0 || (channels != 3 && channels != 4) ||
        pixels.size() % channels != 0 || std::uint64_t(width) * height != pixels.size() / channels ||
        lod_index >= lod_edges.size())
        return {};

    const auto max_samples = lod_edges[lod_index] * lod_edges[lod_index];
    const bool subsample = std::uint64_t(width) * height > max_samples;
    auto resized_width = width;
    auto resized_height = height;
    while (std::uint64_t(resized_width) * resized_height > 4 * max_samples) {
        resized_width /= 2;
        resized_height /= 2;
    }
    if (resized_width == 0 || resized_height == 0)
        return {};
    const auto step_x = width / resized_width;
    const auto step_y = height / resized_height;
    auto columns = resized_width;
    auto rows_count = resized_height;
    if (subsample) {
        // The managed sampler still steps by two after resizing. If the
        // resized image already fits, its sampling walks beyond the bitmap.
        if (std::uint64_t(resized_width) * resized_height <= max_samples)
            return {};
        columns = resized_width / 2 + 1;
        rows_count = resized_height / 2 + 1;
    }
    if (columns < 2 || rows_count < 2)
        return {};

    constexpr std::uint8_t mirror_flag = 0x80;
    constexpr std::uint8_t invert_flag = 0x40;
    const bool mirror = (sculpt_type & mirror_flag) != 0;
    const bool invert = ((sculpt_type & invert_flag) != 0) != mirror;
    constexpr std::uint8_t sphere = 1, torus = 2, plane = 3;
    const auto topology = sculpt_type & 7;
    std::vector<std::vector<Vector3>> rows(rows_count);
    constexpr float color_scale = 1.f / 255.f;
    for (std::uint32_t y = 0; y < rows_count; ++y) {
        const auto source_y = subsample && y > 0 ? 2 * y - (y + 1 == rows_count) : y;
        auto& row = rows[y];
        row.reserve(columns + 1);
        for (std::uint32_t x = 0; x < columns; ++x) {
            const auto source_x = subsample && x > 0 ? 2 * x - (x + 1 == columns) : x;
            const auto offset = (std::size_t(source_y) * step_y * width + source_x * step_x) * channels;
            const float red = pixels[offset] * color_scale - .5f;
            row.push_back({mirror ? -red : red, pixels[offset + 1] * color_scale - .5f,
                           pixels[offset + 2] * color_scale - .5f});
        }
    }

    const bool even_rows = rows.size() % 2 == 0;
    if (topology != plane) {
        for (auto& row : rows) {
            if (even_rows)
                row.push_back(row.front());
            else
                row.front() = row.back();
        }
    }
    if (topology == sphere) {
        const auto top = rows.front()[columns / 2];
        const auto bottom = rows.back()[columns / 2];
        if (even_rows) {
            const auto pole_width = rows.front().size();
            rows.insert(rows.begin(), std::vector<Vector3>(pole_width, top));
            rows.emplace_back(pole_width, bottom);
        } else {
            std::ranges::fill(rows.front(), top);
            std::ranges::fill(rows.back(), bottom);
        }
    } else if (topology == torus) {
        rows.push_back(rows.front());
    }

    const auto across = static_cast<std::uint32_t>(rows.front().size());
    const auto down = static_cast<std::uint32_t>(rows.size());
    const float u_step = 1.f / (across - 1);
    const float v_step = 1.f / (down - 1);
    AssetRenderFace face;
    auto& [vertices, indices] = face.geometry;
    vertices.reserve(across * down);
    face.texcoords.reserve(across * down);
    indices.reserve((across - 1) * (down - 1) * 6);
    for (std::uint32_t y = 0; y < down; ++y) {
        for (std::uint32_t x = 0; x < across; ++x) {
            vertices.push_back(rows[y][x]);
            face.texcoords.push_back({u_step * x, v_step * y});
            if (x == 0 || y == 0)
                continue;
            const auto southeast = y * across + x;
            const auto southwest = southeast - 1;
            const auto northeast = southeast - across;
            const auto northwest = northeast - 1;
            if (invert)
                indices.insert(indices.end(), {northwest, southeast, southwest, northwest, northeast, southeast});
            else
                indices.insert(indices.end(), {northwest, southwest, southeast, northwest, southeast, northeast});
        }
    }

    const auto normalize = [](Vector3 value) {
        const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        return length > 0.f ? Vector3{value.x / length, value.y / length, value.z / length} : Vector3{};
    };
    face.normals.resize(vertices.size());
    for (std::size_t index = 0; index < indices.size(); index += 3) {
        const auto a = indices[index], b = indices[index + 1], c = indices[index + 2];
        const auto origin = vertices[a];
        const Vector3 u{vertices[b].x - origin.x, vertices[b].y - origin.y, vertices[b].z - origin.z};
        const Vector3 v{vertices[c].x - origin.x, vertices[c].y - origin.y, vertices[c].z - origin.z};
        const auto normal = normalize({u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x});
        for (const auto vertex : {a, b, c}) {
            auto& sum = face.normals[vertex];
            sum = {sum.x + normal.x, sum.y + normal.y, sum.z + normal.z};
        }
    }
    for (auto& normal : face.normals)
        normal = normalize(normal);
    if (topology != plane) {
        for (std::uint32_t y = 0; y < down; ++y) {
            auto& first = face.normals[y * across];
            auto& last = face.normals[(y + 1) * across - 1];
            first = last = normalize({first.x + last.x, first.y + last.y, first.z + last.z});
        }
    }
    return face;
}
}
