// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <bit>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <type_traits>
#include <pugixml.hpp>
static_assert(!std::is_copy_assignable_v<opensim::framework::ShapeByteArray>);
using namespace opensim::framework;
using Bytes = PrimitiveBaseShapeCore::Bytes;
template <class Range> std::string hex(const Range& b) {
    std::string s;
    for (auto v : b) {
        s += "0123456789abcdef"[v >> 4];
        s += "0123456789abcdef"[v & 15];
    }
    return s;
}
Bytes bytes(const std::string& s) {
    if (s == "null")
        return {};
    std::vector<std::uint8_t> b;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 2)
            b.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
    return std::make_shared<ShapeByteArray>(std::move(b));
}
std::string encode(const std::string& s) {
    return hex(std::vector<std::uint8_t>(s.begin(), s.end()));
}
float number(const std::string& s) {
    return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(s, nullptr, 16)));
}
std::optional<std::u16string> text16(const std::string& s) {
    if (s == "null")
        return {};
    std::u16string result;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            result.push_back(static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16)));
    return result;
}
std::string raw16(const std::optional<std::u16string>& value) {
    if (!value)
        return "null";
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : *value)
        out << std::setw(4) << static_cast<unsigned>(c);
    return out.str();
}
std::string nullable_hex(const Bytes& value) {
    return value ? hex(*value) : "null";
}
class TraceMediaReader : public ShapeMediaXmlReader {
  public:
    std::function<void(const std::string&)> event;
    std::optional<std::u16string> type = u"sl";
    std::u16string inner = u"<llsd><array /></llsd>";
    bool empty = false;
    std::string fault;
    unsigned disposals = 0;
    explicit TraceMediaReader(std::function<void(const std::string&)> trace) : event(std::move(trace)) {
    }
    void call(const std::string& name) {
        event(name);
        if (fault == name)
            throw PrimitiveShapeError("System.InvalidOperationException", name + " fault");
    }
    void dtd_processing_ignore() override {
        call("dtd");
    }
    void move_to_content() override {
        call("move");
    }
    std::optional<std::u16string> get_attribute(std::u16string_view) override {
        call("attr");
        return type;
    }
    void read_start_element(std::u16string_view) override {
        call("start");
    }
    std::u16string read_inner_xml() override {
        call("inner");
        return inner;
    }
    bool is_empty_element() override {
        call("empty");
        return empty;
    }
    void dispose() override {
        ++disposals;
        call("dispose");
    }
};
class TraceMediaWriter : public ShapeMediaXmlWriter {
  public:
    std::function<void(std::u16string)> write;
    explicit TraceMediaWriter(std::function<void(std::u16string)> sink) : write(std::move(sink)) {
    }
    void write_raw(std::u16string value) override {
        write(std::move(value));
    }
};
// Fixture-only DOM adapter. Production reader/LLSD obligations stay explicit.
class FixtureMediaReader : public ShapeMediaXmlReader {
    pugi::xml_document doc_;
    pugi::xml_node root_, current_;
    bool empty_ = false;

