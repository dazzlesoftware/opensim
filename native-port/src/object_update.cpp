// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/scene/object_update.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace opensim::scene {
namespace {
std::vector<std::uint8_t> without_light_params(const std::vector<std::uint8_t>& params) {
    constexpr std::uint16_t light_type = 0x20;
    constexpr std::size_t header_size = 6;
    std::vector<std::uint8_t> filtered{0};
    std::size_t offset = 1;
    for (unsigned index = 0; index < params.front(); ++index) {
        if (params.size() - offset < header_size)
            return params;
        const auto type = params[offset] | (std::uint16_t{params[offset + 1]} << 8);
        std::uint32_t length = 0;
        for (unsigned byte = 0; byte < 4; ++byte)
            length |= std::uint32_t{params[offset + 2 + byte]} << (8 * byte);
        if (length > params.size() - offset - header_size)
            return params;
        const auto end = offset + header_size + length;
        if (type != light_type) {
            filtered.insert(filtered.end(), params.begin() + offset, params.begin() + end);
            ++filtered.front();
        }
        offset = end;
    }
    if (offset != params.size())
        return params;
    return filtered;
}
std::uint8_t attachment_state(const SceneObject& object) {
    const auto point = object.shape.state;
    return object.is_attachment ? static_cast<std::uint8_t>((point >> 4) | (point << 4)) : 0;
}
void quantized(std::vector<std::uint8_t>& bytes, float value) {
    const auto encoded = value >= 1.f ? std::uint16_t{65535}
                         : value <= -1.f || std::isnan(value)
                             ? std::uint16_t{0}
                             : static_cast<std::uint16_t>(static_cast<double>(value) * 32767.5 + 32767.0);
    bytes.push_back(static_cast<std::uint8_t>(encoded));
    bytes.push_back(static_cast<std::uint8_t>(encoded >> 8));
}
void real(std::vector<std::uint8_t>& b, float f) {
    auto u = std::bit_cast<std::uint32_t>(f);
    for (int i = 0; i < 4; i++)
        b.push_back(static_cast<std::uint8_t>(u >> (8 * i)));
}
void vector(std::vector<std::uint8_t>& b, framework::Vector3 v) {
    if (v == framework::Vector3{})
        b.insert(b.end(), 12, 0);
    else {
        real(b, v.x);
        real(b, v.y);
        real(b, v.z);
    }
}
void quaternion(std::vector<std::uint8_t>& b, framework::Quaternion q) {
    float norm = framework::sdk_quaternion_length_squared(q);
    if (norm > 1.e-6f) {
        float m = (q.w < 0 ? -1.f : 1.f) / std::sqrt(norm);
        real(b, m * q.x);
        real(b, m * q.y);
        real(b, m * q.z);
    } else
        b.insert(b.end(), 12, 0);
}
}
network::lludp::Packet object_terse_update(const SceneObject& object, std::uint64_t region_handle) {
    auto packet = network::lludp::make_packet("ImprovedTerseObjectUpdate");
    // The simulator sets zero coding on its terse updates despite the template.
    packet.zerocoded = true;
    packet.block("RegionData")["RegionHandle"] = region_handle;
    packet.block("RegionData")["TimeDilation"] = std::uint64_t(65535);
    std::vector<std::uint8_t> data;
    data.reserve(44);
    for (int i = 0; i < 4; ++i)
        data.push_back(static_cast<std::uint8_t>(object.local_id >> (8 * i)));
    data.push_back(attachment_state(object));
    data.push_back(0);
    vector(data, object.position);
    const auto pack_vector = [&](framework::Vector3 value, float range) {
        const float scale = 1.f / std::max({range, std::abs(value.x), std::abs(value.y), std::abs(value.z)});
        for (const float axis : {value.x, value.y, value.z})
            quantized(data, axis * scale);
    };
    pack_vector(object.velocity, 128.f);
    pack_vector(object.acceleration, 64.f);
    const auto& rotation = object.rotation;
    for (const float axis : {rotation.x, rotation.y, rotation.z, rotation.w})
        quantized(data, axis);
    pack_vector(object.angular_velocity, 64.f);
    auto block = network::lludp::make_block(packet.spec->blocks.at(1));
    block["Data"] = std::move(data);
    packet.blocks["ObjectData"].push_back(std::move(block));
    return packet;
}
network::lludp::Packet object_update(const SceneObject& o, std::uint64_t region, float dilation,
                                     const ObjectUpdateOptions& options) {
    if (o.shape.pcode != 9 || (o.is_attachment && o.parent_id == 0))
        throw std::invalid_argument("object_update requires a prim with a valid attachment parent");
    using namespace network;
    auto p = lludp::make_packet("ObjectUpdate");
    auto& r = p.block("RegionData");
    r["RegionHandle"] = region;
    r["TimeDilation"] = std::uint64_t(dilation <= 0 || std::isnan(dilation) ? 0
                                      : dilation >= 1 ? 65535
                                                      : static_cast<std::uint16_t>(dilation * 65535.f));
    auto b = lludp::make_block(p.spec->blocks.at(1));
    b["ID"] = std::uint64_t(o.local_id);
    b["State"] = o.is_attachment ? std::uint64_t(attachment_state(o)) : std::uint64_t(o.shape.state);
    b["FullID"] = o.id;
    b["CRC"] = std::uint64_t(options.pseudo_crc);
    b["PCode"] = std::uint64_t(o.shape.pcode);
    b["Material"] = std::uint64_t(options.material);
    b["ClickAction"] = std::uint64_t(options.click_action);
    b["Scale"] = o.scale == framework::Vector3{} ? std::vector<double>{0, 0, 0}
                                                 : std::vector<double>{o.scale.x, o.scale.y, o.scale.z};
    b["ParentID"] = std::uint64_t(o.parent_id);
    std::uint32_t flags = options.generated_client_flags.value_or(o.flags) & ~2u;
    if (options.recipient == o.owner && options.create_selected)
        flags |= 2u;
    b["UpdateFlags"] = std::uint64_t(flags);
    std::vector<std::uint8_t> data;
    data.reserve(60);
    vector(data, o.position);
    vector(data, o.velocity);
    vector(data, o.acceleration);
    quaternion(data, o.rotation);
    vector(data, o.angular_velocity);
    b["ObjectData"] = std::move(data);
    const auto& s = o.shape;
    b["PathCurve"] = std::uint64_t(s.path_curve);
    b["ProfileCurve"] = std::uint64_t(s.profile_curve);
    b["PathBegin"] = std::uint64_t(s.path_begin);
    b["PathEnd"] = std::uint64_t(s.path_end);
    b["PathScaleX"] = std::uint64_t(s.path_scale_x);
    b["PathScaleY"] = std::uint64_t(s.path_scale_y);
    b["PathShearX"] = std::uint64_t(s.path_shear_x);
    b["PathShearY"] = std::uint64_t(s.path_shear_y);
    b["PathTwist"] = std::int64_t(s.path_twist);
    b["PathTwistBegin"] = std::int64_t(s.path_twist_begin);
    b["PathRadiusOffset"] = std::int64_t(s.path_radius_offset);
    b["PathTaperX"] = std::int64_t(s.path_taper_x);
    b["PathTaperY"] = std::int64_t(s.path_taper_y);
    b["PathRevolutions"] = std::uint64_t(s.path_revolutions);
    b["PathSkew"] = std::int64_t(s.path_skew);
    b["ProfileBegin"] = std::uint64_t(s.profile_begin);
    b["ProfileEnd"] = std::uint64_t(s.profile_end);
    b["ProfileHollow"] = std::uint64_t(s.profile_hollow);
    b["TextureEntry"] = o.texture_entry;
    b["TextureAnim"] = o.texture_animation;
    if (o.extra_params.size() > 1) {
        constexpr std::uint8_t left_hand = 5;
        constexpr std::uint8_t right_hand = 6;
        const bool suppress_light = options.disable_facelights && o.is_attachment &&
                                    o.shape.state != left_hand && o.shape.state != right_hand;
        auto extra_params = suppress_light ? without_light_params(o.extra_params) : o.extra_params;
        if (extra_params.size() > 1)
            b["ExtraParams"] = std::move(extra_params);
    }
    const auto short_text = [](const std::string& value) {
        auto length = std::min<std::size_t>(254, value.size());
        while (length && length < value.size() &&
               (static_cast<unsigned char>(value[length]) & 0xc0u) == 0x80u)
            --length;
        std::vector<std::uint8_t> bytes(value.begin(), value.begin() + length);
        if (length)
            bytes.push_back(0);
        return bytes;
    };
    if (!o.media_url.empty())
        b["MediaURL"] = short_text(o.media_url);
    if (!o.looped_sound.is_zero() || o.sound_flags) {
        b["Sound"] = o.looped_sound;
        b["OwnerID"] = o.owner;
        b["Gain"] = static_cast<double>(o.looped_sound_gain);
        b["Flags"] = std::uint64_t{o.sound_flags};
        b["Radius"] = static_cast<double>(o.sound_radius);
    }
    if (!o.floating_text.empty()) {
        b["Text"] = short_text(o.floating_text);
        b["TextColor"] = std::vector<std::uint8_t>{
            static_cast<std::uint8_t>(o.text_argb >> 16), static_cast<std::uint8_t>(o.text_argb >> 8),
            static_cast<std::uint8_t>(o.text_argb), static_cast<std::uint8_t>(255u - (o.text_argb >> 24))};
    }
    if (o.is_attachment) {
        const auto item = o.from_inventory_item.is_zero() ? o.id : o.from_inventory_item;
        const auto name_value = "AttachItemID STRING RW SV " + item.str();
        b["NameValue"] =
            std::vector<std::uint8_t>(name_value.c_str(), name_value.c_str() + name_value.size() + 1);
    }
    p.blocks["ObjectData"].push_back(std::move(b));
    return p;
}
}
