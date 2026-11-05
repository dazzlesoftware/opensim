// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_value.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <exception>
#include <sstream>
#include <iomanip>
namespace opensim::framework {
namespace {
using Bytes = PrimitiveBaseShapeCore::Bytes;
int length(const Bytes& b) {
    if (!b)
        throw PrimitiveShapeError("System.NullReferenceException",
                                  "Object reference not set to an instance of an object.");
    if (b->size() > std::size_t(INT32_MAX))
        throw std::length_error("Shape array exceeds managed array limit");
    return static_cast<int>(b->size());
}
std::uint8_t at(const Bytes& b, int pos) {
    if (pos < 0 || pos >= length(b))
        throw PrimitiveShapeError("System.IndexOutOfRangeException",
                                  "Index was outside the bounds of the array.");
    return (*b)[static_cast<std::size_t>(pos)];
}
// The SDK uses unchecked native reads for UUID/float arrays. Deterministic valid
// ranges are ported; out-of-buffer native-memory observations are not reproduced.
void native_range(const Bytes& b, int pos, int size) {
    if (pos < 0 || size < 0 || std::int64_t(pos) + size > length(b))
        throw PrimitiveShapeError("NativeShapeUnsafeRead",
                                  "SDK native read extends beyond supplied shape buffer");
}
std::uint32_t uint_at(const Bytes& b, int p) {
    native_range(b, p, 4);
    return std::uint32_t(at(b, p)) | (std::uint32_t(at(b, p + 1)) << 8) |
           (std::uint32_t(at(b, p + 2)) << 16) | (std::uint32_t(at(b, p + 3)) << 24);
}
float float_at(const Bytes& b, int p) {
    return std::bit_cast<float>(uint_at(b, p));
}
Uuid uuid_at(const Bytes& b, int p) {
    native_range(b, p, 16);
    return Uuid::from_bytes(std::span<const std::uint8_t>(*b).subspan(p, 16));
}
int sub_length(const Bytes& b, int pos) {
    return std::bit_cast<std::int32_t>(std::uint32_t(length(b)) - std::uint32_t(pos));
}
int add(int x, int y) {
    return std::bit_cast<std::int32_t>(std::uint32_t(x) + std::uint32_t(y));
}
float clamp(float v, float high) {
    return v < 0 ? 0 : (v > high ? high : v);
}
std::uint8_t cast_byte(float v) {
    if (!std::isfinite(v) || v >= 2147483648.f || v < -2147483648.f)
        return 0;
    return static_cast<std::uint8_t>(static_cast<std::int32_t>(v));
}
std::uint8_t color_byte(float v) {
    return v <= 0 ? 0 : (v >= 1 ? 255 : cast_byte(255.f * v));
}
void push_uint(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int n = 0; n < 4; ++n)
        b.push_back(static_cast<std::uint8_t>(v >> (n * 8)));
}
void push_float(std::vector<std::uint8_t>& b, float v) {
    push_uint(b, std::bit_cast<std::uint32_t>(v));
}
void push_uuid(std::vector<std::uint8_t>& b, const Uuid& u) {
    b.insert(b.end(), u.bytes().begin(), u.bytes().end());
}
}
PrimitiveBaseShapeCore::PrimitiveBaseShapeCore(Log log) : log_(std::make_shared<Log>(std::move(log))) {
    if (!*log_)
        throw std::invalid_argument("PrimitiveBaseShapeCore requires shape log provider");
    static const auto defaults =
        std::make_shared<ShapeByteArray>(TextureEntry(Uuid("89556747-24cb-43ed-920b-47caed15465f")).bytes());
    texture_ = defaults;
    static const auto empty = std::make_shared<ShapeByteArray>();
    sculpt_data = empty;
}
PrimitiveBaseShapeCore PrimitiveBaseShapeCore::create_box(Log log) {
    PrimitiveBaseShapeCore s(std::move(log));
    s.path_curve = 16;
    s.profile_shape = 1;
    s.path_scale_x = s.path_scale_y = 100;
    return s;
}
PrimitiveBaseShapeCore PrimitiveBaseShapeCore::create_sphere(Log log) {
    auto s = create_box(std::move(log));
    s.path_curve = 32;
    s.profile_shape = 5;
    return s;
}
PrimitiveBaseShapeCore PrimitiveBaseShapeCore::create_cylinder(Log log) {
    auto s = create_box(std::move(log));
    s.path_curve = 32;
    return s;
}
PrimitiveBaseShapeCore PrimitiveBaseShapeCore::default_shape(Log log) {
    auto s = create_box(std::move(log));
    s.set_scale(.5f);
    return s;
}
PrimitiveBaseShapeCore PrimitiveBaseShapeCore::create_cylinder(float radius, float height, Log log) {
    auto s = create_cylinder(std::move(log));
    s.set_heigth(height);
    s.set_radius(radius);
    return s;
}
PrimitiveBaseShapeCore PrimitiveBaseShapeCore::create_mesh(int faces, Uuid mesh, Log log) {
    PrimitiveBaseShapeCore s(std::move(log));
    s.path_scale_x = s.path_scale_y = 100;
    if (faces <= 0)
        faces = 1;
    switch (faces) {
    case 1:
    case 2:
        s.profile_curve(48);
        s.path_curve = 32;
        s.path_scale_y = 150;
        if (faces == 2)
            s.profile_hollow = 27500;
        break;
    case 3:
    case 4:
        s.profile_curve(48);
        s.path_curve = 16;
        if (faces == 4)
            s.profile_hollow = 27500;
        break;
    case 5:
        s.profile_curve(51);
        s.path_curve = 16;
        break;
    case 6:
    case 7:
        s.profile_curve(49);
        s.path_curve = 16;
        if (faces == 7)
            s.profile_hollow = 27500;
        break;
    default:
        s.profile_curve(49);
        s.path_curve = 16;
        s.profile_begin = 9375;
        break;
    }
    s.sculpt_entry = true;
    s.sculpt_type = 5;
    s.sculpt_texture = mesh;
    return s;
}
void PrimitiveBaseShapeCore::profile_curve(std::uint8_t v) {
    auto h = static_cast<std::uint8_t>(v & 0xf0);
    if (h != 0 && h != 16 && h != 32 && h != 48) {
        (*log_)("[SHAPE]: Attempt to set a ProfileCurve with a hollow shape value of " + std::to_string(h) +
                ", which isn't a valid enum.  Replacing with default shape.");
        hollow_shape = 0;
    } else
        hollow_shape = h;
    auto p = static_cast<std::uint8_t>(v & 15);
    if (p > 5) {
        (*log_)("[SHAPE]: Attempt to set a ProfileCurve with a profile shape value of " + std::to_string(p) +
                ", which isn't a valid enum.  Replacing with square.");
        profile_shape = 1;
    } else
        profile_shape = p;
}
float PrimitiveBaseShapeCore::light_color(unsigned channel) const {
    switch (channel) {
    case 0:
        return light_color_r;
    case 1:
        return light_color_g;
    case 2:
        return light_color_b;
    case 3:
        return light_color_a;
    default:
        throw std::out_of_range("Light color channel");
    }
}
void PrimitiveBaseShapeCore::light_color(unsigned channel, float value) {
    value = clamp(value, 1);
    switch (channel) {
    case 0:
        light_color_r = value;
        break;
    case 1:
        light_color_g = value;
        break;
    case 2:
        light_color_b = value;
        break;
    case 3:
        light_color_a = value;
        break;
    default:
        throw std::out_of_range("Light color channel");
    }
}
bool PrimitiveBaseShapeCore::animesh_enabled() const noexcept {
    return mesh_flags_entry_ && (mesh_flags_ & 1) != 0 && (sculpt_type & 7) == 5;
}
void PrimitiveBaseShapeCore::animesh_enabled(bool v) {
    if ((sculpt_type & 7) != 5) {
        mesh_flags_entry_ = false;
        mesh_flags_ = 0;
        return;
    }
    if (v) {
        mesh_flags_entry_ = true;
        mesh_flags_ |= 1;
    } else
        mesh_flags_ &= 0xfe;
}
void PrimitiveBaseShapeCore::texture_entry(Bytes v) {
    texture_ = v ? std::move(v) : std::make_shared<ShapeByteArray>(1);
}
TextureEntry PrimitiveBaseShapeCore::textures() const {
    try {
        if (!texture_)
            throw std::runtime_error("null texture");
        return TextureEntry(std::span<const std::uint8_t>(*texture_));
    } catch (...) {
        (*log_)("[SHAPE]: Failed to decode texture, length=" +
                std::to_string(texture_ ? texture_->size() : 0));
        return TextureEntry(Uuid{});
    }
}
void PrimitiveBaseShapeCore::textures(const std::shared_ptr<TextureEntry>& t) {
    if (!t)
        throw PrimitiveShapeError("System.NullReferenceException",
                                  "Object reference not set to an instance of an object.");
    textures(*t);
}
void PrimitiveBaseShapeCore::textures(const TextureEntry& t) {
    texture_ = std::make_shared<ShapeByteArray>(t.bytes());
}
PrimGeometry PrimitiveBaseShapeCore::geometry() const noexcept {
    PrimGeometry s;
    s.sculpt_entry = sculpt_entry;
    s.sculpt_type = sculpt_type;
    s.profile_curve = profile_curve();
    s.path_curve = path_curve;
    s.profile_begin = profile_begin;
    s.profile_end = profile_end;
    s.profile_hollow = profile_hollow;
    s.path_begin = path_begin;
    s.path_end = path_end;
    s.path_skew = path_skew;
    s.path_twist_begin = path_twist_begin;
    s.path_twist = path_twist;
    s.path_taper_x = path_taper_x;
    s.path_taper_y = path_taper_y;
    s.path_radius_offset = path_radius_offset;
    s.path_revolutions = path_revolutions;
    return s;
}
void PrimitiveBaseShapeCore::read_sculpt(const Bytes& b, int p) {
    if (sub_length(b, p) >= 17) {
        sculpt_texture = uuid_at(b, p);
        sculpt_type = at(b, add(p, 16));
        sculpt_entry = true;
    } else {
        sculpt_entry = false;
        sculpt_texture = {};
        sculpt_type = 0;
    }
}
void PrimitiveBaseShapeCore::read_flexi(const Bytes& b, int p) {
    if (sub_length(b, p) >= 16) {
        flexi_entry = true;
        flexi_softness = ((at(b, p) & 128) >> 6) | ((at(b, add(p, 1)) & 128) >> 7);
        flexi_tension = (at(b, p++) & 127) / 10.f;
        flexi_drag = (at(b, p++) & 127) / 10.f;
        flexi_gravity = at(b, p++) / 10.f - 10.f;
        flexi_wind = at(b, p++) / 10.f;
        flexi_force_x = float_at(b, p);
        flexi_force_y = float_at(b, add(p, 4));
        flexi_force_z = float_at(b, add(p, 8));
    } else {
        flexi_entry = false;
        flexi_softness = 0;
        flexi_tension = flexi_drag = flexi_gravity = flexi_wind = flexi_force_x = flexi_force_y =
            flexi_force_z = 0;
    }
}
void PrimitiveBaseShapeCore::read_light(const Bytes& b, int p) {
    if (sub_length(b, p) >= 16) {
        light_entry = true;
        light_intensity = at(b, add(p, 3)) * (1.f / 255.f);
        light_color_a = 1;
        light_color_r = at(b, p) * (1.f / 255.f);
        light_color_g = at(b, add(p, 1)) * (1.f / 255.f);
        light_color_b = at(b, add(p, 2)) * (1.f / 255.f);
        light_radius = float_at(b, add(p, 4));
        light_cutoff = float_at(b, add(p, 8));
        light_falloff = float_at(b, add(p, 12));
    } else {
        light_entry = false;
        light_color_a = 1;
        light_color_r = light_color_g = light_color_b = light_radius = light_cutoff = light_falloff =
            light_intensity = 0;
    }
}
void PrimitiveBaseShapeCore::read_projection(const Bytes& b, int p) {
    if (sub_length(b, p) >= 28) {
        projection_entry = true;
        projection_texture = uuid_at(b, p);
        projection_fov = float_at(b, add(p, 16));
        projection_focus = float_at(b, add(p, 20));
        projection_ambiance = float_at(b, add(p, 24));
    } else {
        projection_entry = false;
        projection_texture = {};
        projection_fov = projection_focus = projection_ambiance = 0;
    }
}
void PrimitiveBaseShapeCore::read_mesh_flags(const Bytes& b, int p) {
    mesh_flags_entry_ = true;
    mesh_flags_ = sub_length(b, p) >= 4 ? uint_at(b, p) : 0;
}
void PrimitiveBaseShapeCore::read_reflection_probe(const Bytes& b, int p) {
    if (sub_length(b, p) >= 9) {
        auto probe = std::make_shared<ShapeReflectionProbe>();
        const auto sdk_clamp = [](float value, float high) {
            if (!(value < high))
                return high;
            if (!(value > 0))
                return 0.f;
            return value;
        };
        probe->ambiance = sdk_clamp(float_at(b, p), 1);
        probe->clip_distance = sdk_clamp(float_at(b, add(p, 4)), 1024);
        probe->flags = at(b, add(p, 8));
        reflection_probe = std::move(probe);
    }
}
int PrimitiveBaseShapeCore::read_render_materials(const Bytes& b, int p, int size) {
    if (size > 17) {
        int count = at(b, p);
        p = add(p, 1);
        if (size >= 1 + 17 * count) {
            auto entries = std::make_shared<ShapeArray<ShapeMaterialEntry>>(count);
            for (auto& e : *entries) {
                e.te_index = at(b, p);
                p = add(p, 1);
                e.id = uuid_at(b, p);
                p = add(p, 16);
            }
            if (!render_materials)
                render_materials = std::make_shared<ShapeRenderMaterials>();
            render_materials->entries = std::move(entries);
        }
    }
    return add(size, 4);
}
void PrimitiveBaseShapeCore::update_extra_param(std::uint16_t type, bool use, const Bytes& b) {
    switch (type) {
    case 16:
        if (!use) {
            flexi_entry = false;
            return;
        }
        read_flexi(b, 0);
        break;
    case 32:
        if (!use) {
            light_entry = false;
            return;
        }
        read_light(b, 0);
        break;
    case 48:
    case 96:
        if (!use) {
            sculpt_entry = false;
            return;
        }
        read_sculpt(b, 0);
        break;
    case 64:
        if (!use) {
            projection_entry = false;
            return;
        }
        read_projection(b, 0);
        break;
    case 112:
        if (!use) {
            mesh_flags_entry_ = false;
            mesh_flags_ = 0;
            return;
        }
        read_mesh_flags(b, 0);
        break;
    case 144:
        if (!use) {
            reflection_probe.reset();
            return;
        }
        read_reflection_probe(b, 0);
        break;
    case 128:
        if (!use) {
            render_materials.reset();
            return;
        }
        read_render_materials(b, 0, length(b));
        break;
    }
}
void PrimitiveBaseShapeCore::read_extra_params(const Bytes& b) {
    if (!b)
        return;
    flexi_entry = light_entry = sculpt_entry = projection_entry = mesh_flags_entry_ = false;
    render_materials.reset();
    reflection_probe.reset();
    if (length(b) == 1)
        return;
    int count = at(b, 0), p = 1;
    for (int n = 0; n < count; ++n) {
        switch (at(b, p)) {
        case 16:
            p = add(p, 6);
            read_flexi(b, p);
            p = add(p, 16);
            break;
        case 32:
            p = add(p, 6);
            read_light(b, p);
            p = add(p, 16);
            break;
        case 48:
        case 96:
            p = add(p, 6);
            read_sculpt(b, p);
            p = add(p, 17);
            break;
        case 64:
            p = add(p, 6);
            read_projection(b, p);
            p = add(p, 28);
            break;
        case 112:
            p = add(p, 6);
            read_mesh_flags(b, p);
            p = add(p, 4);
            break;
        case 144:
            p = add(p, 6);
            read_reflection_probe(b, p);
            p = add(p, 9);
            break;
        case 128:
            p = add(p, 2);
            if (sub_length(b, p) >= 4) {
                int size = std::bit_cast<std::int32_t>(uint_at(b, p));
                p = add(p, 4);
                p = add(p, read_render_materials(b, p, size));
            }
            break;
        }
    }
}
std::vector<std::uint8_t> PrimitiveBaseShapeCore::extra_params() const {
    std::vector<std::uint8_t> out(1);
    const auto header = [&](std::uint8_t id, std::uint32_t size) {
        ++out[0];
        out.push_back(id);
        out.push_back(0);
        push_uint(out, size);
    };
    if (flexi_entry) {
        header(16, 16);
        out.push_back(((flexi_softness & 2) << 6) | (cast_byte(flexi_tension * 10.01f) & 127));
        out.push_back(((flexi_softness & 1) << 7) | (cast_byte(flexi_drag * 10.01f) & 127));
        out.push_back(cast_byte((flexi_gravity + 10.f) * 10.01f));
        out.push_back(cast_byte(flexi_wind * 10.01f));
        push_float(out, flexi_force_x);
        push_float(out, flexi_force_y);
        push_float(out, flexi_force_z);
    }
    if (light_entry) {
        header(32, 16);
        out.push_back(color_byte(light_color_r));
        out.push_back(color_byte(light_color_g));
        out.push_back(color_byte(light_color_b));
        out.push_back(color_byte(light_intensity));
        push_float(out, light_radius);
        push_float(out, light_cutoff);
        push_float(out, light_falloff);
    }
    if (sculpt_entry) {
        header(48, 17);
        push_uuid(out, sculpt_texture);
        out.push_back(sculpt_type);
    }
    if (projection_entry) {
        header(64, 28);
        push_uuid(out, projection_texture);
        push_float(out, projection_fov);
        push_float(out, projection_focus);
        push_float(out, projection_ambiance);
    }
    if (mesh_flags_entry_) {
        header(112, 4);
        push_uint(out, mesh_flags_);
    }
    if (reflection_probe) {
        header(144, 9);
        push_float(out, reflection_probe->ambiance);
        push_float(out, reflection_probe->clip_distance);
        out.push_back(reflection_probe->flags);
    }
    if (render_materials && render_materials->entries && !render_materials->entries->empty()) {
        header(128, static_cast<std::uint32_t>(1 + 17 * render_materials->entries->size()));
        out.push_back(static_cast<std::uint8_t>(render_materials->entries->size()));
        for (const auto& e : *render_materials->entries) {
            out.push_back(e.te_index);
            push_uuid(out, e.id);
        }
    }
    return out;
}
std::uint64_t PrimitiveBaseShapeCore::mesh_key(Vector3 size, float lod, bool convex) const noexcept {
    std::uint64_t hash = 5381;
    const auto append = [&](std::uint64_t v) { hash = 33 * hash + v; };
    append(path_curve);
    append(profile_curve());
    append(path_begin);
    append(path_end);
    append(path_scale_x);
    append(path_scale_y);
    append(path_shear_x);
    append(path_shear_y);
    append(static_cast<std::uint8_t>(path_twist));
    append(static_cast<std::uint8_t>(path_twist_begin));
    append(static_cast<std::uint8_t>(path_radius_offset));
    append(static_cast<std::uint8_t>(path_taper_x));
    append(static_cast<std::uint8_t>(path_taper_y));
    append(path_revolutions);
    append(static_cast<std::uint8_t>(path_skew));
    append(profile_begin);
    append(profile_end);
    append(profile_hollow);
    for (float value : {size.x, size.y, size.z, lod}) {
        auto bits = std::bit_cast<std::uint32_t>(value);
        if (((bits - 1) & 0x7fffffff) >= 0x7f800000)
            bits &= 0x7f800000;
        append(static_cast<std::uint64_t>(static_cast<std::int64_t>(std::bit_cast<std::int32_t>(bits))));
    }
    if (sculpt_entry)
        for (auto value : sculpt_texture.bytes())
            append(value);
    if (convex)
        append(0xa5);
    return hash;
}

