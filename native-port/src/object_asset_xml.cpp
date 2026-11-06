// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/network/osd_value.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <pugixml.hpp>

namespace opensim::scene {
namespace {
void start(framework::NativeXmlTextWriter &writer, const char16_t *name) {
    writer.write_start_element({}, std::u16string(name), {});
}
void element(framework::NativeXmlTextWriter &writer, const char16_t *name, const std::string &value) {
    start(writer, name);
    writer.write_string(framework::utf16(value));
    writer.write_end_element();
}
void binary_element(framework::NativeXmlTextWriter &writer, const char16_t *name,
                    std::span<const std::uint8_t> data) {
    start(writer, name);
    writer.write_base64(data, 0, static_cast<std::int32_t>(data.size()));
    writer.write_end_element();
}
void read_binary_element(pugi::xml_node field, std::vector<std::uint8_t> &bytes) {
    if (!field)
        return;
    network::Value value;
    value.kind = network::Value::Kind::Binary;
    value.text = field.child_value();
    bytes = *network::osd_binary(value);
}
void write_object_asset_shape(framework::NativeXmlTextWriter &writer, const ObjectAssetPart &snapshot) {
    const auto number = [&](const char16_t *name, int value) {
        element(writer, name, std::to_string(value));
    };
    const auto &part = snapshot.object;
    const auto &shape = part.shape;
    writer.write_start_element({}, std::u16string(u"Shape"), {});
    number(u"ProfileCurve", shape.profile_curve);
    binary_element(writer, u"TextureEntry", part.texture_entry);
    binary_element(writer, u"ExtraParams", part.extra_params);
    number(u"PathBegin", shape.path_begin);
    number(u"PathCurve", shape.path_curve);
    number(u"PathEnd", shape.path_end);
    number(u"PathRadiusOffset", shape.path_radius_offset);
    number(u"PathRevolutions", shape.path_revolutions);
    number(u"PathScaleX", shape.path_scale_x);
    number(u"PathScaleY", shape.path_scale_y);
    number(u"PathShearX", shape.path_shear_x);
    number(u"PathShearY", shape.path_shear_y);
    number(u"PathSkew", shape.path_skew);
    number(u"PathTaperX", shape.path_taper_x);
    number(u"PathTaperY", shape.path_taper_y);
    number(u"PathTwist", shape.path_twist);
    number(u"PathTwistBegin", shape.path_twist_begin);
    number(u"PCode", shape.pcode);
    number(u"ProfileBegin", shape.profile_begin);
    number(u"ProfileEnd", shape.profile_end);
    number(u"ProfileHollow", shape.profile_hollow);
    number(u"State", shape.state);
    number(u"LastAttachPoint", snapshot.last_attachment_point);
    number(u"ProfileShape", shape.profile_curve & 0x0f);
    number(u"HollowShape", shape.profile_curve & 0xf0);
    if (!part.face_media_xml.empty())
        element(writer, u"Media", part.face_media_xml);
    if (!part.material_overrides.empty())
        binary_element(writer, u"MatOvrd", part.material_overrides);
    writer.write_end_element();
}
void write_part(framework::NativeXmlTextWriter &writer, const ObjectAssetPart &snapshot,
                const SceneObject &root, std::size_t link_number) {
    const auto &part = snapshot.object;
    const auto number = [&](const char16_t *name, auto value) {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{})
            throw std::runtime_error("Cannot format object asset number");
        element(writer, name, std::string(buffer, result.ptr));
    };
    const auto uuid = [&](const char16_t *name, framework::Uuid value) {
        start(writer, name);
        element(writer, u"UUID", value.str());
        writer.write_end_element();
    };
    const auto vector = [&](const char16_t *name, framework::Vector3 value) {
        start(writer, name);
        number(u"X", value.x);
        number(u"Y", value.y);
        number(u"Z", value.z);
        writer.write_end_element();
    };
    const auto quaternion = [&](const char16_t* name, framework::Quaternion value) {
        start(writer, name);
        number(u"X", value.x);
        number(u"Y", value.y);
        number(u"Z", value.z);
        number(u"W", value.w);
        writer.write_end_element();
    };
    start(writer, u"SceneObjectPart");
    uuid(u"CreatorID", part.creator);
    number(u"InventorySerial", snapshot.inventory_serial);
    if (!snapshot.inventory.empty()) {
        start(writer, u"TaskInventory");
        for (const auto &item : snapshot.inventory) {
            start(writer, u"TaskInventoryItem");
            uuid(u"AssetID", item.asset_id);
            number(u"BasePermissions", item.base_permissions);
            number(u"CreationDate", item.creation_date);
            uuid(u"CreatorID", item.creator_id);
            if (item.creator_data && !item.creator_data->empty())
                element(writer, u"CreatorData", *item.creator_data);
            element(writer, u"Description", item.description.value_or(""));
            number(u"EveryonePermissions", item.everyone_permissions);
            number(u"Flags", item.flags);
            uuid(u"GroupID", item.group_id);
            number(u"GroupPermissions", item.group_permissions);
            number(u"InvType", item.inventory_type);
            uuid(u"ItemID", item.item_id);
            uuid(u"OldItemID", item.old_item_id);
            uuid(u"LastOwnerID", item.last_owner_id);
            element(writer, u"Name", item.name.value_or(""));
            number(u"NextPermissions", item.next_permissions);
            uuid(u"OwnerID", item.owner_id);
            number(u"CurrentPermissions", item.current_permissions);
            uuid(u"ParentID", item.parent_id);
            uuid(u"ParentPartID", item.parent_part_id);
            uuid(u"PermsGranter", item.permissions_granter);
            number(u"PermsMask", item.permissions_mask);
            number(u"Type", item.type);
            element(writer, u"OwnerChanged", item.owner_changed ? "true" : "false");
            writer.write_end_element();
        }
        writer.write_end_element();
    }
    uuid(u"UUID", part.id);
    number(u"LocalId", part.local_id);
    element(writer, u"Name", part.name);
    number(u"Material", part.material);
    element(writer, u"PassTouches", part.pass_touches ? "true" : "false");
    vector(u"GroupPosition", root.position);
    vector(u"OffsetPosition", part.id == root.id ? framework::Vector3{} : part.position);
    quaternion(u"RotationOffset", part.rotation);
    vector(u"Velocity", part.velocity);
    vector(u"AngularVelocity", part.angular_velocity);
    vector(u"Acceleration", part.acceleration);
    element(writer, u"Description", part.description);
    start(writer, u"Color");
    number(u"R", (part.text_argb >> 16) & 255u);
    number(u"G", (part.text_argb >> 8) & 255u);
    number(u"B", part.text_argb & 255u);
    number(u"A", part.text_argb >> 24);
    writer.write_end_element();
    element(writer, u"Text", part.floating_text);
    element(writer, u"SitName", part.sit_name);
    element(writer, u"TouchName", part.touch_name);
    number(u"LinkNum", link_number);
    number(u"ClickAction", part.click_action);
    write_object_asset_shape(writer, snapshot);
    vector(u"Scale", part.scale);
    quaternion(u"SitTargetOrientation", part.sit_target_orientation);
    vector(u"SitTargetPosition", part.sit_target_position);
    vector(u"SitTargetPositionLL", part.sit_target_position);
    if (std::abs(part.sit_active_range) > 1e-5f)
        number(u"SitActRange", part.sit_active_range);
    quaternion(u"SitTargetOrientationLL", part.sit_target_orientation);
    if (part.stand_target != framework::Vector3{})
        vector(u"StandTarget", part.stand_target);
    vector(u"CameraEyeOffset", part.camera_eye_offset);
    vector(u"CameraAtOffset", part.camera_at_offset);
    number(u"ParentID", part.parent_id);
    number(u"CreationDate", part.creation_date);
    number(u"Category", part.category);
    number(u"SalePrice", part.sale_price);
    number(u"ObjectSaleType", part.sale_type);
    uuid(u"GroupID", part.group);
    uuid(u"OwnerID", part.owner);
    uuid(u"LastOwnerID", part.last_owner);
    uuid(u"RezzerID", part.rezzer);
    number(u"BaseMask", part.base_mask);
    number(u"OwnerMask", part.owner_mask);
    number(u"GroupMask", part.group_mask);
    number(u"EveryoneMask", part.everyone_mask);
    number(u"NextOwnerMask", part.next_mask);
    element(writer, u"AllowedDrop", (part.flags & 0x10000u) ? "true" : "false");
    number(u"Flags", part.flags & ~0x10000u);
    uuid(u"CollisionSound", part.collision_sound);
    number(u"CollisionSoundVolume", part.collision_sound_volume);
    if (!part.media_url.empty())
        element(writer, u"MediaUrl", part.media_url);
    vector(u"AttachedPos", snapshot.attachment_position);
    constexpr std::array pay_names{u"PayPrice0", u"PayPrice1", u"PayPrice2", u"PayPrice3", u"PayPrice4"};
    for (std::size_t i = 0; i < pay_names.size(); ++i)
        number(pay_names[i], part.pay_prices[i]);
    number(u"Buoyancy", root.buoyancy);
    vector(u"Force", root.force);
    vector(u"Torque", root.torque);
    number(u"PhysicsShapeType", part.physics.shape);
    if (part.physics.density != 1000.f)
        number(u"Density", part.physics.density);
    if (part.physics.friction != .6f)
        number(u"Friction", part.physics.friction);
    if (part.physics.restitution != .5f)
        number(u"Bounce", part.physics.restitution);
    if (part.physics.gravity != 1.f)
        number(u"GravityModifier", part.physics.gravity);
    uuid(u"SoundID", part.looped_sound);
    number(u"SoundGain", part.looped_sound_gain);
    number(u"SoundFlags", part.sound_flags);
    number(u"SoundRadius", part.sound_radius);
    if (!part.texture_animation.empty())
        binary_element(writer, u"TextureAnimation", part.texture_animation);
    if (!part.dynamic_attributes_xml.empty()) {
        pugi::xml_document document;
        if (document.load_string(part.dynamic_attributes_xml.c_str()) && document.child("llsd")) {
            std::ostringstream xml;
            document.child("llsd").print(xml, "", pugi::format_raw);
            start(writer, u"DynAttrs");
            writer.write_raw(framework::utf16(xml.str()));
            writer.write_end_element();
        }
    }
    element(writer, u"SoundQueueing", part.sound_queueing ? "true" : "false");
    if (!part.object_animations.empty())
        binary_element(writer, u"SOPAnims", part.object_animations);
    writer.write_end_element();
}
}
void write_object_asset_xml(framework::NativeXmlTextWriter &writer, const ObjectAssetPart &root,
                            std::span<const ObjectAssetPart> children) {
    start(writer, u"SceneObjectGroup");
    start(writer, u"RootPart");
    write_part(writer, root, root.object, children.empty() ? 0 : 1);
    writer.write_end_element();
    start(writer, u"OtherParts");
    std::size_t link_number = 2;
    for (const auto &child : children) {
        start(writer, u"Part");
        write_part(writer, child, root.object, link_number++);
        writer.write_end_element();
    }
    writer.write_end_element();
    // The managed OriginalXml reader consumes linkset data before the rez string.
    if (!root.object.linkset_data.empty()) {
        start(writer, u"lnkstdt");
        writer.write_base64(root.object.linkset_data, 0,
                            static_cast<std::int32_t>(root.object.linkset_data.size()));
        writer.write_end_element();
    }
    if (root.object.rez_start_string)
        element(writer, u"StartStr", *root.object.rez_start_string);
    writer.write_end_element();
}
void write_crossing_object_xml(framework::NativeXmlTextWriter& writer, const ObjectAssetPart& root,
                               std::span<const ObjectAssetPart> children) {
    start(writer, u"SceneObjectGroup");
    write_part(writer, root, root.object, children.empty() ? 0 : 1);
    start(writer, u"OtherParts");
    std::size_t link_number = 2;
    for (const auto& child : children)
        write_part(writer, child, root.object, link_number++);
    writer.write_end_element();
    if (root.object.rez_start_string)
        element(writer, u"StartStr", *root.object.rez_start_string);
    if (!root.object.linkset_data.empty())
        binary_element(writer, u"lnkstdt", root.object.linkset_data);
    writer.write_end_element();
}
void write_coalesced_object_asset_xml(framework::NativeXmlTextWriter& writer,
                                      std::span<const ObjectAssetLinkset> linksets) {
    const auto limit = std::numeric_limits<float>::max();
    framework::Vector3 minimum{limit, limit, limit}, maximum{-limit, -limit, -limit};
    for (const auto& linkset : linksets) {
        const auto& root = linkset.parts.front().object;
        for (const auto& snapshot : linkset.parts) {
            const auto& part = snapshot.object;
            auto center = root.position;
            auto rotation = root.rotation;
            if (part.id != root.id) {
                const auto offset = framework::sdk_rotate_vector(part.position, root.rotation);
                center = {center.x + offset.x, center.y + offset.y, center.z + offset.z};
                rotation = framework::sdk_multiply_quaternion(root.rotation, part.rotation);
            }
            const auto x = framework::sdk_rotate_vector({part.scale.x * .5f, 0, 0}, rotation);
            const auto y = framework::sdk_rotate_vector({0, part.scale.y * .5f, 0}, rotation);
            const auto z = framework::sdk_rotate_vector({0, 0, part.scale.z * .5f}, rotation);
            const framework::Vector3 half_extent{std::abs(x.x) + std::abs(y.x) + std::abs(z.x),
                                                 std::abs(x.y) + std::abs(y.y) + std::abs(z.y),
                                                 std::abs(x.z) + std::abs(y.z) + std::abs(z.z)};
            minimum.x = std::min(minimum.x, center.x - half_extent.x);
            minimum.y = std::min(minimum.y, center.y - half_extent.y);
            minimum.z = std::min(minimum.z, center.z - half_extent.z);
            maximum.x = std::max(maximum.x, center.x + half_extent.x);
            maximum.y = std::max(maximum.y, center.y + half_extent.y);
            maximum.z = std::max(maximum.z, center.z + half_extent.z);
        }
    }
    const auto attribute = [&](const char16_t* name, float value) {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{})
            throw std::runtime_error("Cannot format coalesced object asset bounds");
        writer.write_start_attribute({}, std::u16string(name), {});
        writer.write_string(framework::utf16(std::string(buffer, result.ptr)));
        writer.write_end_attribute();
    };
    start(writer, u"CoalescedObject");
    attribute(u"x", maximum.x - minimum.x);
    attribute(u"y", maximum.y - minimum.y);
    attribute(u"z", maximum.z - minimum.z);
    for (const auto& linkset : linksets) {
        const auto& root = linkset.parts.front();
        start(writer, u"SceneObjectGroup");
        attribute(u"offsetx", root.object.position.x - minimum.x);
        attribute(u"offsety", root.object.position.y - minimum.y);
        attribute(u"offsetz", root.object.position.z - minimum.z);
        write_object_asset_xml(writer, root, std::span(linkset.parts).subspan(1));
        writer.write_end_element();
    }
    writer.write_end_element();
}
namespace {
template <class T> void read_number(pugi::xml_node node, const char *name, T &value) {
    const std::string_view text = node.child_value(name);
    if (text.empty())
        return;
    T parsed{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error == std::errc{} && end == text.data() + text.size())
        value = parsed;
}
void read_uuid(pugi::xml_node node, const char *name, framework::Uuid &value) {
    const auto field = node.child(name);
    if (!field)
        return;
    auto content = field.child("UUID");
    if (!content)
        content = field.child("Guid");
    framework::try_parse_sdk_uuid(content ? content.child_value() : field.child_value(), value);
}
void read_prim_flags(pugi::xml_node node, std::uint32_t &flags) {
    constexpr std::array<std::string_view, 32> names{"Physics",
                                                     "CreateSelected",
                                                     "ObjectModify",
                                                     "ObjectCopy",
                                                     "ObjectAnyOwner",
                                                     "ObjectYouOwner",
                                                     "Scripted",
                                                     "Touch",
                                                     "ObjectMove",
                                                     "Money",
                                                     "Phantom",
                                                     "InventoryEmpty",
                                                     "JointHinge",
                                                     "JointP2P",
                                                     "JointLP2P",
                                                     "JointWheel",
                                                     "AllowInventoryDrop",
                                                     "ObjectTransfer",
                                                     "ObjectGroupOwned",
                                                     "ObjectYouOfficer",
                                                     "CameraDecoupled",
                                                     "AnimSource",
                                                     "CameraSource",
                                                     "CastShadows",
                                                     "DieAtEdge",
                                                     "ReturnAtEdge",
                                                     "Sandbox",
                                                     "Flying",
                                                     "ObjectOwnerModify",
                                                     "TemporaryOnRez",
                                                     "Temporary",
                                                     "ZlibCompressed"};
    std::string_view remaining = node.child_value("Flags");
    if (remaining.empty())
        return;
    std::uint32_t result = 0;
    const auto [end, error] = std::from_chars(remaining.data(), remaining.data() + remaining.size(), result);
    if (error == std::errc{} && end == remaining.data() + remaining.size()) {
        flags = result;
        return;
    }
    while (!remaining.empty()) {
        const auto begin = remaining.find_first_not_of(", \t\r\n");
        if (begin == std::string_view::npos)
            break;
        remaining.remove_prefix(begin);
        const auto length = remaining.find_first_of(", \t\r\n");
        const auto token = remaining.substr(0, length);
        if (token != "None") {
            const auto found = std::find(names.begin(), names.end(), token);
            if (found == names.end())
                return;
            result |= std::uint32_t{1} << std::distance(names.begin(), found);
        }
        remaining.remove_prefix(token.size());
    }
    flags = result;
}
template <class T> void read_xyz(pugi::xml_node node, T &value) {
    read_number(node, node.child("X") ? "X" : "x", value.x);
    read_number(node, node.child("Y") ? "Y" : "y", value.y);
    read_number(node, node.child("Z") ? "Z" : "z", value.z);
}
void read_shape(pugi::xml_node node, SceneObject &part) {
    auto &shape = part.shape;
    read_number(node, "ProfileCurve", shape.profile_curve);
    read_number(node, "PathBegin", shape.path_begin);
    read_number(node, "PathCurve", shape.path_curve);
    read_number(node, "PathEnd", shape.path_end);
    read_number(node, "PathRadiusOffset", shape.path_radius_offset);
    read_number(node, "PathRevolutions", shape.path_revolutions);
    read_number(node, "PathScaleX", shape.path_scale_x);
    read_number(node, "PathScaleY", shape.path_scale_y);
    read_number(node, "PathShearX", shape.path_shear_x);
    read_number(node, "PathShearY", shape.path_shear_y);
    read_number(node, "PathSkew", shape.path_skew);
    read_number(node, "PathTaperX", shape.path_taper_x);
    read_number(node, "PathTaperY", shape.path_taper_y);
    read_number(node, "PathTwist", shape.path_twist);
    read_number(node, "PathTwistBegin", shape.path_twist_begin);
    read_number(node, "PCode", shape.pcode);
    read_number(node, "ProfileBegin", shape.profile_begin);
    read_number(node, "ProfileEnd", shape.profile_end);
    read_number(node, "ProfileHollow", shape.profile_hollow);
    read_number(node, "State", shape.state);
    read_binary_element(node.child("TextureEntry"), part.texture_entry);
    read_binary_element(node.child("ExtraParams"), part.extra_params);
    read_binary_element(node.child("MatOvrd"), part.material_overrides);
    part.face_media_xml = node.child_value("Media");
}
framework::TaskInventoryItem read_task_item(pugi::xml_node node) {
    framework::TaskInventoryItem item;
    read_uuid(node, "AssetID", item.asset_id);
    read_uuid(node, "CreatorID", item.creator_id);
    read_uuid(node, "GroupID", item.group_id);
    read_uuid(node, "ItemID", item.item_id);
    read_uuid(node, "OldItemID", item.old_item_id);
    read_uuid(node, "LastOwnerID", item.last_owner_id);
    read_uuid(node, "OwnerID", item.owner_id);
    read_uuid(node, "ParentID", item.parent_id);
    read_uuid(node, "ParentPartID", item.parent_part_id);
    read_uuid(node, "PermsGranter", item.permissions_granter);
    read_number(node, "BasePermissions", item.base_permissions);
    read_number(node, "CreationDate", item.creation_date);
    read_number(node, "EveryonePermissions", item.everyone_permissions);
    read_number(node, "Flags", item.flags);
    read_number(node, "GroupPermissions", item.group_permissions);
    read_number(node, "InvType", item.inventory_type);
    read_number(node, "NextPermissions", item.next_permissions);
    read_number(node, "CurrentPermissions", item.current_permissions);
    read_number(node, "PermsMask", item.permissions_mask);
    read_number(node, "Type", item.type);
    item.creator_data = node.child_value("CreatorData");
    item.description = node.child_value("Description");
    item.name = node.child_value("Name");
    item.owner_changed = std::string_view(node.child_value("OwnerChanged")) == "true";
    return item;
}
ObjectAssetPart read_part(pugi::xml_node node, bool root) {
    ObjectAssetPart result;
    auto &part = result.object;
    read_uuid(node, "UUID", part.id);
    read_uuid(node, "CreatorID", part.creator);
    read_uuid(node, "OwnerID", part.owner);
    read_uuid(node, "GroupID", part.group);
    read_uuid(node, "LastOwnerID", part.last_owner);
    read_uuid(node, "RezzerID", part.rezzer);
    read_uuid(node, "CollisionSound", part.collision_sound);
    read_uuid(node, "SoundID", part.looped_sound);
    if (const auto name = node.child("Name"))
        part.name = name.child_value();
    part.description = node.child_value("Description");
    part.touch_name = node.child_value("TouchName");
    part.sit_name = node.child_value("SitName");
    part.floating_text = node.child_value("Text");
    part.media_url = node.child_value("MediaUrl");
    read_number(node, "LocalId", part.local_id);
    read_number(node, "ParentID", part.parent_id);
    read_number(node, "LinkNum", part.link_order);
    read_number(node, "Material", part.material);
    read_number(node, "CreationDate", part.creation_date);
    read_number(node, "Category", part.category);
    read_number(node, "SalePrice", part.sale_price);
    read_number(node, "ObjectSaleType", part.sale_type);
    read_number(node, "ClickAction", part.click_action);
    part.pass_touches = node.child("PassTouches").text().as_bool();
    read_number(node, "BaseMask", part.base_mask);
    read_number(node, "OwnerMask", part.owner_mask);
    read_number(node, "GroupMask", part.group_mask);
    read_number(node, "EveryoneMask", part.everyone_mask);
    read_number(node, "NextOwnerMask", part.next_mask);
    read_prim_flags(node, part.flags);
    if (const auto allowed_drop = node.child("AllowedDrop"))
        part.flags = allowed_drop.text().as_bool() ? part.flags | 0x10000u : part.flags & ~0x10000u;
    read_number(node, "CollisionSoundVolume", part.collision_sound_volume);
    read_number(node, "SoundGain", part.looped_sound_gain);
    read_binary_element(node.child("TextureAnimation"), part.texture_animation);
    read_number(node, "SoundFlags", part.sound_flags);
    read_number(node, "SoundRadius", part.sound_radius);
    part.sound_queueing = std::string_view(node.child_value("SoundQueueing")) == "true";
    constexpr std::array pay_names{"PayPrice0", "PayPrice1", "PayPrice2", "PayPrice3", "PayPrice4"};
    for (std::size_t i = 0; i < pay_names.size(); ++i)
        read_number(node, pay_names[i], part.pay_prices[i]);
    read_number(node, "PhysicsShapeType", part.physics.shape);
    read_number(node, "Density", part.physics.density);
    read_number(node, "Friction", part.physics.friction);
    read_number(node, "Bounce", part.physics.restitution);
    read_number(node, "GravityModifier", part.physics.gravity);
    read_number(node, "Buoyancy", part.buoyancy);
    read_xyz(node.child("Force"), part.force);
    read_xyz(node.child("Torque"), part.torque);
    const auto color = node.child("Color");
    constexpr std::array channels{"A", "R", "G", "B"};
    for (const auto channel : channels) {
        float component = 0;
        read_number(color, channel, component);
        part.text_argb =
            (part.text_argb << 8) |
            (component >= 0.f && component <= 255.f ? static_cast<std::uint32_t>(component) : 0u);
    }
    read_number(node, "InventorySerial", result.inventory_serial);
    read_xyz(node.child(root ? "GroupPosition" : "OffsetPosition"), part.position);
    read_xyz(node.child("AttachedPos"), result.attachment_position);
    read_number(node.child("Shape"), "LastAttachPoint", result.last_attachment_point);
    part.last_attachment_point = result.last_attachment_point;
    part.saved_attachment_position = result.attachment_position;
    const auto rotation = node.child("RotationOffset");
    read_xyz(rotation, part.rotation);
    read_number(rotation, rotation.child("W") ? "W" : "w", part.rotation.w);
    read_xyz(node.child("Scale"), part.scale);
    read_number(node, "SitActRange", part.sit_active_range);
    read_xyz(node.child("StandTarget"), part.stand_target);
    read_xyz(node.child("CameraEyeOffset"), part.camera_eye_offset);
    read_xyz(node.child("CameraAtOffset"), part.camera_at_offset);
    for (const auto field : node.children()) {
        const std::string_view name = field.name();
        if (name == "SitTargetPosition" || name == "SitTargetPositionLL")
            read_xyz(field, part.sit_target_position);
        else if (name == "SitTargetOrientation" || name == "SitTargetOrientationLL") {
            read_xyz(field, part.sit_target_orientation);
            read_number(field, field.child("W") ? "W" : "w", part.sit_target_orientation.w);
        }
    }
    read_xyz(node.child("Velocity"), part.velocity);
    read_xyz(node.child("AngularVelocity"), part.angular_velocity);
    read_xyz(node.child("Acceleration"), part.acceleration);
    read_shape(node.child("Shape"), part);
    if (const auto attrs = node.child("DynAttrs").child("llsd")) {
        std::ostringstream xml;
        attrs.print(xml, "", pugi::format_raw);
        part.dynamic_attributes_xml = xml.str();
    }
    read_binary_element(node.child("SOPAnims"), part.object_animations);
    for (const auto item : node.child("TaskInventory").children("TaskInventoryItem"))
        result.inventory.push_back(read_task_item(item));
    part.inventory_empty = result.inventory.empty();
    return result;
}
bool read_linkset(pugi::xml_node group, ObjectAssetLinkset &linkset) {
    const auto root_wrapper = group.child("RootPart");
    const auto root = root_wrapper ? root_wrapper.child("SceneObjectPart") : group.child("SceneObjectPart");
    if (!root)
        return false;
    auto &parts = linkset.parts;
    parts.push_back(read_part(root, true));
    if (const auto start_string = group.child("StartStr"))
        parts.front().object.rez_start_string = start_string.child_value();
    if (const auto data = group.child("lnkstdt")) {
        network::Value value;
        value.kind = network::Value::Kind::Binary;
        value.text = data.child_value();
        parts.front().object.linkset_data = *network::osd_binary(value);
    }
    for (const auto child : group.child("OtherParts").children(root_wrapper ? "Part" : "SceneObjectPart")) {
        const auto part = root_wrapper ? child.child("SceneObjectPart") : child;
        if (!part)
            return false;
        parts.push_back(read_part(part, false));
        if (!parts.back().object.link_order)
            parts.back().object.link_order = parts.size();
    }
    for (const auto saved : group.child("GroupScriptStates").children("SavedScriptState")) {
        framework::Uuid item_id;
        if (!framework::try_parse_sdk_uuid(saved.attribute("UUID").value(), item_id) || item_id.is_zero())
            continue;
        const auto running = saved.child("ScriptState").child("Running");
        if (!running)
            continue;
        const std::string_view value = running.child_value();
        if (value != "False" && value != "false" && value != "0")
            continue;
        for (auto &part : parts)
            for (auto &item : part.inventory)
                if (item.item_id == item_id && item.inventory_type == 10)
                    item.script_running = false;
    }
    std::stable_sort(parts.begin() + 1, parts.end(), [](const auto &left, const auto &right) {
        return left.object.link_order < right.object.link_order;
    });
    // The world derives link numbers from relative order. Give ties a stable
    // ordering as well, so missing or repeated numbers never collide at rez.
    for (std::size_t i = 1; i < parts.size(); ++i)
        parts[i].object.link_order = i + 1;
    return true;
}
}
std::optional<ObjectAsset> read_object_asset_xml(std::span<const std::uint8_t> xml) {
    pugi::xml_document document;
    if (!document.load_buffer(xml.data(), xml.size()))
        return std::nullopt;
    ObjectAsset asset;
    if (const auto collection = document.child("CoalescedObject")) {
        asset.coalesced = true;
        asset.bounds = {collection.attribute("x").as_float(), collection.attribute("y").as_float(),
                        collection.attribute("z").as_float()};
        for (const auto wrapper : collection.children("SceneObjectGroup")) {
            auto &linkset = asset.linksets.emplace_back();
            linkset.offset = {wrapper.attribute("offsetx").as_float(),
                              wrapper.attribute("offsety").as_float(),
                              wrapper.attribute("offsetz").as_float()};
            const auto inner = wrapper.child("SceneObjectGroup");
            if (!read_linkset(inner ? inner : wrapper, linkset))
                return std::nullopt;
        }
    } else {
        if (!read_linkset(document.child("SceneObjectGroup"), asset.linksets.emplace_back()))
            return std::nullopt;
    }
    if (asset.linksets.empty())
        return std::nullopt;
    return asset;
}
}
