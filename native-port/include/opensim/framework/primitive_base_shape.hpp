// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/prim_geometry.hpp"
#include "opensim/framework/texture_entry.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include "opensim/network/rpc.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace opensim::framework {
// Native PrimitiveBaseShape methods. Full SDK Primitive objects are adapted
// through the explicit consumed-property projection below; XML reader/LLSD parser
// and CurrentCulture StartsWith remain required dependencies at their boundaries.
// Byte arrays and SDK reference objects use shared ownership to preserve MemberwiseClone.
enum class ProfileShape : std::uint8_t {
    Circle = 0,
    Square = 1,
    IsometricTriangle = 2,
    EquilateralTriangle = 3,
    RightTriangle = 4,
    HalfCircle = 5
};
enum class HollowShape : std::uint8_t { Same = 0, Circle = 16, Square = 32, Triangle = 48 };
enum class Extrusion : std::uint8_t { Straight = 16, Curve1 = 32, Curve2 = 48, Flexible = 128 };
enum class PCodeEnum : std::uint8_t {
    Primitive = 9,
    Avatar = 47,
    Grass = 95,
    NewTree = 111,
    ParticleSystem = 143,
    Tree = 255
};
class PrimitiveShapeError : public std::runtime_error {
  public:
    PrimitiveShapeError(std::string type, std::string message, std::optional<std::string> param = {})
        : std::runtime_error(std::move(message)), source_type(std::move(type)), parameter(std::move(param)) {
        if (source_type == "System.NullReferenceException" || source_type == "System.ArgumentNullException")
            hresult = 0x80004003u;
        else if (source_type == "System.IndexOutOfRangeException")
            hresult = 0x80131508u;
        else if (source_type == "System.ArgumentException")
            hresult = 0x80070057u;
        else if (source_type == "System.ArgumentOutOfRangeException")
            hresult = 0x80131502u;
        else if (source_type == "System.InvalidOperationException")
            hresult = 0x80131509u;
        else if (source_type == "System.InvalidCastException")
            hresult = 0x80004002u;
    }
    std::string source_type;
    std::optional<std::uint32_t> hresult;
    std::optional<std::string> parameter;
};
// Managed array ownership: nullable shared identity, mutable elements, fixed length.
template <class T> class ShapeArray {
  public:
    explicit ShapeArray(std::size_t size = 0) : data_(size) {}
    explicit ShapeArray(std::vector<T> data) : data_(std::move(data)) {}
    ShapeArray(std::size_t size, const T &value) : data_(size, value) {}
    ShapeArray(const ShapeArray &) = default;
    ShapeArray &operator=(const ShapeArray &) = delete;
    ShapeArray &operator=(ShapeArray &&) = delete;
    std::size_t size() const noexcept { return data_.size(); }
    bool empty() const noexcept { return data_.empty(); }
    T &operator[](std::size_t n) { return data_[n]; }
    const T &operator[](std::size_t n) const { return data_[n]; }
    T &at(std::size_t n) { return data_.at(n); }
    const T &at(std::size_t n) const { return data_.at(n); }
    auto begin() noexcept { return data_.begin(); }
    auto end() noexcept { return data_.end(); }
    auto begin() const noexcept { return data_.begin(); }
    auto end() const noexcept { return data_.end(); }

  private:
    std::vector<T> data_;
};
using ShapeByteArray = ShapeArray<std::uint8_t>;
struct ShapeReflectionProbe {
    float ambiance = 0, clip_distance = 0;
    std::uint8_t flags = 0;
};
struct ShapeMaterialEntry {
    std::uint8_t te_index = 0;
    Uuid id;
};
struct ShapeMaterialOverride {
    std::uint8_t te_index = 0;
    std::optional<std::u16string> data;
};
struct ShapeRenderMaterials {
    std::shared_ptr<ShapeArray<ShapeMaterialEntry>> entries;
    std::shared_ptr<ShapeArray<ShapeMaterialOverride>> overrides;
};
struct ShapeMediaEntry {
    bool enable_alternative_image = false, auto_loop = false, auto_play = false, auto_scale = false,
         auto_zoom = false, interact_on_first_click = false, enable_whitelist = false;
    std::int32_t controls = 0, height = 0, width = 0;
    std::uint8_t control_permissions = 0, interact_permissions = 0;
    std::optional<std::u16string> home_url, current_url;
    std::shared_ptr<ShapeArray<std::optional<std::u16string>>> whitelist;
};
// System.Xml reader state and the complete SDK LLSD parser remain explicit
// dependencies; no DOM/ISO/empty-list fallback silently replaces these contracts.
class ShapeMediaXmlReader {
  public:
    virtual ~ShapeMediaXmlReader() = default;
    virtual void dtd_processing_ignore() = 0;
    virtual void move_to_content() = 0;
    virtual std::optional<std::u16string> get_attribute(std::u16string_view) = 0;
    virtual void read_start_element(std::u16string_view) = 0;
    virtual std::u16string read_inner_xml() = 0;
    virtual bool is_empty_element() = 0;
    // Dispose reader and its factory-owned StringReader source in source order.
    virtual void dispose() = 0;
};
class ShapeMediaXmlWriter {
  public:
    virtual ~ShapeMediaXmlWriter() = default;
    virtual void write_raw(std::u16string) = 0;
};
using ShapeMediaIntegerFormat = std::function<std::u16string(std::int32_t)>;
std::u16string format_invariant_shape_media_integer(std::int32_t);
std::shared_ptr<ShapeMediaEntry> shape_media_entry_from_invariant_osd(const network::Value &);
struct ShapeMediaXmlBackend {
    std::function<std::shared_ptr<ShapeMediaXmlReader>(const std::optional<std::u16string> &)> create_reader;
    std::function<network::Value(std::u16string_view)> deserialize_llsd;
    // SDK MediaEntry coercions include CurrentCulture numeric parsing.
    std::function<std::shared_ptr<ShapeMediaEntry>(const network::Value &)> media_from_osd;
};
class ShapeMediaList {
  public:
    using Entry = std::shared_ptr<ShapeMediaEntry>;
    // Sink for the original Debug-level MOAP decoding message.
    using Log = std::function<void(const std::string &)>;
    inline static constexpr std::u16string_view MEDIA_TEXTURE_TYPE = u"sl";
    explicit ShapeMediaList(Log);
    ShapeMediaList(int capacity, Log);
    ShapeMediaList(const std::optional<std::vector<Entry>> &, Log);
    std::nullptr_t get_schema() const noexcept { return nullptr; }
    std::u16string to_xml(const ShapeMediaIntegerFormat &) const;
    void write_xml(ShapeMediaXmlWriter &, const ShapeMediaIntegerFormat &) const;
    void write_xml(const std::shared_ptr<ShapeMediaXmlWriter> &, const ShapeMediaIntegerFormat &) const;
    void read_xml(const std::optional<std::u16string> &, const ShapeMediaXmlBackend &);
    void read_xml(ShapeMediaXmlReader &, const ShapeMediaXmlBackend &);
    void read_xml(const std::shared_ptr<ShapeMediaXmlReader> &, const ShapeMediaXmlBackend &);
    static std::shared_ptr<ShapeMediaList> from_xml(const std::optional<std::u16string> &,
                                                    const ShapeMediaXmlBackend &, Log);
    void add(Entry entry) { entries.push_back(std::move(entry)); }
    std::size_t size() const noexcept { return entries.size(); }
    // Source callers are required to lock the MediaList while manipulating it.
    mutable std::recursive_mutex mutex;
    std::vector<Entry> entries;