namespace {
float shape_round(float v) {
    if (!std::isfinite(v) || std::fabs(v) >= 8388608.f)
        return v;
    const auto low = std::floor(v), fraction = v - low;
    return fraction < .5f
               ? low
               : (fraction > .5f ? low + 1.f : (static_cast<std::int32_t>(low) % 2 == 0 ? low : low + 1.f));
}
std::int32_t unchecked_integer(float v) {
    return !std::isfinite(v) || v >= 2147483648.f || v < -2147483648.f ? INT32_MIN
                                                                       : static_cast<std::int32_t>(v);
}
std::uint16_t pack_begin(float v) {
    return static_cast<std::uint16_t>(unchecked_integer(shape_round(v * 50000.f)));
}
std::uint16_t pack_end(float v) {
    return static_cast<std::uint16_t>(50000 - pack_begin(v));
}
std::uint8_t pack_scale(float v) {
    return static_cast<std::uint8_t>(200 -
                                     static_cast<std::uint8_t>(unchecked_integer(shape_round(v * 100.f))));
}
std::int8_t pack_signed(float v) {
    return static_cast<std::int8_t>(unchecked_integer(shape_round(v * 100.f)));
}
// SDK osUTF8.Append drops NULs and unpaired surrogates; a high surrogate
// consumes the following code unit even when that code unit is not low.
void append_osutf8(std::vector<std::uint8_t>& bytes, std::u16string_view value) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto c = value[i];
        if (c == 0)
            continue;
        if (c >= 0xdc00 && c <= 0xdfff)
            continue;
        std::u16string chars(1, c);
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == value.size())
                break;
            auto low = value[i];
            if (low < 0xdc00 || low > 0xdfff)
                continue;
            chars += low;
        }
        auto encoded = utf8(chars);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }
}
}
void PrimitiveBaseShapeCore::set_path_range(float begin, float end) {
    path_begin = pack_begin(begin);
    path_end = pack_end(end);
}
void PrimitiveBaseShapeCore::set_profile_range(float begin, float end) {
    profile_begin = pack_begin(begin);
    profile_end = pack_end(end);
}
PrimitiveBaseShapeCore::PrimitiveBaseShapeCore(const std::shared_ptr<ShapeOmvPrimitive>& prim, Log log)
    : PrimitiveBaseShapeCore(std::move(log)) {
    if (!prim)
        throw PrimitiveShapeError("System.NullReferenceException",
                                  "Object reference not set to an instance of an object.");
    const auto& p = prim->prim_data;
    pcode = p.pcode;
    state = p.state;
    last_attach_point = p.state;
    path_begin = pack_begin(p.path_begin);
    path_end = pack_end(p.path_end);
    path_scale_x = pack_scale(p.path_scale_x);
    path_scale_y = pack_scale(p.path_scale_y);
    path_shear_x = static_cast<std::uint8_t>(pack_signed(p.path_shear_x));
    path_shear_y = static_cast<std::uint8_t>(pack_signed(p.path_shear_y));
    path_skew = pack_signed(p.path_skew);
    profile_begin = pack_begin(p.profile_begin);
    profile_end = pack_end(p.profile_end);
    scale = prim->scale;
    path_curve = p.path_curve;
    profile_curve(p.profile_curve & 15);
    profile_hollow = pack_begin(p.profile_hollow);
    path_radius_offset = pack_signed(p.path_radius_offset);
    path_revolutions =
        static_cast<std::uint8_t>(unchecked_integer(shape_round((p.path_revolutions - 1.f) * 66.66667f)));
    path_taper_x = pack_signed(p.path_taper_x);
    path_taper_y = pack_signed(p.path_taper_y);
    path_twist = pack_signed(p.path_twist);
    path_twist_begin = pack_signed(p.path_twist_begin);
    textures(prim->textures);
    if (prim->sculpt) {
        sculpt_entry = prim->sculpt->type() != 0;
        std::vector<std::uint8_t> b;
        push_uuid(b, prim->sculpt->texture);
        b.push_back(prim->sculpt->raw_type);
        sculpt_data = std::make_shared<ShapeByteArray>(std::move(b));
        sculpt_texture = prim->sculpt->texture;
        sculpt_type = prim->sculpt->type();
    } else
        sculpt_type = 0;
}
ShapeOmvPrimitive PrimitiveBaseShapeCore::to_omv_primitive() const {
    return to_omv_primitive({0, 0, 0}, {0, 0, 0, 1});
}
ShapeOmvPrimitive PrimitiveBaseShapeCore::to_omv_primitive(Vector3 position, Quaternion rotation) const {
    ShapeOmvPrimitive prim;
    prim.scale = scale;
    prim.position = position;
    prim.rotation = rotation;
    if (sculpt_entry) {
        prim.sculpt = std::make_shared<ShapeOmvSculpt>();
        prim.sculpt->raw_type = sculpt_type;
        prim.sculpt->texture = sculpt_texture;
    }
    auto& p = prim.prim_data;
    p.path_shear_x = path_shear_x < 128 ? float(path_shear_x) * .01f : float(int(path_shear_x) - 256) * .01f;
    p.path_shear_y = path_shear_y < 128 ? float(path_shear_y) * .01f : float(int(path_shear_y) - 256) * .01f;
    p.path_begin = float(path_begin) * 2.e-5f;
    p.path_end = 1.f - float(path_end) * 2.e-5f;
    p.path_scale_x = (200 - int(path_scale_x)) * .01f;
    p.path_scale_y = (200 - int(path_scale_y)) * .01f;
    p.path_taper_x = path_taper_x * .01f;
    p.path_taper_y = path_taper_y * .01f;
    p.path_twist_begin = path_twist_begin * .01f;
    p.path_twist = path_twist * .01f;
    p.profile_begin = float(profile_begin) * 2.e-5f;
    p.profile_end = 1.f - float(profile_end) * 2.e-5f;
    p.profile_hollow = float(profile_hollow) * 2.e-5f;
    p.profile_curve = static_cast<std::uint8_t>((profile_curve() & 15) | hollow_shape);
    p.path_curve = path_curve;
    p.path_radius_offset = .01f * path_radius_offset;
    p.path_revolutions = 1.f + .015f * path_revolutions;
    p.path_skew = .01f * path_skew;
    p.pcode = 9;
    p.state = 0;
    if (flexi_entry) {
        prim.flexible = std::make_shared<ShapeOmvFlexible>();
        auto& f = *prim.flexible;
        f.drag = flexi_drag;
        f.force = {flexi_force_x, flexi_force_y, flexi_force_z};
        f.gravity = flexi_gravity;
        f.softness = flexi_softness;
        f.tension = flexi_tension;
        f.wind = flexi_wind;
    }
    if (light_entry) {
        prim.light = std::make_shared<ShapeOmvLight>();
        auto& l = *prim.light;
        const auto sdk_color = [](float v) {
            if (!(v < 1))
                return 1.f;
            if (!(v > 0))
                return 0.f;
            return v;
        };
        l.color = {sdk_color(light_color_r), sdk_color(light_color_g), sdk_color(light_color_b),
                   sdk_color(light_color_a)};
        l.cutoff = light_cutoff;
        l.falloff = light_falloff;
        l.intensity = light_intensity;
        l.radius = light_radius;
    }
    prim.textures = std::make_shared<TextureEntry>(textures());
    prim.properties = std::make_shared<ShapeOmvProperties>();
    prim.properties->name = u"Object";
    prim.properties->description = u"";
    prim.properties->sale_price = 10;
    return prim;
}
PrimitiveBaseShapeCore::Bytes PrimitiveBaseShapeCore::render_overrides_to_raw() const {
    if (!render_materials)
        return {};
    if (!render_materials->overrides || render_materials->overrides->empty())
        return std::make_shared<ShapeByteArray>(1);
    int count = 0;
    for (const auto& entry : *render_materials->overrides)
        if (entry.data && !entry.data->empty())
            ++count;
    if (!count)
        return std::make_shared<ShapeByteArray>(1);
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(count)};
    for (const auto& entry : *render_materials->overrides) {
        if (!entry.data || entry.data->empty())
            continue;
        bytes.push_back(entry.te_index);
        const auto len = entry.data->size();
        bytes.push_back(static_cast<std::uint8_t>(len));
        bytes.push_back(static_cast<std::uint8_t>(len >> 8));
        append_osutf8(bytes, *entry.data);
    }
    return std::make_shared<ShapeByteArray>(std::move(bytes));
}
void PrimitiveBaseShapeCore::render_overrides_from_raw(const Bytes& bytes, const StartsWith& starts_with) {
    if (!starts_with)
        throw std::invalid_argument("PBR override decoding requires CurrentCulture StartsWith provider");
    if (render_materials && render_materials->overrides)
        render_materials->overrides.reset();
    if (!bytes || bytes->empty())
        return;
    const int count = at(bytes, 0);
    if (count > 128)
        return;
    if (!count) {
        if (!render_materials)
            render_materials = std::make_shared<ShapeRenderMaterials>();
        return;
    }
    int index = 1;
    auto overrides = std::make_shared<ShapeArray<ShapeMaterialOverride>>(count);
    try {
        for (auto& entry : *overrides) {
            entry.te_index = at(bytes, index++);
            int len = at(bytes, index++);
            len += int(at(bytes, index++)) << 8;
            std::u16string value;
            if (length(bytes) >= add(index, len)) {
                int last = at(bytes, add(index, len - 1));
                int decoded = len - (last == 0 ? 1 : 0);
                native_range(bytes, index, decoded);
                value = utf16_replacing(std::string_view(
                    reinterpret_cast<const char*>(std::span<const std::uint8_t>(*bytes).data() + index),
                    decoded));
            }
            entry.data = std::move(value);
            if (starts_with(*entry.data, u"{\"asset"))
                return;
            index = add(index, len);
        }
    } catch (...) {
        return;
    }
    if (!render_materials)
        render_materials = std::make_shared<ShapeRenderMaterials>();
    render_materials->overrides = std::move(overrides);
}