  public:
    explicit FixtureMediaReader(const std::optional<std::u16string>& text) {
        if (!text)
            throw std::runtime_error("null XML");
        for (std::size_t i = 0; i < text->size(); ++i) {
            auto c = (*text)[i];
            if (c < 32 && c != 9 && c != 10 && c != 13)
                throw std::runtime_error("XML control");
            if (c >= 0xd800 && c <= 0xdbff) {
                if (++i == text->size() || (*text)[i] < 0xdc00 || (*text)[i] > 0xdfff)
                    throw std::runtime_error("XML surrogate");
            } else if (c >= 0xdc00 && c <= 0xdfff)
                throw std::runtime_error("XML surrogate");
        }
        auto data = utf8(*text);
        if (!doc_.load_buffer(data.data(), data.size(), pugi::parse_default | pugi::parse_ws_pcdata))
            throw std::runtime_error("Invalid XML");
        root_ = doc_.document_element();
        current_ = root_;
        auto end = data.find('>');
        empty_ = end != std::string::npos && end > 0 && data[end - 1] == '/';
    }
    void dtd_processing_ignore() override {
    }
    void move_to_content() override {
    }
    std::optional<std::u16string> get_attribute(std::u16string_view name) override {
        auto a = root_.attribute(utf8(name).c_str());
        return a ? std::optional<std::u16string>(utf16_replacing(a.value())) : std::nullopt;
    }
    void read_start_element(std::u16string_view name) override {
        if (utf8(name) != root_.name())
            throw std::runtime_error("Wrong root");
        current_ = root_.first_child();
    }
    std::u16string read_inner_xml() override {
        std::ostringstream out;
        if (current_.type() == pugi::node_element)
            for (auto n : current_.children())
                n.print(out, "", pugi::format_raw);
        return utf16_replacing(out.str());
    }
    bool is_empty_element() override {
        return empty_;
    }
    void dispose() override {
    }
};
ShapeMediaXmlBackend media_backend() {
    ShapeMediaXmlBackend b;
    b.create_reader = [](const auto& text) { return std::make_shared<FixtureMediaReader>(text); };
    b.deserialize_llsd = [](auto xml) {
        try {
            return opensim::network::parse_llsd_xml(utf8(xml));
        } catch (...) {
            return opensim::network::Value{};
        }
    };
    b.media_from_osd = shape_media_entry_from_invariant_osd;
    return b;
}
std::string media_snapshot(const std::shared_ptr<ShapeMediaList>& list) {
    if (!list)
        return "null";
    std::ostringstream o;
    o << list->size() << ":";
    for (const auto& p : list->entries) {
        if (!p) {
            o << "null;";
            continue;
        }
        const auto& e = *p;
        o << e.enable_alternative_image << e.auto_loop << e.auto_play << e.auto_scale << e.auto_zoom
          << e.interact_on_first_click << e.enable_whitelist << "," << e.controls << "," << e.height << ","
          << e.width << "," << int(e.control_permissions) << "," << int(e.interact_permissions) << ","
          << raw16(e.home_url) << "," << raw16(e.current_url) << ",";
        if (!e.whitelist)
            o << "null";
        else
            for (const auto& v : *e.whitelist)
                o << raw16(v) << ":";
        o << ";";
    }
    return o.str();
}
std::string omv_snapshot(const ShapeOmvPrimitive& s) {
    std::ostringstream o;
    const auto& p = s.prim_data;
    o << std::bit_cast<std::uint32_t>(p.path_end) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_radius_offset) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_skew) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_scale_x) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_scale_y) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_shear_x) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_shear_y) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_taper_x) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_taper_y) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_begin) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_twist) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_twist_begin) << ",";
    o << std::bit_cast<std::uint32_t>(p.path_revolutions) << ",";
    o << std::bit_cast<std::uint32_t>(p.profile_begin) << ",";
    o << std::bit_cast<std::uint32_t>(p.profile_end) << ",";
    o << std::bit_cast<std::uint32_t>(p.profile_hollow) << ",";
    o << int(p.profile_curve) << "," << int(p.path_curve) << "," << int(p.state) << "," << int(p.pcode) << ","
      << int(p.material) << ",";
    o << std::bit_cast<std::uint32_t>(s.scale.x) << ",";
    o << std::bit_cast<std::uint32_t>(s.scale.y) << ",";
    o << std::bit_cast<std::uint32_t>(s.scale.z) << ",";
    o << std::bit_cast<std::uint32_t>(s.position.x) << ",";
    o << std::bit_cast<std::uint32_t>(s.position.y) << ",";
    o << std::bit_cast<std::uint32_t>(s.position.z) << ",";
    o << std::bit_cast<std::uint32_t>(s.rotation.x) << ",";
    o << std::bit_cast<std::uint32_t>(s.rotation.y) << ",";
    o << std::bit_cast<std::uint32_t>(s.rotation.z) << ",";
    o << std::bit_cast<std::uint32_t>(s.rotation.w) << ",";
    o << (s.textures ? hex(s.textures->bytes()) : "null") << ",";
    if (s.sculpt) {
        o << s.sculpt->texture.str() << ":" << int(s.sculpt->raw_type) << ":" << int(s.sculpt->type());
    } else
        o << "null";
    o << ",";
    if (s.flexible) {
        auto& f = *s.flexible;
        o << f.softness << ":";
        for (float v : {f.tension, f.drag, f.gravity, f.wind, f.force.x, f.force.y, f.force.z})
            o << std::bit_cast<std::uint32_t>(v) << ":";
    } else
        o << "null";
    o << ",";
    if (s.light) {
        auto& l = *s.light;
        for (float v :
             {l.color[0], l.color[1], l.color[2], l.color[3], l.intensity, l.radius, l.cutoff, l.falloff})
            o << std::bit_cast<std::uint32_t>(v) << ":";
    } else
        o << "null";
    o << ",";
    if (s.properties) {
        auto& v = *s.properties;
        o << raw16(v.name) << ":" << raw16(v.description) << ":" << raw16(v.touch_name) << ":"
          << raw16(v.sit_name) << ":" << v.object_id.str() << ":" << v.creator_id.str() << ":"
          << v.owner_id.str() << ":" << v.group_id.str() << ":" << v.item_id.str() << ":" << v.folder_id.str()
          << ":" << v.from_task_id.str() << ":" << v.last_owner_id.str() << ":" << v.creation_date_ticks
          << ":";
        for (auto n : v.permissions)
            o << n << ":";
        o << v.ownership_cost << ":" << v.sale_price << ":" << v.category << ":" << int(v.sale_type) << ":"
          << int(v.aggregate_perms) << ":" << int(v.aggregate_perm_textures) << ":"
          << int(v.aggregate_perm_textures_owner) << ":" << v.inventory_serial << ":"
          << (v.texture_ids ? "array" : "null");
    } else
        o << "null";
    return o.str();
}
void seed(PrimitiveBaseShapeCore& s) {
    s.flexi_entry = s.light_entry = s.sculpt_entry = s.projection_entry = true;
    s.sculpt_type = 5;
    s.sculpt_texture = Uuid("00112233-4455-6677-8899-aabbccddeeff");
    s.projection_texture = s.sculpt_texture;
    s.flexi_softness = 3;
    s.flexi_tension = 0.25f;
    s.flexi_drag = 0.5f;
    s.flexi_gravity = 0.75f;
    s.flexi_wind = 1.0f;
    s.flexi_force_x = 1.25f;
    s.flexi_force_y = 1.5f;
    s.flexi_force_z = 1.75f;
    s.light_color(0, 2.0f);
    s.light_color(1, 2.25f);
    s.light_color(2, 2.5f);
    s.light_color(3, 2.75f);
    s.light_radius = 3.0f;
    s.light_cutoff = 3.25f;
    s.light_falloff = 3.5f;
    s.light_intensity = 3.75f;
    s.projection_fov = 4.0f;
    s.projection_focus = 4.25f;
    s.projection_ambiance = 4.5f;
    s.read_mesh_flags(bytes("ffffffff"), 0);
    s.reflection_probe = std::make_shared<ShapeReflectionProbe>(ShapeReflectionProbe{.5f, 2.f, 9});
    s.render_materials = std::make_shared<ShapeRenderMaterials>();
    s.render_materials->entries =
        std::make_shared<ShapeArray<ShapeMaterialEntry>>(1, ShapeMaterialEntry{7, s.sculpt_texture});
}
std::string snapshot(const PrimitiveBaseShapeCore& s) {
    std::ostringstream o;
    o << std::bit_cast<std::uint32_t>(s.scale.x) << "," << std::bit_cast<std::uint32_t>(s.scale.y) << ","
      << std::bit_cast<std::uint32_t>(s.scale.z) << ",";
    o << int(s.path_begin) << ",";
    o << int(s.path_curve) << ",";
    o << int(s.path_end) << ",";
    o << int(s.path_radius_offset) << ",";
    o << int(s.path_revolutions) << ",";
    o << int(s.path_scale_x) << ",";
    o << int(s.path_scale_y) << ",";
    o << int(s.path_shear_x) << ",";
    o << int(s.path_shear_y) << ",";
    o << int(s.path_skew) << ",";
    o << int(s.path_taper_x) << ",";
    o << int(s.path_taper_y) << ",";
    o << int(s.path_twist) << ",";
    o << int(s.path_twist_begin) << ",";
    o << int(s.pcode) << ",";
    o << int(s.profile_begin) << ",";
    o << int(s.profile_end) << ",";
    o << int(s.profile_hollow) << ",";
    o << int(s.state) << ",";
    o << int(s.last_attach_point) << ",";
    o << int(s.profile_shape) << ",";
    o << int(s.hollow_shape) << ",";
    o << int(s.sculpt_type) << ",";
    o << int(s.flexi_softness) << ",";
    o << std::bit_cast<std::uint32_t>(s.flexi_tension) << ",";
    o << std::bit_cast<std::uint32_t>(s.flexi_drag) << ",";
    o << std::bit_cast<std::uint32_t>(s.flexi_gravity) << ",";
    o << std::bit_cast<std::uint32_t>(s.flexi_wind) << ",";
    o << std::bit_cast<std::uint32_t>(s.flexi_force_x) << ",";
    o << std::bit_cast<std::uint32_t>(s.flexi_force_y) << ",";
    o << std::bit_cast<std::uint32_t>(s.flexi_force_z) << ",";
    o << std::bit_cast<std::uint32_t>(s.light_color(0)) << ",";
    o << std::bit_cast<std::uint32_t>(s.light_color(1)) << ",";
    o << std::bit_cast<std::uint32_t>(s.light_color(2)) << ",";
    o << std::bit_cast<std::uint32_t>(s.light_color(3)) << ",";
    o << std::bit_cast<std::uint32_t>(s.light_radius) << ",";
    o << std::bit_cast<std::uint32_t>(s.light_cutoff) << ",";
    o << std::bit_cast<std::uint32_t>(s.light_falloff) << ",";
    o << std::bit_cast<std::uint32_t>(s.light_intensity) << ",";
    o << std::bit_cast<std::uint32_t>(s.projection_fov) << ",";
    o << std::bit_cast<std::uint32_t>(s.projection_focus) << ",";
    o << std::bit_cast<std::uint32_t>(s.projection_ambiance) << ",";
    o << s.flexi_entry << ",";
    o << s.light_entry << ",";
    o << s.sculpt_entry << ",";
    o << s.projection_entry << ",";
    o << s.mesh_flag_entry() << "," << s.mesh_flags() << "," << s.animesh_enabled() << ","
      << s.sculpt_texture.str() << "," << s.projection_texture.str() << "," << hex(*s.texture_entry()) << ",";
    if (s.reflection_probe)
        o << std::bit_cast<std::uint32_t>(s.reflection_probe->ambiance) << ":"
          << std::bit_cast<std::uint32_t>(s.reflection_probe->clip_distance) << ":"
          << int(s.reflection_probe->flags);
    else
        o << "null";
    o << ",";
    if (!s.render_materials)
        o << "null";
    else if (!s.render_materials->entries)
        o << "none";
    else
        for (const auto& e : *s.render_materials->entries)
            o << int(e.te_index) << ":" << e.id.str() << ";";
    o << "," << hex(s.extra_params()) << ",";
    if (!s.render_materials)
        o << "nomat";
    else if (!s.render_materials->overrides)
        o << "null";
    else
        for (const auto& e : *s.render_materials->overrides)
            o << int(e.te_index) << ":" << raw16(e.data) << ";";
    o << "," << nullable_hex(s.render_overrides_to_raw()) << "," << nullable_hex(s.sculpt_data) << ","
      << media_snapshot(s.media);
    return o.str();
}
int main(int argc, char** argv) {
    try {
        std::ios::sync_with_stdio(false);
        std::cin.tie(nullptr);
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            std::string line;
            while (std::getline(std::cin, line)) {
                std::vector<std::string> logs;
                auto log = [&](const auto& message) { logs.push_back(message); };
                PrimitiveBaseShapeCore s(log);
                std::u16string negative_sign = u"-";
                ShapeMediaIntegerFormat format = [&](std::int32_t v) {
                    return v < 0 ? negative_sign + utf16(std::to_string(-std::int64_t(v)))
                                 : utf16(std::to_string(v));
                };
                std::string error = "ok", result = "";
                std::stringstream steps(line);
                std::string step;
                while (std::getline(steps, step, '|')) {
                    try {
                        std::istringstream in(step);
                        std::string op;
                        in >> op;
                        if (op == "seed")
                            seed(s);
                        else if (op == "factory") {
                            int k, n;
                            in >> k >> n;
                            switch (k) {
                            case 0:
                                s = PrimitiveBaseShapeCore(log);
                                break;
                            case 1:
                                s = PrimitiveBaseShapeCore::create_box(log);
                                break;
                            case 2:
                                s = PrimitiveBaseShapeCore::create_sphere(log);
                                break;
                            case 3:
                                s = PrimitiveBaseShapeCore::create_cylinder(log);
                                break;
                            case 4:
                                s = PrimitiveBaseShapeCore::default_shape(log);
                                break;
                            case 5:
                                s = PrimitiveBaseShapeCore::create_mesh(
                                    n, Uuid("00112233-4455-6677-8899-aabbccddeeff"), log);
                                break;
                            case 6:
                                s = PrimitiveBaseShapeCore::create_cylinder(1.25f, 2.5f, log);
                                break;
                            case 7:
                                s = PrimitiveBaseShapeCore::create(log);
                                break;
                            }
                        } else if (op == "profile") {
                            unsigned v;
                            in >> v;
                            s.profile_curve(static_cast<std::uint8_t>(v));
                        } else if (op == "rawprofile") {
                            unsigned h, p;
                            in >> h >> p;
                            s.hollow_shape = static_cast<std::uint8_t>(h);
                            s.profile_shape = static_cast<std::uint8_t>(p);
                        } else if (op == "animesh") {
                            bool value;
                            in >> value;
                            s.animesh_enabled(value);
                        } else if (op == "sculpt") {
                            unsigned t;
                            bool e;
                            in >> t >> e;
                            s.sculpt_type = static_cast<std::uint8_t>(t);
                            s.sculpt_entry = e;
                        } else if (op == "read") {
                            int k, p, size;
                            std::string data;
                            in >> k >> p >> size >> data;
                            auto b = bytes(data);
                            switch (k) {
                            case 0:
                                s.read_extra_params(b);
                                break;
                            case 1:
                                s.read_sculpt(b, p);
                                break;
                            case 2:
                                s.read_flexi(b, p);
                                break;
                            case 3:
                                s.read_light(b, p);
                                break;
                            case 4:
                                s.read_projection(b, p);
                                break;
                            case 5:
                                s.read_mesh_flags(b, p);
                                break;
                            case 6:
                                s.read_reflection_probe(b, p);
                                break;
                            case 7:
                                result = std::to_string(s.read_render_materials(b, p, size));
                                break;
                            }
                        } else if (op == "update") {
                            unsigned k;
                            bool use;
                            std::string data;
                            in >> k >> use >> data;
                            s.update_extra_param(static_cast<std::uint16_t>(k), use, bytes(data));
                        } else if (op == "color") {
                            unsigned c;
                            std::string value;
                            in >> c >> value;
                            s.light_color(c, number(value));
                        } else if (op == "texture") {
                            std::string data;
                            in >> data;
                            s.texture_entry(bytes(data));
                        } else if (op == "medianew") {
                            int capacity;
                            in >> capacity;
                            s.media = std::make_shared<ShapeMediaList>(capacity, log);
                        } else if (op == "mediaadd") {
                            bool valid;
                            in >> valid;
                            s.media->add(valid ? std::make_shared<ShapeMediaEntry>() : nullptr);
                        } else if (op == "mediafield") {
                            int k;
                            std::string raw;
                            in >> k >> raw;
                            auto& e = *s.media->entries.back();
                            if (k == 0)
                                e.current_url = text16(raw);
                            else
                                e.home_url = text16(raw);
                        } else if (op == "mediawhite") {
                            int index;
                            std::string raw;
                            in >> index >> raw;
                            s.media->entries.back()->whitelist->at(index) = text16(raw);
                        } else if (op == "mediawhites") {
                            int count;
                            in >> count;
                            s.media->entries.back()->whitelist =
                                count < 0
                                    ? nullptr
                                    : std::make_shared<ShapeArray<std::optional<std::u16string>>>(count);
                        } else if (op == "mediavalue") {
                            int index, value;
                            in >> index >> value;
                            auto& e = *s.media->entries.back();
                            switch (index) {
                            case 0:
                                e.enable_alternative_image = value != 0;
                                break;
                            case 1:
                                e.auto_loop = value != 0;
                                break;
                            case 2:
                                e.auto_play = value != 0;
                                break;
                            case 3:
                                e.auto_scale = value != 0;
                                break;
                            case 4:
                                e.auto_zoom = value != 0;
                                break;
                            case 5:
                                e.interact_on_first_click = value != 0;
                                break;
                            case 6:
                                e.enable_whitelist = value != 0;
                                break;
                            case 7:
                                e.controls = value;
                                break;
                            case 8:
                                e.height = value;
                                break;
                            case 9:
                                e.width = value;
                                break;
                            case 10:
                                e.control_permissions = static_cast<std::uint8_t>(value);
                                break;
                            case 11:
                                e.interact_permissions = static_cast<std::uint8_t>(value);
                                break;
                            }
                        } else if (op == "mediacollect") {
                            bool copy;
                            in >> copy;
                            if (copy) {
                                auto old = s.media;
                                s.media = std::make_shared<ShapeMediaList>(std::optional(old->entries), log);
                                result = std::to_string(old != s.media) +
                                         std::to_string(old->entries.empty() ||
                                                        old->entries.front() == s.media->entries.front());
                            } else
                                s.media = std::make_shared<ShapeMediaList>(
                                    std::optional<std::vector<ShapeMediaList::Entry>>{}, log);
                        } else if (op == "mediawrite") {
                            bool fault;
                            in >> fault;
                            TraceMediaWriter writer([&](auto value) {
                                result = "raw:" + raw16(value) + ";";
                                if (fault)
                                    throw PrimitiveShapeError("System.InvalidOperationException",
                                                              "writer fault");
                            });
                            s.media->write_xml(writer, format);
                        } else if (op == "mediawritenull") {
                            s.media->write_xml(std::shared_ptr<ShapeMediaXmlWriter>{}, format);
                        } else if (op == "mediareadnull") {
                            s.media->read_xml(std::shared_ptr<ShapeMediaXmlReader>{}, media_backend());
                        } else if (op == "mediaouterfault") {
                            bool empty;
                            int fault;
                            std::string raw;
                            in >> empty >> fault >> raw;
                            TraceMediaReader reader([&](auto event) { result += event + ";"; });
                            reader.empty = empty;
                            reader.fault = fault == 1 ? "empty" : (fault == 2 ? "inner" : "");
                            reader.inner = *text16(raw);
                            s.media->read_xml(reader, media_backend());
                        } else if (op == "mediaculture") {
                            std::string raw;
                            in >> raw;
                            negative_sign = *text16(raw);
                        } else if (op == "mediamutate") {
                            bool first = true;
                            result = raw16(s.media->to_xml([&](std::int32_t value) {
                                if (first) {
                                    first = false;
                                    auto e = s.media->entries.front();
                                    e->current_url = u"changed";
                                    e->height = -99;
                                    (*e->whitelist)[0] = u"changed";
                                    s.media->add(nullptr);
                                }
                                return format_invariant_shape_media_integer(value);
                            }));
                        } else if (op == "mediaxml") {
                            result = raw16(s.media->to_xml(format));
                        } else if (op == "mediaread") {
                            std::string raw;
                            in >> raw;
                            s.media->read_xml(text16(raw), media_backend());
                        } else if (op == "mediafrom") {
                            std::string raw;
                            in >> raw;
                            s.media = ShapeMediaList::from_xml(text16(raw), media_backend(), log);
                        } else if (op == "mediaouter") {
                            std::string raw;
                            in >> raw;
                            FixtureMediaReader reader(text16(raw));
                            s.media->read_xml(reader, media_backend());
                        } else if (op == "fromomv") {
                            int profile, path, state, pcode, sculpt, texture;
                            in >> profile >> path >> state >> pcode >> sculpt >> texture;
                            auto prim = std::make_shared<ShapeOmvPrimitive>();
                            auto& p = prim->prim_data;
                            p.profile_curve = static_cast<std::uint8_t>(profile);
                            p.path_curve = static_cast<std::uint8_t>(path);
                            p.state = static_cast<std::uint8_t>(state);
                            p.pcode = static_cast<std::uint8_t>(pcode);
                            std::string raw;
                            in >> raw;
                            p.path_end = number(raw);
                            in >> raw;
                            p.path_radius_offset = number(raw);
                            in >> raw;
                            p.path_skew = number(raw);
                            in >> raw;
                            p.path_scale_x = number(raw);
                            in >> raw;
                            p.path_scale_y = number(raw);
                            in >> raw;
                            p.path_shear_x = number(raw);
                            in >> raw;
                            p.path_shear_y = number(raw);
                            in >> raw;
                            p.path_taper_x = number(raw);
                            in >> raw;
                            p.path_taper_y = number(raw);
                            in >> raw;
                            p.path_begin = number(raw);
                            in >> raw;
                            p.path_twist = number(raw);
                            in >> raw;
                            p.path_twist_begin = number(raw);
                            in >> raw;
                            p.path_revolutions = number(raw);
                            in >> raw;
                            p.profile_begin = number(raw);
                            in >> raw;
                            p.profile_end = number(raw);
                            in >> raw;
                            p.profile_hollow = number(raw);
                            for (float* value : {&prim->scale.x, &prim->scale.y, &prim->scale.z}) {
                                in >> raw;
                                *value = number(raw);
                            }
                            if (texture)
                                prim->textures = std::make_shared<TextureEntry>(
                                    Uuid("00112233-4455-6677-8899-aabbccddeeff"));
                            if (sculpt >= 0)
                                prim->sculpt = std::make_shared<ShapeOmvSculpt>(
                                    ShapeOmvSculpt{Uuid("00112233-4455-6677-8899-aabbccddeeff"),
                                                   static_cast<std::uint8_t>(sculpt)});
                            s = PrimitiveBaseShapeCore(prim, log);
                        } else if (op == "convert") {
                            int positioned;
                            in >> positioned;
                            result = omv_snapshot(positioned ? s.to_omv_primitive({1, 2, 3}, {4, 5, 6, 7})
                                                             : s.to_omv_primitive());
                        } else if (op == "nullomv") {
                            s = PrimitiveBaseShapeCore(std::shared_ptr<ShapeOmvPrimitive>{}, log);
                        } else if (op == "dimension") {
                            int kind;
                            std::string raw;
                            in >> kind >> raw;
                            float value = number(raw);
                            if (kind == 0)
                                s.set_scale(value);
                            else if (kind == 1)
                                s.set_heigth(value);
                            else
                                s.set_radius(value);
                        } else if (op == "sculptprops") {
                            unsigned type;
                            std::string id;
                            in >> type >> id;
                            s.set_sculpt_properties(static_cast<std::uint8_t>(type), Uuid(id));
                        } else if (op == "textures") {
                            std::string id;
                            in >> id;
                            s.textures(TextureEntry(Uuid(id)));
                        } else if (op == "getmesh") {
                            s.get_mesh();
                        } else if (op == "range") {
                            int kind;
                            std::string a, b;
                            in >> kind >> a >> b;
                            float x = number(a), y = number(b);
                            if (kind == 0)
                                s.set_path_range(x, y);
                            else if (kind == 1)
                                s.set_profile_range(x, y);
                            else if (kind == 2)
                                s.set_path_range(Vector3{x, y, 9});
                            else
                                s.set_profile_range(Vector3{x, y, 9});
                        } else if (op == "ovrnew") {
                            int n;
                            in >> n;
                            if (n < 0) {
                                s.render_materials.reset();
                            } else {
                                s.render_materials = std::make_shared<ShapeRenderMaterials>();
                                if (n > 0)
                                    s.render_materials->overrides =
                                        std::make_shared<ShapeArray<ShapeMaterialOverride>>(n);
                            }
                        } else if (op == "ovrset") {
                            int index, face;
                            std::string raw;
                            in >> index >> face >> raw;
                            s.render_materials->overrides->at(index) = {static_cast<std::uint8_t>(face),
                                                                        text16(raw)};
                        } else if (op == "ovrread") {
                            std::string raw;
                            in >> raw;
                            s.render_overrides_from_raw(bytes(raw), [](auto text, auto prefix) {
                                return invariant_starts_with(utf8(text), utf8(prefix));
                            });
                        } else if (op == "float") {
                            int index;
                            std::string raw;
                            in >> index >> raw;
                            float value = number(raw);
                            switch (index) {
                            case 0:
                                s.flexi_tension = value;
                                break;
                            case 1:
                                s.flexi_drag = value;
                                break;
                            case 2:
                                s.flexi_gravity = value;
                                break;
                            case 3:
                                s.flexi_wind = value;
                                break;
                            case 4:
                                s.flexi_force_x = value;
                                break;
                            case 5:
                                s.flexi_force_y = value;
                                break;
                            case 6:
                                s.flexi_force_z = value;
                                break;
                            case 7:
                                s.light_color(0, value);
                                break;
                            case 8:
                                s.light_color(1, value);
                                break;
                            case 9:
                                s.light_color(2, value);
                                break;
                            case 10:
                                s.light_color(3, value);
                                break;
                            case 11:
                                s.light_radius = value;
                                break;
                            case 12:
                                s.light_cutoff = value;
                                break;
                            case 13:
                                s.light_falloff = value;
                                break;
                            case 14:
                                s.light_intensity = value;
                                break;
                            case 15:
                                s.projection_fov = value;
                                break;
                            case 16:
                                s.projection_focus = value;
                                break;
                            case 17:
                                s.projection_ambiance = value;
                                break;
                            }
                        } else if (op == "set") {
                            int index, value;
                            in >> index >> value;
                            switch (index) {
                            case 0:
                                s.path_begin = static_cast<decltype(s.path_begin)>(value);
                                break;
                            case 1:
                                s.path_curve = static_cast<decltype(s.path_curve)>(value);
                                break;
                            case 2:
                                s.path_end = static_cast<decltype(s.path_end)>(value);
                                break;
                            case 3:
                                s.path_radius_offset = static_cast<decltype(s.path_radius_offset)>(value);
                                break;
                            case 4:
                                s.path_revolutions = static_cast<decltype(s.path_revolutions)>(value);
                                break;
                            case 5:
                                s.path_scale_x = static_cast<decltype(s.path_scale_x)>(value);
                                break;
                            case 6:
                                s.path_scale_y = static_cast<decltype(s.path_scale_y)>(value);
                                break;
                            case 7:
                                s.path_shear_x = static_cast<decltype(s.path_shear_x)>(value);
                                break;
                            case 8:
                                s.path_shear_y = static_cast<decltype(s.path_shear_y)>(value);
                                break;
                            case 9:
                                s.path_skew = static_cast<decltype(s.path_skew)>(value);
                                break;
                            case 10:
                                s.path_taper_x = static_cast<decltype(s.path_taper_x)>(value);
                                break;
                            case 11:
                                s.path_taper_y = static_cast<decltype(s.path_taper_y)>(value);
                                break;
                            case 12:
                                s.path_twist = static_cast<decltype(s.path_twist)>(value);
                                break;
                            case 13:
                                s.path_twist_begin = static_cast<decltype(s.path_twist_begin)>(value);
                                break;
                            case 14:
                                s.pcode = static_cast<decltype(s.pcode)>(value);
                                break;
                            case 15:
                                s.profile_begin = static_cast<decltype(s.profile_begin)>(value);
                                break;
                            case 16:
                                s.profile_end = static_cast<decltype(s.profile_end)>(value);
                                break;
                            case 17:
                                s.profile_hollow = static_cast<decltype(s.profile_hollow)>(value);
                                break;
                            case 18:
                                s.state = static_cast<decltype(s.state)>(value);
                                break;
                            case 19:
                                s.last_attach_point = static_cast<decltype(s.last_attach_point)>(value);
                                break;
                            case 20:
                                s.profile_shape = static_cast<decltype(s.profile_shape)>(value);
                                break;
                            case 21:
                                s.hollow_shape = static_cast<decltype(s.hollow_shape)>(value);
                                break;
                            case 22:
                                s.sculpt_type = static_cast<decltype(s.sculpt_type)>(value);
                                break;
                            case 23:
                                s.flexi_softness = static_cast<decltype(s.flexi_softness)>(value);
                                break;
                            }
                        } else if (op == "decode") {
                            result = hex(s.textures().bytes());
                        } else if (op == "nulltextures") {
                            s.textures(std::shared_ptr<TextureEntry>{});
                        } else if (op == "defaults") {
                            PrimitiveBaseShapeCore other(log);
                            result = std::to_string(s.texture_entry() == other.texture_entry()) +
                                     std::to_string(s.sculpt_data == other.sculpt_data);
                        } else if (op == "aliasreplace") {
                            seed(s);
                            auto other = s.copy();
                            other.read_render_materials(bytes("012100112233445566778899aabbccddeeff"), 0, 18);
                            other.read_reflection_probe(bytes("0000803f0000004007"), 0);
                            result = std::to_string(s.render_materials == other.render_materials) +
                                     std::to_string(s.reflection_probe == other.reflection_probe);
                        } else if (op == "alias") {
                            seed(s);
                            s.texture_entry(bytes("1234"));
                            s.sculpt_data = bytes("5678");
                            s.media = std::make_shared<ShapeMediaList>(log);
                            s.media->add(std::make_shared<ShapeMediaEntry>());
                            auto other = s.copy();
                            result = std::to_string(s.texture_entry() == other.texture_entry()) +
                                     std::to_string(s.sculpt_data == other.sculpt_data) +
                                     std::to_string(s.reflection_probe == other.reflection_probe) +
                                     std::to_string(s.render_materials == other.render_materials) +
                                     std::to_string(s.media == other.media);
                            other.media->add(nullptr);
                            other.profile_begin = 123;
                            other.texture_entry()->at(0) = 77;
                            other.sculpt_data->at(0) = 88;
                            other.reflection_probe->ambiance = 9;
                            other.render_materials->entries->at(0).te_index = 19;
                            result += ":" + std::to_string(s.profile_begin) + ":" + hex(*s.sculpt_data);
                        } else if (op == "meshkey") {
                            std::string x, y, z, l;
                            bool convex;
                            in >> x >> y >> z >> l >> convex;
                            result = std::to_string(
                                s.mesh_key({number(x), number(y), number(z)}, number(l), convex));
                        } else
                            throw std::runtime_error("Unknown shape probe operation");
                    } catch (const PrimitiveShapeError& e) {
                        error = e.source_type + ":" + encode(e.what()) + ":" +
                                (e.hresult ? std::to_string(*e.hresult) : "null") + ":" +
                                (e.parameter ? encode(*e.parameter) : "null");
                        break;
                    }
                }
                std::cout << error << "/" << result << "/" << snapshot(s) << "/";
                for (auto& message : logs)
                    std::cout << encode(message) << ";";
                std::cout << '\n';
            }
            return 0;
        }
        std::vector<std::string> logs;
        auto log = [&](const auto& m) { logs.push_back(m); };
        auto a = PrimitiveBaseShapeCore::create_mesh(8, Uuid{}, log);
        if (get_number_of_sides(a.geometry()) != 8)
            throw std::runtime_error("Geometry adapter");
        auto b = a.copy();
        if (a.texture_entry() != b.texture_entry() || a.sculpt_data != b.sculpt_data)
            throw std::runtime_error("Copy lost array aliases");
        auto independent = PrimitiveBaseShapeCore(log);
        if (a.texture_entry() != independent.texture_entry() || a.sculpt_data != independent.sculpt_data)
            throw std::runtime_error("Shared static arrays lost");
        auto data = bytes("1234");
        a.texture_entry(data);
        b = a.copy();
        (*data)[0] = 99;
        if ((*b.texture_entry())[0] != 99)
            throw std::runtime_error("Texture entry aliases copied deeply");
        a.texture_entry({});
        if (a.texture_entry()->size() != 1 || (*a.texture_entry())[0] != 0)
            throw std::runtime_error("Null texture assignment");
        seed(a);
        b = a.copy();
        if (a.reflection_probe != b.reflection_probe || a.render_materials != b.render_materials)
            throw std::runtime_error("Copy lost SDK reference aliases");
        b.render_materials->entries->at(0).te_index = 27;
        if (a.render_materials->entries->at(0).te_index != 27)
            throw std::runtime_error("Render entry alias");
        bool rejected = false;
        try {
            PrimitiveBaseShapeCore incomplete({});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Missing logger silently omitted");

        std::vector<int> counts;
        PrimitiveBaseShape stateful([n = 0, &counts](const auto&) mutable { counts.push_back(++n); });
        auto logger_copy = stateful.copy();
        stateful.profile_curve(240);
        logger_copy.profile_curve(240);
        if (counts != std::vector<int>{1, 2})
            throw std::runtime_error("Shape copy cloned logger callback state");
        std::vector<std::string> trace;
        auto reader = std::make_shared<TraceMediaReader>([&](const auto& e) { trace.push_back(e); });
        ShapeMediaXmlBackend xml;
        xml.media_from_osd = shape_media_entry_from_invariant_osd;
        xml.create_reader = [&](const auto&) {
            trace.push_back("create");
            return reader;
        };
        xml.deserialize_llsd = [&](auto) {
            trace.push_back("parse");
            opensim::network::Value v;
            v.kind = opensim::network::Value::Kind::Array;
            v.array.emplace_back();
            auto map = opensim::network::Value::object({});
            map.set("home_url", opensim::network::Value("x"));
            v.array.push_back(map);
            return v;
        };
        auto list = std::make_shared<ShapeMediaList>([&](const auto&) { trace.push_back("log"); });
        list->read_xml(u"source", xml);
        if (trace != std::vector<std::string>{"create", "dtd", "move", "attr", "start", "inner", "parse",
                                              "dispose"} ||
            list->size() != 2 || reader->disposals != 1)
            throw std::runtime_error("Media read/disposal sequence");
        trace.clear();
        reader->fault = "move";
        list->read_xml(u"source", xml);
        if (trace != std::vector<std::string>{"create", "dtd", "move", "dispose", "log"} ||
            list->size() != 2 || reader->disposals != 2)
            throw std::runtime_error("Media fault disposal/log ordering");
        trace.clear();
        reader->fault = "dispose";
        list->read_xml(u"source", xml);
        if (trace.back() != "log" || list->size() != 4 || reader->disposals != 3)
            throw std::runtime_error("Dispose failure lost source partial state");
        trace.clear();
        reader->fault = "";
        reader->empty = true;
        list->read_xml(*reader, {});
        if (trace != std::vector<std::string>{"empty"} || reader->disposals != 3)
            throw std::runtime_error("Empty outer reader advanced/disposed");
        reader->empty = false;
        reader->fault = "inner";
        trace.clear();
        bool escaped = false;
        try {
            list->read_xml(*reader, xml);
        } catch (const PrimitiveShapeError&) {
            escaped = true;
        }
        if (!escaped || trace != std::vector<std::string>{"empty", "inner"} || reader->disposals != 3)
            throw std::runtime_error("Outer ReadInnerXml fault was swallowed/disposed");
        escaped = false;
        try {
            list->read_xml(u"source", {});
        } catch (const std::invalid_argument&) {
            escaped = true;
        }
        if (!escaped)
            throw std::runtime_error("Missing XML providers silently accepted");
        std::u16string retained;
        TraceMediaWriter writer([&](auto value) { retained = std::move(value); });
        list->write_xml(writer, format_invariant_shape_media_integer);
        if (retained != list->to_xml(format_invariant_shape_media_integer) || list->get_schema() != nullptr)
            throw std::runtime_error("Media writer/schema ownership");
        a.media = list;
        b = a.copy();
        b.media->add(nullptr);
        if (a.media != b.media || a.media->size() != 5)
            throw std::runtime_error("Shape clone lost MediaList reference");
        a.get_mesh();
        std::cout << "PASS: PrimitiveBaseShape methods, aliases, geometry and XML-provider lifecycle\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
