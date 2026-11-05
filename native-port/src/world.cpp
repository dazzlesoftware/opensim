// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/world.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/framework/texture_entry.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include <tuple>
#include <chrono>
#include <bit>
#include <algorithm>
namespace opensim::scene {
SceneObject::SceneObject()
    : rezzed_at(std::chrono::system_clock::now()),
      texture_entry(
          framework::TextureEntry(framework::Uuid("89556747-24cb-43ed-920b-47caed15465f")).bytes()) {
    creation_date = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(rezzed_at.time_since_epoch()).count()));
}
SceneObject SceneObject::primitive(framework::Uuid owner, framework::Vector3 position,
                                   framework::Vector3 scale) {
    SceneObject result;
    result.id = framework::Uuid::random();
    result.owner = result.creator = result.rezzer = result.last_owner = owner;
    result.position = position;
    result.scale = scale;
    return result;
}
World::World(framework::Uuid region, std::uint32_t width, std::uint32_t height, std::uint32_t last_local_id)
    : region_(region), terrain_(width, height), last_local_id_(last_local_id) {
}
std::uint32_t World::allocate_local_id() {
    return last_local_id_.fetch_add(1, std::memory_order_relaxed) + 1u;
}
void World::event_unlocked(WorldEventKind kind, const SceneObject& object, std::int32_t script_changes) {
    events_.push_back({kind, object, ++sequence_, script_changes});
}
std::optional<SceneObject> World::add(SceneObject object, bool attach_to_backup, bool send_update) {
    if (object.id.is_zero())
        return {};
    std::lock_guard lock(mutex_);
    if (objects_.contains(object.id))
        return {};
    if (!object.is_attachment && object.parent_id != 0) {
        const auto parent = by_local_.find(object.parent_id);
        if (object.shape.pcode != 9 || parent == by_local_.end())
            return {};
        const auto& root = objects_.at(parent->second);
        if (root.is_attachment || root.parent_id != 0 || root.shape.pcode != 9)
            return {};
    }
    if (object.is_attachment && (object.shape.pcode != 9 || object.shape.state == 0 || object.parent_id == 0))
        return {};
    if (object.local_id == 0)
        object.local_id = allocate_local_id();
    object.attached_to_backup = attach_to_backup && !object.is_attachment && !object.parent_id;
    if (attach_to_backup)
        object.changed = true;
    if (object.is_attachment)
        attachments_[object.parent_id].push_back(object.id);
    if (object.parent_id && !object.is_attachment) {
        if (!object.link_order)
            object.link_order = ++link_sequence_;
        else
            link_sequence_ = std::max(link_sequence_, object.link_order);
    } else
        object.link_order = 0;
    if (!object.parent_id && !object.is_attachment && (object.flags & 0x20000000u))
        temporary_expiry_[object.id] = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    objects_[object.id] = object;
    by_local_[object.local_id] = object.id;
    if (send_update)
        event_unlocked(WorldEventKind::Added, object);
    return object;
}
std::optional<SceneObject> World::object(framework::Uuid id) const {
    std::lock_guard lock(mutex_);
    auto i = objects_.find(id);
    return i == objects_.end() ? std::nullopt : std::optional(i->second);
}
std::optional<SceneObject> World::object(std::uint32_t local_id) const {
    std::lock_guard lock(mutex_);
    auto i = by_local_.find(local_id);
    if (i == by_local_.end())
        return {};
    auto row = objects_.find(i->second);
    return row == objects_.end() ? std::nullopt : std::optional(row->second);
}
std::vector<SceneObject> World::objects() const {
    std::lock_guard lock(mutex_);
    std::vector<SceneObject> result;
    result.reserve(objects_.size());
    for (auto& [id, object] : objects_)
        result.push_back(object);
    return result;
}
std::vector<SceneObject> World::attachments(std::uint32_t parent) const {
    std::lock_guard lock(mutex_);
    std::vector<SceneObject> result;
    const auto worn = attachments_.find(parent);
    if (worn == attachments_.end())
        return result;
    result.reserve(worn->second.size());
    for (const auto& id : worn->second)
        result.push_back(objects_.at(id));
    return result;
}
std::size_t World::size() const {
    std::lock_guard lock(mutex_);
    return objects_.size();
}
std::int32_t World::prim_count(framework::Uuid id, bool include_attachments) const {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return 0;
    const auto& object = entry->second;
    if (object.is_attachment)
        return include_attachments ? 1 : 0;
    const auto root = object.parent_id ? object.parent_id : object.local_id;
    std::int32_t count = 1;
    if (root)
        for (const auto& [part_id, part] : objects_)
            if (!part.is_attachment && part.parent_id == root)
                ++count;
    return count;
}
framework::Uuid World::link_key(framework::Uuid id, std::int32_t number) const {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (number == -4 && entry != objects_.end())
        return id;
    if (entry == objects_.end() || number < 0)
        return {};
    const auto& object = entry->second;
    auto root = entry;
    if (object.parent_id && !object.is_attachment) {
        const auto parent = by_local_.find(object.parent_id);
        if (parent == by_local_.end())
            return {};
        root = objects_.find(parent->second);
        if (root == objects_.end())
            return {};
    }
    if (number < 2)
        return root->first;
    if (root->second.is_attachment || !root->second.local_id)
        return {};
    std::vector<const SceneObject*> children;
    for (const auto& [part_id, part] : objects_)
        if (!part.is_attachment && part.parent_id == root->second.local_id)
            children.push_back(&part);
    const auto index = static_cast<std::size_t>(number - 2);
    if (index >= children.size())
        return {};
    const auto child = children.begin() + index;
    std::nth_element(children.begin(), child, children.end(),
                     [](const auto* a, const auto* b) { return a->link_order < b->link_order; });
    return (*child)->id;
}
std::int32_t World::link_number(framework::Uuid id) const {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end() || entry->second.is_attachment)
        return 0;
    const auto& object = entry->second;
    const auto parent = object.parent_id ? object.parent_id : object.local_id;
    if (!parent)
        return 0;
    std::int32_t number = object.parent_id ? 2 : 0;
    for (const auto& [part_id, part] : objects_) {
        if (part.is_attachment || part.parent_id != parent)
            continue;
        if (!object.parent_id)
            return 1;
        if (part.link_order < object.link_order)
            ++number;
    }
    return number;
}
framework::Vector3 World::geometric_center(framework::Uuid id) const {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return {};
    const auto& object = entry->second;
    const auto root_local = object.parent_id && !object.is_attachment ? object.parent_id : object.local_id;
    if (!root_local || !by_local_.contains(root_local))
        return {};
    framework::Vector3 center{};
    std::size_t count = 1;
    for (const auto& [part_id, part] : objects_) {
        if (part.is_attachment || part.parent_id != root_local)
            continue;
        center.x += part.position.x;
        center.y += part.position.y;
        center.z += part.position.z;
        ++count;
    }
    const auto divisor = static_cast<float>(count);
    return {center.x / divisor, center.y / divisor, center.z / divisor};
}
bool World::update_transform(framework::Uuid id, framework::Vector3 position, framework::Quaternion rotation,
                             framework::Vector3 scale) {
    return patch_transform(id, position, rotation, scale);
}
bool World::patch_transform(framework::Uuid id, std::optional<framework::Vector3> position,
                            std::optional<framework::Quaternion> rotation,
                            std::optional<framework::Vector3> scale, bool preserve_children) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    auto& object = entry->second;
    constexpr std::int32_t changed_scale = 8;
    const auto changes = scale && object.scale != *scale ? changed_scale : 0;
    if (preserve_children && object.local_id && object.parent_id == 0 && !object.is_attachment &&
        (position || rotation)) {
        const auto next = position.value_or(object.position);
        const auto q = rotation.value_or(object.rotation);
        const framework::Quaternion inverse{-q.x, -q.y, -q.z, q.w};
        const auto adjustment = framework::sdk_multiply_quaternion(inverse, object.rotation);
        const auto delta = framework::sdk_rotate_vector(
            {object.position.x - next.x, object.position.y - next.y, object.position.z - next.z}, inverse);
        for (auto& [part_id, part] : objects_) {
            if (part.is_attachment || part.parent_id != object.local_id)
                continue;
            const auto offset = framework::sdk_rotate_vector(part.position, adjustment);
            part.position = {offset.x + delta.x, offset.y + delta.y, offset.z + delta.z};
            if (rotation)
                part.rotation = framework::sdk_multiply_quaternion(adjustment, part.rotation);
            part.changed = true;
            event_unlocked(WorldEventKind::Updated, part);
        }
    }
    if (position)
        object.position = *position;
    if (rotation)
        object.rotation = *rotation;
    if (scale)
        object.scale = *scale;
    mark_linkset_changed_unlocked(object);
    event_unlocked(WorldEventKind::Updated, object, changes);
    return true;
}
void World::mark_linkset_changed_unlocked(SceneObject& part) {
    part.changed = true;
    if (!part.is_attachment && part.parent_id) {
        const auto parent = by_local_.find(part.parent_id);
        if (parent != by_local_.end())
            objects_.at(parent->second).changed = true;
    }
}
bool World::update_object_text(framework::Uuid id, bool description, std::string value) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    auto& object = entry->second;
    auto& text = description ? object.description : object.name;
    if (text == value)
        return true;
    text = std::move(value);
    mark_linkset_changed_unlocked(object);
    event_unlocked(WorldEventKind::Updated, object);
    return true;
}
bool World::update_click_action(framework::Uuid id, std::uint8_t action, bool broadcast) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    if (broadcast && entry->second.click_action == action)
        return true;
    entry->second.click_action = action;
    mark_linkset_changed_unlocked(entry->second);
    if (broadcast)
        event_unlocked(WorldEventKind::Updated, entry->second);
    return true;
}
void World::set_allowed_drop(framework::Uuid id, bool allowed) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& part = entry->second;
    constexpr std::uint32_t flag = 0x10000u;
    const auto flags = allowed ? part.flags | flag : part.flags & ~flag;
    if (flags != part.flags) {
        part.flags = flags;
        mark_linkset_changed_unlocked(part);
    }
}
void World::set_seat_camera(framework::Uuid id, std::optional<framework::Vector3> eye,
                             std::optional<framework::Vector3> at, std::optional<bool> mouselook) {
    std::lock_guard lock(mutex_);
    if (const auto entry = objects_.find(id); entry != objects_.end()) {
        auto& part = entry->second;
        if (eye)
            part.camera_eye_offset = *eye;
        if (at)
            part.camera_at_offset = *at;
        if (mouselook)
            part.force_mouselook = *mouselook;
    }
}
void World::set_stand_target(framework::Uuid id, framework::Vector3 offset) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry != objects_.end() && entry->second.stand_target != offset) {
        entry->second.stand_target = offset;
        mark_linkset_changed_unlocked(entry->second);
    }
}
void World::set_sit_active_range(framework::Uuid id, float range) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry != objects_.end() && entry->second.sit_active_range != range) {
        entry->second.sit_active_range = range;
        mark_linkset_changed_unlocked(entry->second);
    }
}
void World::set_sit_target(framework::Uuid id, framework::Vector3 position, framework::Quaternion orientation) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& part = entry->second;
    if (part.sit_target_position == position && part.sit_target_orientation == orientation)
        return;
    part.sit_target_position = position;
    part.sit_target_orientation = orientation;
    mark_linkset_changed_unlocked(part);
}
void World::set_pass_touches(framework::Uuid id, bool pass) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry != objects_.end() && entry->second.pass_touches != pass) {
        entry->second.pass_touches = pass;
        mark_linkset_changed_unlocked(entry->second);
    }
}
bool World::update_motion(framework::Uuid id, framework::Vector3 velocity,
                          framework::Vector3 angular_velocity, framework::Vector3 acceleration) {
    std::lock_guard lock(mutex_);
    auto i = objects_.find(id);
    if (i == objects_.end())
        return false;
    auto& object = i->second;
    object.velocity = velocity;
    object.angular_velocity = angular_velocity;
    object.acceleration = acceleration;
    object.changed = true;
    event_unlocked(WorldEventKind::Updated, object);
    return true;
}
std::optional<std::uint8_t> World::update_material(framework::Uuid id, std::uint8_t material) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return std::nullopt;
    auto& object = entry->second;
    constexpr float friction[] = {.8f, .3f, .2f, .6f, .9f, .4f, .9f, 0.f};
    constexpr float restitution[] = {.4f, .4f, .7f, .5f, .3f, .7f, .95f, 0.f};
    if (material <= 7 && (object.material != material || object.physics.friction != friction[material] ||
                          object.physics.restitution != restitution[material])) {
        object.material = material;
        object.physics.friction = friction[material];
        object.physics.restitution = restitution[material];
        mark_linkset_changed_unlocked(object);
        event_unlocked(WorldEventKind::Updated, object);
    }
    return object.material;
}
std::optional<SceneObject> World::prepare_attached_sound(framework::Uuid id, float default_radius) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return {};
    auto& object = entry->second;
    if (object.sound_radius == 0)
        object.sound_radius = default_radius;
    object.sound_flags = 0;
    return object;
}
void World::set_pay_prices(framework::Uuid id, const std::array<std::int32_t, 5>& prices) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end() || (entry->second.parent_id && !entry->second.is_attachment))
        return;
    entry->second.pay_prices = prices;
    mark_linkset_changed_unlocked(entry->second);
}
void World::set_collision_sound(framework::Uuid id, framework::Uuid asset, float volume) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& object = entry->second;
    object.collision_sound = asset;
    object.collision_sound_volume = volume >= 0.f ? volume : 0.f;
    mark_linkset_changed_unlocked(object);
}
void World::set_sound_queueing(framework::Uuid id, bool queue) {
    std::lock_guard lock(mutex_);
    if (const auto entry = objects_.find(id); entry != objects_.end())
        entry->second.sound_queueing = queue;
}
void World::set_sound_radius(framework::Uuid id, float radius) {
    std::lock_guard lock(mutex_);
    if (const auto entry = objects_.find(id); entry != objects_.end())
        entry->second.sound_radius = radius;
}
void World::set_looped_sound(framework::Uuid id, framework::Uuid asset, float gain, float radius,
                             std::uint8_t sync_flags) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& object = entry->second;
    object.looped_sound = asset;
    object.looped_sound_gain = gain;
    object.sound_flags = asset.is_zero() ? 32 : 1 | sync_flags | (object.sound_queueing ? 16 : 0);
    if (!asset.is_zero() && object.sound_radius == 0)
        object.sound_radius = radius;
    mark_linkset_changed_unlocked(object);
    event_unlocked(WorldEventKind::Updated, object);
}
void World::set_interaction_label(framework::Uuid id, bool sit, std::string text) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& object = entry->second;
    auto& label = sit ? object.sit_name : object.touch_name;
    if (label == text)
        return;
    label = std::move(text);
    mark_linkset_changed_unlocked(object);
}
void World::set_floating_text(framework::Uuid id, std::string text, std::uint32_t argb) {
    if (text.size() > 254) {
        std::size_t end = 254;
        while (end && (static_cast<unsigned char>(text[end]) & 0xc0u) == 0x80u)
            --end;
        text.resize(end);
    }
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& object = entry->second;
    if (object.floating_text == text && object.text_argb == argb)
        return;
    object.floating_text = std::move(text);
    object.text_argb = argb;
    mark_linkset_changed_unlocked(object);
    event_unlocked(WorldEventKind::Updated, object);
}
void World::update_extra_param(framework::Uuid id, std::uint16_t type, bool enabled,
                               const std::vector<std::uint8_t>& data) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& object = entry->second;
    framework::PrimitiveBaseShapeCore shape([](const std::string&) {});
    try {
        shape.read_extra_params(std::make_shared<framework::ShapeByteArray>(object.extra_params));
        shape.update_extra_param(type, enabled, std::make_shared<framework::ShapeByteArray>(data));
    } catch (const framework::PrimitiveShapeError&) {
        return;
    }
    object.extra_params = shape.extra_params();
    mark_linkset_changed_unlocked(object);
    event_unlocked(WorldEventKind::Updated, object);
}
bool World::update_shape(framework::Uuid id, PrimShape shape) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    auto& object = entry->second;
    shape.pcode = object.shape.pcode;
    shape.state = object.shape.state;
    object.shape = shape;
    if (!object.is_attachment && object.parent_id != 0) {
        const auto parent = by_local_.find(object.parent_id);
        if (parent != by_local_.end())
            objects_.at(parent->second).rezzed_at = std::chrono::system_clock::now();
    }
    mark_linkset_changed_unlocked(object);
    constexpr std::int32_t changed_shape = 4;
    event_unlocked(WorldEventKind::Updated, object, changed_shape);
    return true;
}
int PrimShape::face_count() const {
    const auto profile = profile_curve & 7;
    const bool straight = path_curve == 16 || path_curve == 128;
    const bool curved = path_curve == 32;
    const bool profile_cut = profile_begin != 0 || profile_end != 0;
    const bool path_cut =
        path_begin != 0 || path_end != 0 || path_skew != 0 || path_twist_begin != path_twist;
    const bool hollow = profile_hollow != 0;
    if (profile == 5 && (curved || path_curve == 48))
        return 1 + 2 * path_cut + 2 * (profile_cut || hollow) + hollow;

    const bool curved_path_cut = curved && (path_cut || path_taper_x != 0 || path_taper_y != 0 ||
                                            path_revolutions != 0 || path_radius_offset != 0);
    int faces = 6;
    int removed = profile_begin / 12500 + profile_end / 12500;
    if (profile == 0 && (straight || curved)) {
        faces = curved ? 1 : 3;
        removed = 0;
    } else if (profile == 3 && (straight || curved)) {
        faces = curved ? 3 : 5;
        const auto triangle_cut = [](std::uint16_t cut) { return (cut >= 16667) + (cut >= 33333); };
        removed = triangle_cut(profile_begin) + triangle_cut(profile_end);
    } else if (profile == 1 && curved) {
        faces = 4;
    }
    const bool curved_primitive = curved && (profile == 0 || profile == 1 || profile == 3);
    return faces + (curved_primitive && curved_path_cut ? 2 : 0) + (profile_cut ? 2 - removed : 0) + hollow;
}
bool World::update_texture(framework::Uuid id, const std::vector<std::uint8_t>& bytes) {
    framework::TextureEntry incoming(bytes);
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    auto& object = entry->second;
    framework::TextureEntry current(object.texture_entry);
    const auto blank = framework::TextureEntry(framework::TextureEntry::white_texture).default_texture;
    if (!incoming.default_texture)
        incoming.default_texture = blank;
    if (!current.default_texture)
        current.default_texture = blank;
    const auto properties = [](const framework::TextureFace& face) {
        return std::tuple{face.texture_id(),  face.bump(),     face.fullbright(), face.glow(),
                          face.media_flags(), face.offset_u(), face.offset_v(),   face.repeat_u(),
                          face.repeat_v(),    face.rotation(), face.shiny(),      face.mapping()};
    };
    const int sides = object.shape.face_count();
    constexpr std::int32_t changed_color = 2, changed_texture = 16;
    std::int32_t changes = 0;
    for (int i = 0; i < sides; ++i) {
        const auto& before = current.faces[i] ? current.faces[i] : current.default_texture;
        const auto& after = incoming.faces[i] ? incoming.faces[i] : incoming.default_texture;
        if (before->color() != after->color())
            changes |= changed_color;
        if (properties(*before) != properties(*after))
            changes |= changed_texture;
        if (changes == (changed_color | changed_texture))
            break;
    }
    if (!changes)
        return false;
    if (sides == 1 && incoming.faces[0]) {
        incoming.default_texture = framework::TextureEntry(*incoming.get_face(0)).default_texture;
        incoming.faces[0].reset();
    }
    object.texture_entry = incoming.bytes(sides);
    mark_linkset_changed_unlocked(object);
    event_unlocked(WorldEventKind::Updated, object, changes);
    return true;
}
bool World::set_selected(framework::Uuid id, bool selected) {
    std::lock_guard lock(mutex_);
    if (!objects_.contains(id))
        return false;
    return selected ? selected_.insert(id).second : selected_.erase(id) != 0;
}
bool World::update_angular_velocity(framework::Uuid id, framework::Vector3 angular_velocity,
                                    bool full_refresh) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    auto& object = entry->second;
    if (object.angular_velocity != angular_velocity) {
        object.angular_velocity = angular_velocity;
        object.changed = true;
    } else if (!full_refresh) {
        return true;
    }
    event_unlocked(full_refresh ? WorldEventKind::Updated : WorldEventKind::TerseUpdated, object);
    return true;
}
bool World::link_prim(framework::Uuid id, framework::Uuid root_id) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    const auto parent = objects_.find(root_id);
    if (entry == objects_.end() || parent == objects_.end() || entry == parent)
        return false;
    auto& child = entry->second;
    auto& root = parent->second;
    if (child.is_attachment || root.is_attachment || child.parent_id || root.parent_id ||
        child.shape.pcode != 9 || root.shape.pcode != 9 || child.owner != root.owner || !child.local_id ||
        !root.local_id)
        return false;
    const auto q = root.rotation;
    const framework::Quaternion inverse{-q.x, -q.y, -q.z, q.w};
    const auto source_local = child.local_id;
    const auto source_position = child.position;
    const auto source_rotation = child.rotation;
    constexpr std::int32_t changed_link = 32;
    const auto reparent = [&](SceneObject& part, framework::Vector3 position,
                              framework::Quaternion rotation) {
        part.position = framework::sdk_rotate_vector(
            {position.x - root.position.x, position.y - root.position.y, position.z - root.position.z},
            inverse);
        part.rotation = framework::sdk_multiply_quaternion(inverse, rotation);
        part.parent_id = root.local_id;
        part.attached_to_backup = false;
        part.link_order = ++link_sequence_;
        part.changed = true;
        event_unlocked(WorldEventKind::Updated, part, changed_link);
    };
    root.changed = true;
    event_unlocked(WorldEventKind::Updated, root, changed_link);
    for (const auto& [part_id, part] : objects_)
        if (!part.is_attachment && part.parent_id == root.local_id)
            event_unlocked(WorldEventKind::Updated, part, changed_link);
    child.sale_type = 0;
    child.sale_price = 10;
    temporary_expiry_.erase(child.id);
    reparent(child, source_position, source_rotation);
    std::vector<SceneObject*> source_children;
    for (auto& [part_id, part] : objects_)
        if (!part.is_attachment && part.parent_id == source_local)
            source_children.push_back(&part);
    std::sort(source_children.begin(), source_children.end(),
              [](const auto* a, const auto* b) { return a->link_order < b->link_order; });
    for (auto* child_part : source_children) {
        auto& part = *child_part;
        const auto offset = framework::sdk_rotate_vector(part.position, source_rotation);
        reparent(part,
                 {source_position.x + offset.x, source_position.y + offset.y, source_position.z + offset.z},
                 framework::sdk_multiply_quaternion(source_rotation, part.rotation));
    }
    return true;
}
bool World::delink_child(framework::Uuid id, framework::Uuid expected_root) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    const auto parent = objects_.find(expected_root);
    if (entry == objects_.end() || parent == objects_.end())
        return false;
    auto& child = entry->second;
    auto& root = parent->second;
    if (child.is_attachment || child.parent_id == 0 || child.parent_id != root.local_id ||
        root.is_attachment || root.parent_id != 0)
        return false;
    const auto offset = framework::sdk_rotate_vector(child.position, root.rotation);
    child.position = {root.position.x + offset.x, root.position.y + offset.y, root.position.z + offset.z};
    child.rotation = framework::sdk_multiply_quaternion(root.rotation, child.rotation);
    child.last_owner = root.last_owner;
    child.rezzer = root.rezzer;
    child.rezzed_at = root.rezzed_at;
    child.parent_id = 0;
    child.attached_to_backup = true;
    child.link_order = 0;
    if (child.flags & 0x20000000u)
        temporary_expiry_[child.id] = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    child.changed = root.changed = true;
    constexpr std::int32_t changed_link = 32;
    event_unlocked(WorldEventKind::Updated, child, changed_link);
    event_unlocked(WorldEventKind::Updated, root, changed_link);
    for (const auto& [part_id, part] : objects_)
        if (!part.is_attachment && part.parent_id == root.local_id)
            event_unlocked(WorldEventKind::Updated, part, changed_link);
    return true;
}
std::optional<PrimPhysicsProperties> World::update_extra_physics(framework::Uuid id,
                                                                 PrimPhysicsProperties requested) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return {};
    auto& part = entry->second;
    auto& current = part.physics;
    if (requested.shape == 255)
        return current;
    const auto previous = current;
    const bool root = part.is_attachment || part.parent_id == 0;
    current.shape = requested.shape > 2 || (root && requested.shape == 1) ? 0 : requested.shape;
    if (requested.density >= 1.f && requested.density <= 22587.f)
        current.density = requested.density;
    if (requested.friction >= 0.f && requested.friction <= 255.f)
        current.friction = requested.friction;
    if (requested.restitution >= 0.f && requested.restitution <= 1.f)
        current.restitution = requested.restitution;
    if (requested.gravity >= -1.f && requested.gravity <= 28.f)
        current.gravity = requested.gravity;
    if (current != previous) {
        mark_linkset_changed_unlocked(part);
        event_unlocked(WorldEventKind::Updated, part);
    }
    return current;
}
bool World::set_sale_info(framework::Uuid id, std::uint8_t type, std::int32_t price) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    auto& root = entry->second;
    root.sale_type = type;
    root.sale_price = price;
    root.changed = true;
    return true;
}
bool World::set_linkset_data(framework::Uuid id, std::vector<std::uint8_t> data,
                             std::optional<std::int32_t> empty_limit) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    entry->second.linkset_data = std::move(data);
    entry->second.empty_linkset_data_limit = empty_limit;
    entry->second.changed = true;
    return true;
}
bool World::sell_original(const SceneObject& expected, framework::Uuid owner, framework::Uuid group,
                          bool apply_next_permissions, bool propagate_permissions) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(expected.id);
    if (entry == objects_.end())
        return false;
    auto& root = entry->second;
    if (root.owner != expected.owner || root.sale_type != 1 || root.sale_price != 0 ||
        (!root.is_attachment && root.parent_id))
        return false;
    for (auto& [id, part] : objects_) {
        if (id != root.id && (part.is_attachment || !root.local_id || part.parent_id != root.local_id))
            continue;
        if (part.group != part.owner)
            part.last_owner = part.owner;
        part.owner = owner;
        part.group = group;
        if (apply_next_permissions) {
            constexpr std::uint32_t export_permission = 1u << 16;
            part.base_mask &= part.next_mask | export_permission;
            part.owner_mask &= part.next_mask;
            part.everyone_mask &= part.next_mask | export_permission;
            part.group_mask = 0;
        }
        if (id == root.id) {
            part.sale_type = 0;
            part.sale_price = 10;
            part.click_action = 0;
        }
        part.changed = true;
        constexpr std::int32_t changed_owner = 128;
        event_unlocked(WorldEventKind::Updated, part,
                       propagate_permissions || id == root.id ? changed_owner : 0);
    }
    return true;
}
void World::set_group_flags(framework::Uuid id, bool physical, bool temporary, bool phantom) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    const auto& root = entry->second;
    constexpr std::uint32_t physics = 1u, phantom_flag = 0x400u, temporary_on_rez = 0x20000000u;
    const auto flags =
        (physical ? physics : 0u) | (temporary ? temporary_on_rez : 0u) | (phantom ? phantom_flag : 0u);
    if (!root.is_attachment && !root.parent_id) {
        if (temporary && !(root.flags & temporary_on_rez))
            temporary_expiry_[id] = std::chrono::steady_clock::now() + std::chrono::minutes(1);
        if (!temporary)
            temporary_expiry_.erase(id);
    }
    for (auto& [part_id, part] : objects_) {
        if (part_id != id && (!root.local_id || part.is_attachment || part.parent_id != root.local_id))
            continue;
        const auto updated = (part.flags & ~(physics | phantom_flag | temporary_on_rez)) | flags;
        if (updated == part.flags)
            continue;
        part.flags = updated;
        part.changed = true;
        event_unlocked(WorldEventKind::Updated, part);
    }
}
bool World::resize_group(framework::Uuid id, framework::Vector3 scale, float minimum, float maximum,
                         std::optional<framework::Vector3> position) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    auto& root = entry->second;
    if (root.scale.x <= 0 || root.scale.y <= 0 || root.scale.z <= 0)
        return false;
    framework::Vector3 factors{scale.x / root.scale.x, scale.y / root.scale.y, scale.z / root.scale.z};
    std::vector<SceneObject*> children;
    if (root.local_id && !root.is_attachment)
        for (auto& [part_id, part] : objects_)
            if (!part.is_attachment && part.parent_id == root.local_id)
                children.push_back(&part);
    std::sort(children.begin(), children.end(),
              [](const auto* a, const auto* b) { return a->link_order < b->link_order; });
    const auto product = [](framework::Vector3 a, framework::Vector3 b) {
        return framework::Vector3{a.x * b.x, a.y * b.y, a.z * b.z};
    };
    const auto limit = [&](float size) {
        if (size <= 0)
            return;
        const auto adjustment = std::clamp(size, minimum, maximum) / size;
        factors = product(factors, {adjustment, adjustment, adjustment});
    };
    for (const auto* part : children) {
        limit(part->scale.x * factors.x);
        limit(part->scale.y * factors.y);
        limit(part->scale.z * factors.z);
    }
    constexpr std::int32_t changed_scale = 8;
    if (position)
        root.position = *position;
    root.scale = product(root.scale, factors);
    root.changed = true;
    event_unlocked(WorldEventKind::Updated, root, changed_scale);
    for (auto* part : children) {
        part->scale = product(part->scale, factors);
        part->position = product(part->position, factors);
        part->changed = true;
        event_unlocked(WorldEventKind::Updated, *part, changed_scale);
    }
    return true;
}
bool World::delink_root(framework::Uuid id) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end() || entry->second.is_attachment || entry->second.parent_id != 0 ||
        entry->second.local_id == 0)
        return false;
    auto& root = entry->second;
    std::vector<SceneObject*> children;
    for (auto& [part_id, part] : objects_)
        if (!part.is_attachment && part.parent_id == root.local_id)
            children.push_back(&part);
    if (children.empty())
        return false;
    const auto first = std::min_element(children.begin(), children.end(), [](const auto* a, const auto* b) {
        return a->link_order < b->link_order;
    });
    auto& replacement = **first;
    const auto origin = replacement.position;
    const auto rotation = replacement.rotation;
    const framework::Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
    const auto offset = framework::sdk_rotate_vector(origin, root.rotation);
    replacement.position = {root.position.x + offset.x, root.position.y + offset.y,
                            root.position.z + offset.z};
    replacement.rotation = framework::sdk_multiply_quaternion(root.rotation, rotation);
    replacement.parent_id = 0;
    replacement.rezzed_at = root.rezzed_at;
    replacement.attached_to_backup = true;
    replacement.link_order = 0;
    if (replacement.flags & 0x20000000u)
        temporary_expiry_[replacement.id] = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    constexpr std::int32_t changed_link = 32;
    replacement.changed = root.changed = true;
    event_unlocked(WorldEventKind::Updated, replacement, changed_link);
    for (auto* child : children) {
        if (child == &replacement)
            continue;
        child->position = framework::sdk_rotate_vector(
            {child->position.x - origin.x, child->position.y - origin.y, child->position.z - origin.z},
            inverse);
        child->rotation = framework::sdk_multiply_quaternion(inverse, child->rotation);
        child->parent_id = replacement.local_id;
        child->rezzed_at = root.rezzed_at;
        child->changed = true;
        event_unlocked(WorldEventKind::Updated, *child, changed_link);
    }
    event_unlocked(WorldEventKind::Updated, root, changed_link);
    return true;
}
void World::publish_added_linkset(framework::Uuid id) {
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(id);
    if (root == objects_.end())
        return;
    event_unlocked(WorldEventKind::Added, root->second);
    for (const auto& [part_id, part] : objects_)
        if (part_id != id && !part.is_attachment && part.parent_id == root->second.local_id)
            event_unlocked(WorldEventKind::Added, part);
}
bool World::remove(framework::Uuid id, bool send_update) {
    std::lock_guard lock(mutex_);
    return remove_unlocked(id, send_update);
}
void World::expire_temporary_objects(std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(mutex_);
    for (auto expiry = temporary_expiry_.begin(); expiry != temporary_expiry_.end();) {
        if (expiry->second > now) {
            ++expiry;
            continue;
        }
        const auto id = expiry++->first;
        remove_unlocked(id);
    }
}
bool World::remove_unlocked(framework::Uuid id, bool send_update) {
    temporary_expiry_.erase(id);
    auto i = objects_.find(id);
    if (i == objects_.end())
        return false;
    auto object = i->second;
    if (!send_update) {
        std::erase_if(events_, [&](const auto& event) {
            return event.object.id == id ||
                   (!object.is_attachment && !object.parent_id && !event.object.is_attachment &&
                    event.object.parent_id == object.local_id);
        });
    }
    if (!object.is_attachment && object.parent_id == 0) {
        for (auto child = objects_.begin(); child != objects_.end();) {
            const auto& part = child->second;
            if (!part.is_attachment && part.parent_id == object.local_id && part.parent_id != 0) {
                if (send_update)
                    event_unlocked(WorldEventKind::Removed, part);
                by_local_.erase(part.local_id);
                selected_.erase(part.id);
                child = objects_.erase(child);
            } else
                ++child;
        }
    }
    if (object.is_attachment) {
        const auto worn = attachments_.find(object.parent_id);
        std::erase(worn->second, id);
        if (worn->second.empty())
            attachments_.erase(worn);
    }
    by_local_.erase(object.local_id);
    selected_.erase(id);
    objects_.erase(i);
    if (send_update)
        event_unlocked(WorldEventKind::Removed, object);
    return true;
}
bool World::set_inventory_state(framework::Uuid id, bool empty, bool scripted, bool touch,
                                std::optional<std::uint32_t> local) {
    std::lock_guard lock(mutex_);
    auto i = objects_.find(id);
    if (i == objects_.end())
        return false;
    auto& object = i->second;
    object.inventory_empty = empty;
    if (local)
        object.flags = (object.flags & ~0x10280u) | (*local & 0x10280u);
    object.flags = (object.flags & ~0xc0u) | (scripted ? 0x40u : 0u) | (touch ? 0x80u : 0u);
    object.changed = true;
    event_unlocked(WorldEventKind::Updated, object);
    return true;
}
std::vector<WorldEvent> World::drain_events() {
    std::lock_guard lock(mutex_);
    std::vector<WorldEvent> result;
    result.swap(events_);
    return result;
}
}