namespace {
void validate_xml_raw(std::u16string_view text) {
    const auto hex = [](char16_t value) {
        std::ostringstream out;
        out << std::uppercase << std::hex << static_cast<unsigned>(value);
        return out.str();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto high = text[i];
        if (high >= 0xdc00 && high <= 0xdfff)
            throw PrimitiveShapeError(
                "System.ArgumentException",
                "Invalid high surrogate character (0x" + hex(high) +
                    "). A high surrogate character must have a value from range (0xD800 - 0xDBFF).");
        if (high >= 0xd800 && high <= 0xdbff) {
            auto low = i + 1 < text.size() ? text[++i] : char16_t{};
            if (low < 0xdc00 || low > 0xdfff)
                throw PrimitiveShapeError(
                    "System.ArgumentException",
                    "The surrogate pair (0x" + hex(high) + ", 0x" + hex(low) +
                        ") is invalid. A high surrogate character (0xD800 - 0xDBFF) must always be paired with a low surrogate character (0xDC00 - 0xDFFF).");
        }
    }
}
void xml_escape(std::u16string& out, std::u16string_view text) {
    for (auto c : text)
        switch (c) {
        case u'<':
            out += u"&lt;";
            break;
        case u'>':
            out += u"&gt;";
            break;
        case u'&':
            out += u"&amp;";
            break;
        case u'"':
            out += u"&quot;";
            break;
        case u'\'':
            out += u"&apos;";
            break;
        default:
            out += c;
        }
}
void media_osd_xml(std::u16string& out, const ShapeMediaEntry& entry, const ShapeMediaIntegerFormat& format) {
    out += u"<map>";
    const auto key = [&](std::u16string_view value) {
        out += u"<key>";
        out += value;
        out += u"</key>";
    };
    const auto boolean = [&](std::u16string_view name, bool value) {
        key(name);
        out += value ? u"<boolean>1</boolean>" : u"<boolean>0</boolean>";
    };
    const auto integer = [&](std::u16string_view name, std::int32_t value) {
        key(name);
        out += u"<integer>";
        out += format(value);
        out += u"</integer>";
    };
    const auto string = [&](std::u16string_view name, const std::optional<std::u16string>& value) {
        key(name);
        out += u"<string>";
        if (value)
            xml_escape(out, *value);
        out += u"</string>";
    };
    boolean(u"alt_image_enable", entry.enable_alternative_image);
    boolean(u"auto_loop", entry.auto_loop);
    boolean(u"auto_play", entry.auto_play);
    boolean(u"auto_scale", entry.auto_scale);
    boolean(u"auto_zoom", entry.auto_zoom);
    integer(u"controls", entry.controls);
    string(u"current_url", entry.current_url);
    boolean(u"first_click_interact", entry.interact_on_first_click);
    integer(u"height_pixels", entry.height);
    string(u"home_url", entry.home_url);
    integer(u"perms_control", entry.control_permissions);
    integer(u"perms_interact", entry.interact_permissions);
    key(u"whitelist");
    out += u"<array>";
    if (entry.whitelist)
        for (const auto& value : *entry.whitelist) {
            out += u"<string>";
            if (value)
                xml_escape(out, *value);
            out += u"</string>";
        }
    out += u"</array>";
    boolean(u"whitelist_enable", entry.enable_whitelist);
    integer(u"width_pixels", entry.width);
    out += u"</map>";
}
std::shared_ptr<ShapeMediaEntry> media_from_osd(const network::Value& osd) {
    const auto& map = osd.view();
    if (map.kind != network::Value::Kind::Map)
        throw PrimitiveShapeError("System.InvalidCastException", "MediaEntry requires OSDMap");
    auto entry = std::make_shared<ShapeMediaEntry>();
    using namespace network;
    entry->enable_alternative_image = osd_boolean(map.get("alt_image_enable"));
    entry->auto_loop = osd_boolean(map.get("auto_loop"));
    entry->auto_play = osd_boolean(map.get("auto_play"));
    entry->auto_scale = osd_boolean(map.get("auto_scale"));
    entry->auto_zoom = osd_boolean(map.get("auto_zoom"));
    entry->controls = osd_integer(map.get("controls"));
    entry->current_url = utf16_replacing(osd_string(map.get("current_url")));
    entry->interact_on_first_click = osd_boolean(map.get("first_click_interact"));
    entry->height = osd_integer(map.get("height_pixels"));
    entry->home_url = utf16_replacing(osd_string(map.get("home_url")));
    entry->control_permissions = static_cast<std::uint8_t>(osd_integer(map.get("perms_control")));
    entry->interact_permissions = static_cast<std::uint8_t>(osd_integer(map.get("perms_interact")));
    const auto& whitelist = map.get("whitelist").view();
    if (whitelist.kind == Value::Kind::Array && !whitelist.array.empty()) {
        entry->whitelist =
            std::make_shared<ShapeArray<std::optional<std::u16string>>>(whitelist.array.size());
        for (std::size_t i = 0; i < whitelist.array.size(); ++i)
            (*entry->whitelist)[i] = utf16_replacing(osd_string(whitelist.array[i]));
    }
    entry->enable_whitelist = osd_boolean(map.get("whitelist_enable"));
    entry->width = osd_integer(map.get("width_pixels"));
    return entry;
}
void require_media_xml(const ShapeMediaXmlBackend& backend) {
    if (!backend.create_reader || !backend.deserialize_llsd || !backend.media_from_osd)
        throw std::invalid_argument(
            "MediaList XML reading requires System.Xml reader, SDK LLSD parser and CurrentCulture MediaEntry coercion providers");
}
}
ShapeMediaList::ShapeMediaList(Log log) : log_(std::move(log)) {
    if (!log_)
        throw std::invalid_argument("MediaList requires shape logger");
}
ShapeMediaList::ShapeMediaList(int capacity, Log log) : ShapeMediaList(std::move(log)) {
    if (capacity < 0)
        throw PrimitiveShapeError("System.ArgumentOutOfRangeException",
                                  "Non-negative number required. (Parameter 'capacity')", "capacity");
    entries.reserve(capacity);
}
ShapeMediaList::ShapeMediaList(const std::optional<std::vector<Entry>>& collection, Log log)
    : ShapeMediaList(std::move(log)) {
    if (!collection)
        throw PrimitiveShapeError("System.ArgumentNullException",
                                  "Value cannot be null. (Parameter 'collection')", "collection");
    entries = *collection;
}
std::u16string ShapeMediaList::to_xml(const ShapeMediaIntegerFormat& format) const {
    if (!format)
        throw std::invalid_argument("MediaList XML requires CurrentCulture integer formatter");
    std::lock_guard guard(mutex);
    std::vector<std::optional<ShapeMediaEntry>> snapshot;
    snapshot.reserve(entries.size());
    for (const auto& entry : entries) {
        if (!entry) {
            snapshot.emplace_back();
            continue;
        }
        snapshot.emplace_back(*entry);
        if (entry->whitelist)
            snapshot.back()->whitelist =
                std::make_shared<ShapeArray<std::optional<std::u16string>>>(*entry->whitelist);
    }
    std::u16string out = u"<OSMedia type=\"sl\" version=\"0.1\"><OSData><llsd><array>";
    for (const auto& entry : snapshot) {
        if (entry)
            media_osd_xml(out, *entry, format);
        else
            out += u"<undef />";
    }
    out += u"</array></llsd></OSData></OSMedia>";
    validate_xml_raw(out);
    return out;
}
void ShapeMediaList::write_xml(ShapeMediaXmlWriter& writer, const ShapeMediaIntegerFormat& format) const {
    writer.write_raw(to_xml(format));
}
void ShapeMediaList::write_xml(const std::shared_ptr<ShapeMediaXmlWriter>& writer,
                               const ShapeMediaIntegerFormat& format) const {
    auto xml = to_xml(format);
    if (!writer)
        throw PrimitiveShapeError("System.NullReferenceException",
                                  "Object reference not set to an instance of an object.");
    writer->write_raw(std::move(xml));
}
void ShapeMediaList::read_xml(const std::optional<std::u16string>& raw, const ShapeMediaXmlBackend& backend) {
    require_media_xml(backend);
    try {
        auto reader = backend.create_reader(raw);
        if (!reader)
            throw PrimitiveShapeError("System.NullReferenceException",
                                      "Object reference not set to an instance of an object.");
        try {
            [&] {
                reader->dtd_processing_ignore();
                reader->move_to_content();
                if (reader->get_attribute(u"type") != std::optional<std::u16string>(u"sl"))
                    return;
                reader->read_start_element(u"OSMedia");
                auto osd = backend.deserialize_llsd(reader->read_inner_xml());
                const auto& array = osd.view();
                if (array.kind != network::Value::Kind::Array || array.array.empty())
                    return;
                for (const auto& item : array.array) {
                    auto entry = item.view().kind == network::Value::Kind::Map
                                     ? backend.media_from_osd(item)
                                     : std::make_shared<ShapeMediaEntry>();
                    if (!entry)
                        throw std::invalid_argument("SDK MediaEntry converter returned null");
                    add(std::move(entry));
                }
            }();
        } catch (...) {
            auto original = std::current_exception();
            reader->dispose();
            std::rethrow_exception(original);
        }
        reader->dispose();
    } catch (...) {
        log_("PrimitiveBaseShape] error decoding MOAP xml");
    }
}

void ShapeMediaList::read_xml(ShapeMediaXmlReader& reader, const ShapeMediaXmlBackend& backend) {
    if (reader.is_empty_element())
        return;
    read_xml(reader.read_inner_xml(), backend);
}
void ShapeMediaList::read_xml(const std::shared_ptr<ShapeMediaXmlReader>& reader,
                              const ShapeMediaXmlBackend& backend) {
    if (!reader)
        throw PrimitiveShapeError("System.NullReferenceException",
                                  "Object reference not set to an instance of an object.");
    read_xml(*reader, backend);
}
std::shared_ptr<ShapeMediaList> ShapeMediaList::from_xml(const std::optional<std::u16string>& raw,
                                                         const ShapeMediaXmlBackend& backend, Log log) {
    auto result = std::make_shared<ShapeMediaList>(std::move(log));
    result->read_xml(raw, backend);
    return result->size() == 0 ? nullptr : result;
}

std::u16string format_invariant_shape_media_integer(std::int32_t value) {
    return utf16(std::to_string(value));
}
std::shared_ptr<ShapeMediaEntry> shape_media_entry_from_invariant_osd(const network::Value& value) {
    return media_from_osd(value);
}

}