  private:
    Log log_;
};
// Exact SDK property projection consumed/produced by PrimitiveBaseShape. This is
// not a replacement for unrelated Primitive network/particle/physics methods.
struct ShapeOmvConstruction {
    std::uint8_t profile_curve = 0, path_curve = 0, state = 0, pcode = 0, material = 0;
    float path_end = 0, path_radius_offset = 0, path_skew = 0, path_scale_x = 0, path_scale_y = 0,
          path_shear_x = 0, path_shear_y = 0, path_taper_x = 0, path_taper_y = 0, path_begin = 0,
          path_twist = 0, path_twist_begin = 0, path_revolutions = 0, profile_begin = 0, profile_end = 0,
          profile_hollow = 0;
};
struct ShapeOmvSculpt {
    Uuid texture;
    std::uint8_t raw_type = 0;
    std::uint8_t type() const noexcept { return raw_type & 7; }
};
struct ShapeOmvFlexible {
    int softness = 0;
    float tension = 0, drag = 0, gravity = 0, wind = 0;
    Vector3 force;
};
struct ShapeOmvLight {
    std::array<float, 4> color{};
    float intensity = 0, radius = 0, cutoff = 0, falloff = 0;
};
struct ShapeOmvProperties {
    Uuid object_id, creator_id, owner_id, group_id, item_id, folder_id, from_task_id, last_owner_id;
    std::uint64_t creation_date_ticks = 0;
    std::array<std::uint32_t, 5> permissions{};
    std::int32_t ownership_cost = 0, sale_price = 0, category = 0;
    std::uint8_t sale_type = 0, aggregate_perms = 0, aggregate_perm_textures = 0,
                 aggregate_perm_textures_owner = 0;
    std::int16_t inventory_serial = 0;
    std::optional<std::u16string> name = u"", description = u"", touch_name = u"", sit_name = u"";
    std::shared_ptr<ShapeArray<Uuid>> texture_ids;
};
struct ShapeOmvPrimitive {
    ShapeOmvConstruction prim_data;
    Vector3 scale, position;
    Quaternion rotation;
    std::shared_ptr<TextureEntry> textures;
    std::shared_ptr<ShapeOmvSculpt> sculpt;
    std::shared_ptr<ShapeOmvFlexible> flexible;
    std::shared_ptr<ShapeOmvLight> light;
    std::shared_ptr<ShapeOmvProperties> properties;
};
class PrimitiveBaseShapeCore {
  public:
    using Bytes = std::shared_ptr<ShapeByteArray>;
    // Sink for the original Warn-level shape/profile/texture messages.
    using Log = std::function<void(const std::string &)>;
    explicit PrimitiveBaseShapeCore(Log log);
    virtual ~PrimitiveBaseShapeCore() = default;
    PrimitiveBaseShapeCore(const std::shared_ptr<ShapeOmvPrimitive> &, Log);
    ShapeOmvPrimitive to_omv_primitive() const;
    ShapeOmvPrimitive to_omv_primitive(Vector3 position, Quaternion rotation) const;
    virtual void get_mesh() {}
    void set_path_range(Vector3 range) { set_path_range(range.x, range.y); }
    void set_path_range(float begin, float end);
    void set_profile_range(Vector3 range) { set_profile_range(range.x, range.y); }
    void set_profile_range(float begin, float end);
    using StartsWith = std::function<bool(std::u16string_view, std::u16string_view)>;
    Bytes render_overrides_to_raw() const;
    void render_overrides_from_raw(const Bytes &, const StartsWith &);
    PrimitiveBaseShapeCore copy() const { return *this; }
    static PrimitiveBaseShapeCore create(Log log) { return PrimitiveBaseShapeCore(std::move(log)); }
    static PrimitiveBaseShapeCore create_box(Log);
    static PrimitiveBaseShapeCore create_sphere(Log);
    static PrimitiveBaseShapeCore create_cylinder(Log);
    static PrimitiveBaseShapeCore create_mesh(int faces, Uuid mesh, Log);
    static PrimitiveBaseShapeCore default_shape(Log);
    static PrimitiveBaseShapeCore create_cylinder(float radius, float height, Log);
    void set_scale(float side) { scale = {side, side, side}; }
    void set_heigth(float height) { scale.z = height; }
    void set_radius(float radius) { scale.x = scale.y = radius * 2.f; }
    std::uint8_t profile_curve() const noexcept { return profile_shape | hollow_shape; }
    void profile_curve(std::uint8_t);
    float light_color(unsigned channel) const;
    void light_color(unsigned channel, float value);
    bool animesh_enabled() const noexcept;
    void animesh_enabled(bool);
    bool mesh_flag_entry() const noexcept { return mesh_flags_entry_; }
    std::uint32_t mesh_flags() const noexcept { return mesh_flags_; }
    Bytes texture_entry() const { return texture_; }
    void texture_entry(Bytes value);
    TextureEntry textures() const;
    void textures(const TextureEntry &);
    void textures(const std::shared_ptr<TextureEntry> &);
    void set_sculpt_properties(std::uint8_t type, Uuid texture) {
        sculpt_type = type;
        sculpt_texture = texture;
    }
    PrimGeometry geometry() const noexcept;
    std::vector<std::uint8_t> extra_params() const;
    void read_extra_params(const Bytes &);
    void update_extra_param(std::uint16_t, bool, const Bytes &);
    // SDK native-memory reads outside buffers raise NativeShapeUnsafeRead.
    // Such memory observations (including negative positions) are not a portable contract.
    void read_sculpt(const Bytes &, int pos);
    void read_flexi(const Bytes &, int pos);
    void read_light(const Bytes &, int pos);
    void read_projection(const Bytes &, int pos);
    void read_mesh_flags(const Bytes &, int pos);
    void read_reflection_probe(const Bytes &, int pos);
    int read_render_materials(const Bytes &, int pos, int size);
    std::uint64_t mesh_key(Vector3 size, float lod, bool convex = false) const noexcept;
    std::uint16_t path_begin = 0, path_end = 0, profile_begin = 0, profile_end = 0, profile_hollow = 0;
    std::uint8_t path_curve = 0, path_scale_x = 0, path_scale_y = 0, path_shear_x = 0, path_shear_y = 0,
                 path_revolutions = 0, pcode = 9, state = 0, last_attach_point = 0, profile_shape = 0,
                 hollow_shape = 0;
    std::int8_t path_radius_offset = 0, path_skew = 0, path_taper_x = 0, path_taper_y = 0, path_twist = 0,
                path_twist_begin = 0;
    Vector3 scale;
    Uuid sculpt_texture, projection_texture;
    std::uint8_t sculpt_type = 0;
    Bytes sculpt_data;
    int flexi_softness = 0;
    float flexi_tension = 0, flexi_drag = 0, flexi_gravity = 0, flexi_wind = 0, flexi_force_x = 0,
          flexi_force_y = 0, flexi_force_z = 0;
    float light_radius = 0, light_cutoff = 0, light_falloff = 0, light_intensity = 1;
    float projection_fov = 0, projection_focus = 0, projection_ambiance = 0;
    bool flexi_entry = false, light_entry = false, sculpt_entry = false, projection_entry = false;
    std::shared_ptr<ShapeMediaList> media;
    std::shared_ptr<ShapeReflectionProbe> reflection_probe;
    std::shared_ptr<ShapeRenderMaterials> render_materials;

  private:
    float light_color_r = 0, light_color_g = 0, light_color_b = 0, light_color_a = 1;
    std::shared_ptr<Log> log_;
    Bytes texture_;
    bool mesh_flags_entry_ = false;
    std::uint32_t mesh_flags_ = 0;
};
// Core spelling retained for callers of the earlier checkpoint.
using PrimitiveBaseShape = PrimitiveBaseShapeCore;

}
