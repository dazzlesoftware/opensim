// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/world.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/framework/texture_entry.hpp"
#include "opensim/framework/permissions.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include <tuple>
#include <chrono>
#include <bit>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace opensim::scene {
namespace {
constexpr std::uint8_t prim_physics_shape = 0;
constexpr std::uint8_t no_physics_shape = 1;
constexpr std::uint8_t convex_hull_shape = 2;
constexpr std::uint8_t mesh_sculpt_type = 5;

std::uint8_t default_physics_shape(const SceneObject& object) {
    if (object.extra_params.size() > 1) {
        auto geometry = framework::PrimitiveBaseShapeCore::create([](const std::string&) {});
        geometry.read_extra_params(std::make_shared<framework::ShapeByteArray>(object.extra_params));
        if (geometry.sculpt_type == mesh_sculpt_type)
            return convex_hull_shape;
    }
    return prim_physics_shape;
}
}

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
void World::clamp_inserted_prim_sizes(float minimum, float maximum) {
    std::lock_guard lock(mutex_);
    insertion_scale_limits_.emplace(minimum, maximum);
}
std::uint32_t World::allocate_local_id() {
    return last_local_id_.fetch_add(1, std::memory_order_relaxed) + 1u;
}
void World::event_unlocked(WorldEventKind kind, const SceneObject& object, std::int32_t script_changes) {
    events_.push_back({kind, object, ++sequence_, script_changes});
}
std::optional<SceneObject> World::add(SceneObject object, bool attach_to_backup, bool send_update) {
    std::lock_guard lock(mutex_);
    return add_unlocked(std::move(object), attach_to_backup, send_update);
}
std::optional<SceneObject> World::add_unlocked(SceneObject object, bool attach_to_backup, bool send_update) {
    if (object.id.is_zero())
        return {};
    if (objects_.contains(object.id))
        return {};
    if (!object.is_attachment && object.parent_id != 0) {
        const auto parent = by_local_.find(object.parent_id);
        if (object.shape.pcode != 9 || parent == by_local_.end())
            return {};
        const auto& root = objects_.at(parent->second);
        if ((!root.is_attachment && root.parent_id != 0) || root.shape.pcode != 9)
            return {};
    }
    if (object.is_attachment && (object.shape.pcode != 9 || object.shape.state == 0 || object.parent_id == 0))
        return {};
    if (insertion_scale_limits_) {
        const auto [minimum, maximum] = *insertion_scale_limits_;
        const auto clamp = [minimum, maximum](float axis) {
            if (axis > maximum)
                return maximum;
            if (axis < minimum)
                return minimum;
            return axis;
        };
        object.scale = {clamp(object.scale.x), clamp(object.scale.y), clamp(object.scale.z)};
    }
    if (object.shape.pcode == 9 && !object.parent_id && object.physics.shape == no_physics_shape)
        object.physics.shape = default_physics_shape(object);
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
        object.link_membership_order = assign_link_membership_unlocked(by_local_.at(object.parent_id));
    } else {
        object.link_order = object.link_membership_order = 0;
    }
    if (!object.parent_id && !object.is_attachment && (object.flags & 0x20000000u))
        temporary_expiry_[object.id] = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    objects_[object.id] = object;
    by_local_[object.local_id] = object.id;
    if (send_update)
        event_unlocked(WorldEventKind::Added, object);
    return object;
}
bool World::add_crossing_linkset(std::vector<SceneObject> parts, std::uint32_t wearer_local_id) {
    if (parts.empty() || (wearer_local_id && parts.front().shape.state == 0))
        return false;
    std::lock_guard lock(mutex_);
    std::set<framework::Uuid> ids;
    for (const auto& part : parts)
        if (part.id.is_zero() || part.shape.pcode != 9 ||
            (part.is_attachment && (!wearer_local_id || &part != &parts.front())) ||
            objects_.contains(part.id) || !ids.insert(part.id).second)
            return false;
    std::set<std::uint32_t> local_ids;
    for (auto& part : parts) {
        do {
            part.local_id = allocate_local_id();
        } while (!part.local_id || by_local_.contains(part.local_id) || !local_ids.insert(part.local_id).second);
    }
    const auto parent = parts.front().local_id;
    parts.front().is_attachment = wearer_local_id != 0;
    parts.front().parent_id = wearer_local_id;
    for (auto& part : std::span(parts).subspan(1))
        part.parent_id = parent;
    for (auto& part : parts)
        add_unlocked(std::move(part), !wearer_local_id, false);
    return true;
}
bool World::drop_attachment(framework::Uuid id, std::uint32_t wearer_local_id, framework::Vector3 position) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end() || !entry->second.is_attachment || entry->second.parent_id != wearer_local_id)
        return false;
    auto& root = entry->second;
    if (const auto worn = attachments_.find(wearer_local_id); worn != attachments_.end()) {
        std::erase(worn->second, id);
        if (worn->second.empty())
            attachments_.erase(worn);
    }
    root.saved_attachment_position = root.position;
    root.last_attachment_point = root.shape.state;
    root.from_inventory_item = root.from_inventory_folder = root.attachment_source_asset = {};
    root.parent_id = 0;
    root.is_attachment = false;
    if (root.physics.shape == no_physics_shape)
        root.physics.shape = default_physics_shape(root);
    root.position = position;
    root.attached_to_backup = true;
    root.flags &= ~0x20000000u;
    temporary_expiry_.erase(id);
    root.shape.state = 0;
    root.changed = true;
    event_unlocked(WorldEventKind::Updated, root);
    for (auto& [part_id, part] : objects_) {
        if (part.is_attachment || part.parent_id != root.local_id)
            continue;
        part.shape.state = 0;
        part.changed = true;
        event_unlocked(WorldEventKind::Updated, part);
    }
    return true;
}
bool World::attach_from_ground(framework::Uuid id, std::uint32_t wearer_local_id, std::uint8_t point,
                               framework::Vector3 offset, framework::Uuid item, framework::Uuid folder,
                               framework::Uuid asset) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end() || entry->second.is_attachment || entry->second.parent_id ||
        entry->second.shape.pcode != 9 || !wearer_local_id || !point)
        return false;
    auto& root = entry->second;
    attachments_[wearer_local_id].push_back(id);
    root.is_attachment = true;
    root.parent_id = wearer_local_id;
    root.position = offset;
    root.shape.state = point;
    root.from_inventory_item = item;
    root.from_inventory_folder = folder;
    root.attachment_source_asset = asset;
    root.attached_to_backup = false;
    root.changed = true;
    temporary_expiry_.erase(id);
    event_unlocked(WorldEventKind::Updated, root);
    for (const auto& [part_id, part] : objects_)
        if (!part.is_attachment && part.parent_id == root.local_id)
            event_unlocked(WorldEventKind::Updated, part);
    return true;
}
std::optional<SceneObject> World::object(framework::Uuid id) const {
    std::lock_guard lock(mutex_);
    auto i = objects_.find(id);
    return i == objects_.end() ? std::nullopt
                              : std::optional(snapshot_unlocked(i->second, std::chrono::steady_clock::now()));
}
std::optional<SceneObject> World::object(std::uint32_t local_id) const {
    std::lock_guard lock(mutex_);
    auto i = by_local_.find(local_id);
    if (i == by_local_.end())
        return {};
    auto row = objects_.find(i->second);
    return row == objects_.end() ? std::nullopt
                                : std::optional(snapshot_unlocked(row->second, std::chrono::steady_clock::now()));
}
std::vector<SceneObject> World::objects() const {
    std::lock_guard lock(mutex_);
    std::vector<SceneObject> result;
    result.reserve(objects_.size());
    const auto now = std::chrono::steady_clock::now();
    for (const auto& [id, object] : objects_)
        result.push_back(snapshot_unlocked(object, now));
    return result;
}
SceneObject World::snapshot_unlocked(const SceneObject& part, std::chrono::steady_clock::time_point now) const {
    auto snapshot = part;
    if (!snapshot.particle_system.empty() && particles_expired_unlocked(part.id, now))
        snapshot.particle_system.clear();
    return snapshot;
}
bool World::particles_expired_unlocked(framework::Uuid id, std::chrono::steady_clock::time_point now) const {
    const auto expiry = particle_expiry_.find(id);
    return expiry != particle_expiry_.end() && now.time_since_epoch() > expiry->second;
}
namespace {
std::optional<PhysicalPrim> collision_prim(const SceneObject& object, const SceneObject& root) {
    constexpr std::uint32_t physical = 1u, phantom = 0x400u;
    const bool dynamic = (root.flags & physical) != 0;
    const bool volume_detect = (object.flags & prim_flag_volume_detect) != 0;
    const bool is_phantom = !volume_detect && ((root.flags | object.flags) & phantom);
    if (object.is_attachment || root.is_attachment || (!dynamic && is_phantom) ||
        object.physics.shape == no_physics_shape)
        return {};

    const auto make_prim = [&](framework::PrimType type, std::uint8_t segments, float hollow,
                               framework::LinearPrimPath path, framework::Uuid asset = {}) {
        PhysicalPrim result{object.id, object.position, object.scale, {}, {}, {}, object.rotation,
                            object.physics.density, dynamic, type, segments, hollow, path, {}, asset,
                            !asset.is_zero() && object.physics.shape == convex_hull_shape,
                            {object.physics.friction, object.physics.restitution}};
        result.volume_detect = volume_detect;
        result.phantom = is_phantom;
        result.root_id = root.id;
        if (dynamic) {
            result.velocity = object.velocity;
            result.angular_velocity = object.angular_velocity;
            result.acceleration = object.acceleration;
            const float gravity = root.physics.gravity;
            if (gravity >= -1.f && gravity <= 28.f)
                result.gravity_modifier = gravity;
            if (std::isfinite(root.buoyancy))
                result.buoyancy = root.buoyancy;
            const auto force = root.force;
            if (std::isfinite(force.x) && std::isfinite(force.y) && std::isfinite(force.z))
                result.force = force;
            const auto torque = root.torque;
            if (std::isfinite(torque.x) && std::isfinite(torque.y) && std::isfinite(torque.z))
                result.torque = torque;
            result.hover = root.hover;
            result.move_target = root.move_target;
            result.look_at = root.look_at;
            result.inertia_override = root.inertia_override;
            result.rotation_axis_locks = root.rotation_axis_locks;
        }
        if (object.parent_id && !dynamic) {
            const auto offset = framework::sdk_rotate_vector(object.position, root.rotation);
            result.position = {root.position.x + offset.x, root.position.y + offset.y, root.position.z + offset.z};
            result.rotation = framework::sdk_multiply_quaternion(root.rotation, object.rotation);
        }
        return result;
    };
    if (object.extra_params.size() > 1) {
        auto geometry = framework::PrimitiveBaseShapeCore::create([](const std::string&) {});
        geometry.read_extra_params(std::make_shared<framework::ShapeByteArray>(object.extra_params));
        if (geometry.flexi_entry)
            return {};
        if (geometry.sculpt_entry) {
            if ((geometry.sculpt_type & 7) == mesh_sculpt_type && !geometry.sculpt_texture.is_zero())
                return make_prim(framework::PrimType::Sculpt, 0, 0.f, {}, geometry.sculpt_texture);
            return {};
        }
    }
    const auto& shape = object.shape;
    const PrimShape box;
    const auto profile = shape.profile_curve & 0x0f;
    const bool is_box = shape.path_curve == box.path_curve && profile == box.profile_curve;
    const bool is_cylinder = shape.path_curve == box.path_curve && profile == 0;
    const bool is_prism = shape.path_curve == box.path_curve && profile >= 2 && profile <= 4;
    const bool is_sphere = shape.path_curve == 32 && profile == 5;
    const bool swept = shape.path_curve == 32 && profile <= 4;
    const auto hollow_profile = shape.profile_curve & 0xf0;
    const bool convex = object.physics.shape == convex_hull_shape;
    const bool linear = is_box || is_cylinder || is_prism;
    const bool supported_hollow = (linear || swept) && hollow_profile <= 0x30;
    if (shape.pcode != box.pcode || (!linear && !is_sphere && !swept) ||
        (swept && convex) ||
        (convex && (shape.profile_begin || shape.profile_end)) ||
        (shape.profile_hollow && !convex && !supported_hollow) ||
        (!swept && (shape.path_radius_offset || shape.path_taper_x || shape.path_taper_y || shape.path_skew)))
        return {};
    framework::LinearPrimPath path;
    const auto decode_shear = [](std::uint8_t value) {
        return static_cast<float>(value < 128 ? value : int(value) - 256) * .01f;
    };
    if (linear) {
        constexpr float radians_per_twist_unit = std::numbers::pi_v<float> * .01f;
        path = {shape.path_begin * 2.e-5f, 1.f - shape.path_end * 2.e-5f,
                std::clamp((int(shape.path_scale_x) - 100) * .01f, -1.f, 1.f),
                std::clamp((int(shape.path_scale_y) - 100) * .01f, -1.f, 1.f),
                decode_shear(shape.path_shear_x), decode_shear(shape.path_shear_y),
                shape.path_twist_begin * radians_per_twist_unit, shape.path_twist * radians_per_twist_unit};
        if (path.begin >= path.end)
            return {};
    } else if ((!swept && (shape.path_begin || shape.path_end ||
                          shape.path_scale_x != box.path_scale_x || shape.path_scale_y != box.path_scale_y ||
                          shape.path_shear_x || shape.path_shear_y)) ||
               (!swept && (shape.path_twist || shape.path_twist_begin || shape.profile_begin ||
                           shape.profile_end))) {
        return {};
    }
    if (linear || swept) {
        path.profile_end = std::clamp(1.f - shape.profile_end * 2.e-5f, .02f, 1.f);
        path.profile_begin = shape.profile_begin * 2.e-5f;
        if (path.profile_begin >= path.profile_end)
            path.profile_begin = path.profile_end - .02f;
        if (shape.profile_hollow && !convex) {
            if (hollow_profile == 0x10 && profile != 0)
                path.hollow_segments = 24;
            else if (hollow_profile == 0x20 && profile != 1)
                path.hollow_segments = 4;
            else if (hollow_profile == 0x30 && (profile < 2 || profile > 4))
                path.hollow_segments = 3;
        }
    }
    if (swept) {
        path.begin = shape.path_begin * 2.e-5f;
        path.end = 1.f - shape.path_end * 2.e-5f;
        path.hole_x = (200 - int(shape.path_scale_x)) * .01f;
        path.hole_y = (200 - int(shape.path_scale_y)) * .01f;
        path.taper_x = std::clamp(shape.path_taper_x * .01f, -1.f, 1.f);
        path.taper_y = std::clamp(shape.path_taper_y * .01f, -1.f, 1.f);
        path.radius_offset = shape.path_radius_offset * .01f;
        path.skew = shape.path_skew * .01f;
        path.revolutions = 1.f + .015f * shape.path_revolutions;
        path.shear_x = decode_shear(shape.path_shear_x);
        path.shear_y = decode_shear(shape.path_shear_y);
        // The managed curved mesher uses PathTwistBegin for both endpoints.
        path.twist_begin = path.twist_end = shape.path_twist_begin * (std::numbers::pi_v<float> * .02f);
        if (path.begin >= path.end || path.hole_x <= 0.f || path.hole_x > 1.f ||
            path.hole_y <= 0.f || path.hole_y > .5f)
            return {};
    }
    const bool uniform_sphere = is_sphere && object.scale.x == object.scale.y && object.scale.y == object.scale.z;
    auto type = uniform_sphere ? framework::PrimType::Sphere : framework::PrimType::Box;
    std::uint8_t mesh_segments = 0;
    const bool large_enough_to_mesh = std::max({object.scale.x, object.scale.y, object.scale.z}) > .1f;
    if (large_enough_to_mesh && !uniform_sphere) {
        if (swept) {
            type = profile == 0 ? framework::PrimType::Torus :
                   profile == 1 ? framework::PrimType::Tube : framework::PrimType::Ring;
            mesh_segments = profile == 0 ? 24 : profile == 1 ? 4 : 3;
        } else if (is_prism) {
            type = framework::PrimType::Prism;
            mesh_segments = 3;
        } else if (is_sphere || is_cylinder) {
            type = is_sphere ? framework::PrimType::Sphere : framework::PrimType::Cylinder;
            mesh_segments = convex ? 6 : 24;
        } else if (path != framework::LinearPrimPath{} || (shape.profile_hollow && !convex)) {
            mesh_segments = 4;
        }
    }
    const float hollow = mesh_segments && !convex && supported_hollow
        ? shape.profile_hollow * 2.e-5f : 0.f;
    if (!mesh_segments)
        path.hollow_segments = 0;
    return make_prim(type, mesh_segments, hollow, path);
}
}
std::vector<PhysicalPrim> World::collision_prims() const {
    std::lock_guard lock(mutex_);
    std::vector<PhysicalPrim> boxes;
    std::map<framework::Uuid, std::vector<PhysicalPrimPart>> children;
    for (const auto& [id, object] : objects_) {
        if (selected_.contains(id))
            continue;
        const SceneObject* root = &object;
        if (object.parent_id) {
            const auto parent = by_local_.find(object.parent_id);
            if (parent == by_local_.end() || selected_.contains(parent->second))
                continue;
            root = &objects_.at(parent->second);
            if (root->parent_id)
                continue;
        }
        auto box = collision_prim(object, *root);
        if (!box)
            continue;
        if (object.parent_id && box->dynamic) {
            children[root->id].push_back({box->scale, box->position, box->rotation, box->density,
                                          box->type, box->mesh_segments, box->hollow, box->path, box->mesh_asset,
                                          box->convex_mesh, box->contact_material, box->id, box->volume_detect, box->phantom});
        } else {
            boxes.push_back(std::move(*box));
        }
    }
    for (auto& box : boxes)
        if (const auto parts = children.find(box.id); parts != children.end())
            box.children = std::move(parts->second);
    return boxes;
}
bool World::apply_physical_prim_motion(const PhysicalPrim& expected, const PhysicalPrim& motion) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(expected.id);
    if (entry == objects_.end() || entry->second.parent_id || !expected.dynamic || selected_.contains(expected.id))
        return false;
    auto& object = entry->second;
    const auto current = collision_prim(object, object);
    if (!current || !current->dynamic || current->type != expected.type ||
        current->mesh_asset != expected.mesh_asset || current->convex_mesh != expected.convex_mesh ||
        current->mesh_segments != expected.mesh_segments || current->hollow != expected.hollow ||
        current->path != expected.path || current->contact_material != expected.contact_material ||
        current->scale != expected.scale || current->gravity_modifier != expected.gravity_modifier ||
        current->buoyancy != expected.buoyancy || current->force != expected.force ||
        current->torque != expected.torque || current->hover != expected.hover ||
        current->move_target != expected.move_target ||
        current->look_at != expected.look_at || current->inertia_override != expected.inertia_override ||
        current->rotation_axis_locks != expected.rotation_axis_locks ||
        current->volume_detect != expected.volume_detect || current->phantom != expected.phantom ||
        current->density != expected.density || current->position != expected.position ||
        current->rotation != expected.rotation || current->velocity != expected.velocity ||
        current->angular_velocity != expected.angular_velocity || current->acceleration != expected.acceleration)
        return false;
    object.position = motion.position;
    object.rotation = motion.rotation;
    object.velocity = motion.velocity;
    object.angular_velocity = motion.angular_velocity;
    object.acceleration = motion.acceleration;
    object.changed = true;
    event_unlocked(WorldEventKind::TerseUpdated, object);
    return true;
}
std::vector<LandPrimGroup> World::land_prim_groups(bool include_temporary) const {
    constexpr std::uint32_t temporary_on_rez = 0x20000000u;
    std::lock_guard lock(mutex_);
    std::map<std::uint32_t, LandPrimGroup> groups;
    for (const auto& [id, object] : objects_) {
        if (!object.is_attachment && !object.parent_id &&
            (include_temporary || !(object.flags & temporary_on_rez))) {
            groups.emplace(object.local_id, LandPrimGroup{
                object.owner, object.group, object.position, object.local_id, 1, selected_.contains(id)});
        }
    }
    for (const auto& [id, object] : objects_) {
        if (object.is_attachment || !object.parent_id)
            continue;
        if (const auto root = groups.find(object.parent_id); root != groups.end()) {
            ++root->second.prims;
            root->second.selected |= selected_.contains(id);
        }
    }
    std::vector<LandPrimGroup> result;
    result.reserve(groups.size());
    for (const auto& [local_id, group] : groups)
        result.push_back(group);
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
    const auto root = object.parent_id && !object.is_attachment ? object.parent_id : object.local_id;
    const auto parent = by_local_.find(root);
    if (!include_attachments && parent != by_local_.end() && objects_.at(parent->second).is_attachment)
        return 0;
    std::int32_t count = 1;
    if (root)
        for (const auto& [part_id, part] : objects_)
            if (!part.is_attachment && part.parent_id == root)
                ++count;
    return count;
}
std::uint64_t World::assign_link_membership_unlocked(framework::Uuid root) {
    const auto entry = free_link_memberships_.find(root);
    if (entry == free_link_memberships_.end())
        return ++link_sequence_;
    auto& free = entry->second;
    const auto order = free.back();
    free.pop_back();
    if (free.empty())
        free_link_memberships_.erase(entry);
    return order;
}

std::vector<framework::Uuid> World::link_children_in_membership_order(framework::Uuid id) const {
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(id);
    if (root == objects_.end() || !root->second.local_id)
        return {};
    std::vector<const SceneObject*> children;
    for (const auto& [part_id, part] : objects_)
        if (!part.is_attachment && part.parent_id == root->second.local_id)
            children.push_back(&part);
    std::ranges::sort(children, {}, &SceneObject::link_membership_order);
    std::vector<framework::Uuid> result;
    result.reserve(children.size());
    for (const auto* child : children)
        result.push_back(child->id);
    return result;
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
    if (!root->second.local_id)
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
    if (entry == objects_.end())
        return 0;
    const auto& object = entry->second;
    const bool child = object.parent_id && !object.is_attachment;
    const auto parent = child ? object.parent_id : object.local_id;
    if (!parent)
        return 0;
    std::int32_t number = child ? 2 : 0;
    for (const auto& [part_id, part] : objects_) {
        if (part.is_attachment || part.parent_id != parent)
            continue;
        if (!child)
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
std::array<framework::Vector3, 2> World::bounding_box(framework::Uuid id) const {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return {};
    const auto& object = entry->second;
    const auto root_id = object.parent_id && !object.is_attachment ? object.parent_id : object.local_id;
    const auto local = by_local_.find(root_id);
    if (local == by_local_.end())
        return {};
    const auto& root = objects_.at(local->second);
    framework::Vector3 upper{root.scale.x * .5f, root.scale.y * .5f, root.scale.z * .5f};
    framework::Vector3 lower{-upper.x, -upper.y, -upper.z};
    for (const auto& [part_id, part] : objects_) {
        if (part.is_attachment || part.parent_id != root_id)
            continue;
        const auto x = framework::sdk_rotate_vector({1, 0, 0}, part.rotation);
        const auto y = framework::sdk_rotate_vector({0, 1, 0}, part.rotation);
        const auto z = framework::sdk_rotate_vector({0, 0, 1}, part.rotation);
        const framework::Vector3 half{part.scale.x * .5f, part.scale.y * .5f, part.scale.z * .5f};
        const auto extent = [&](float a, float b, float c) {
            return std::abs(a) * half.x + std::abs(b) * half.y + std::abs(c) * half.z;
        };
        const framework::Vector3 bounds{extent(x.x, y.x, z.x), extent(x.y, y.y, z.y), extent(x.z, y.z, z.z)};
        lower.x = std::min(lower.x, part.position.x - bounds.x);
        lower.y = std::min(lower.y, part.position.y - bounds.y);
        lower.z = std::min(lower.z, part.position.z - bounds.z);
        upper.x = std::max(upper.x, part.position.x + bounds.x);
        upper.y = std::max(upper.y, part.position.y + bounds.y);
        upper.z = std::max(upper.z, part.position.z + bounds.z);
    }
    return {lower, upper};
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
void World::set_searchable(framework::Uuid id, bool searchable) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& object = entry->second;
    constexpr std::uint32_t include_in_search = 0x8000u;
    if (searchable)
        object.flags |= include_in_search;
    else
        object.flags &= ~include_in_search;
    mark_linkset_changed_unlocked(object);
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
void World::set_script_access_pin(framework::Uuid id, std::int32_t pin) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry != objects_.end() && entry->second.script_access_pin != pin) {
        entry->second.script_access_pin = pin;
        mark_linkset_changed_unlocked(entry->second);
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
void World::set_pass_collisions(framework::Uuid id, bool pass) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry != objects_.end() && entry->second.pass_collisions != pass) {
        entry->second.pass_collisions = pass;
        mark_linkset_changed_unlocked(entry->second);
    }
}
void World::set_pass_touches(framework::Uuid id, bool pass) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry != objects_.end() && entry->second.pass_touches != pass) {
        entry->second.pass_touches = pass;
        mark_linkset_changed_unlocked(entry->second);
    }
}
void World::set_die_at_edge(framework::Uuid root, bool enabled) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(root);
    if (entry != objects_.end() && entry->second.die_at_edge != enabled) {
        entry->second.die_at_edge = enabled;
        mark_linkset_changed_unlocked(entry->second);
    }
}
void World::set_return_at_edge(framework::Uuid root, bool enabled) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(root);
    if (entry != objects_.end())
        entry->second.return_at_edge = enabled;
}
void World::set_sandbox(framework::Uuid part, framework::Uuid root, bool enabled) {
    std::lock_guard lock(mutex_);
    const auto host = objects_.find(part);
    const auto entry = objects_.find(root);
    if (host == objects_.end() || entry == objects_.end())
        return;
    host->second.sandbox_origin = entry->second.position;
    entry->second.sandboxed = enabled;
}
void World::enable_cast_shadows(framework::Uuid id) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end() || (entry->second.flags & cast_shadows_flag))
        return;
    entry->second.flags |= cast_shadows_flag;
}
void World::set_grab_block(framework::Uuid id, bool whole_linkset, bool enabled) {
    std::lock_guard lock(mutex_);
    const auto part = objects_.find(id);
    if (part == objects_.end())
        return;
    if (whole_linkset)
        part->second.block_grab_override = enabled;
    else
        part->second.block_grab = enabled;
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
void World::set_linkset_damage(framework::Uuid part, float damage) {
    std::lock_guard lock(mutex_);
    if (const auto root = objects_.find(linkset_root_id_unlocked(part)); root != objects_.end())
        root->second.damage = damage;
}
void World::set_collision_sound(framework::Uuid id, framework::Uuid asset, float volume,
                                bool default_volume_override) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& object = entry->second;
    object.collision_sound = asset;
    object.collision_sound_volume_override = default_volume_override;
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
bool World::set_render_materials(const SceneObject& previous, std::vector<std::uint8_t> extra_params,
                                 std::vector<std::uint8_t> overrides, std::int32_t script_changes) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(previous.id);
    if (entry == objects_.end())
        return false;
    auto& part = entry->second;
    if (part.extra_params != previous.extra_params || part.material_overrides != previous.material_overrides)
        return false;
    part.extra_params = std::move(extra_params);
    part.material_overrides = std::move(overrides);
    mark_linkset_changed_unlocked(part);
    event_unlocked(WorldEventKind::MaterialsUpdated, part, script_changes);
    return true;
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
bool World::set_object_animations(framework::Uuid id, std::vector<std::uint8_t> animations) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    auto& part = entry->second;
    part.object_animations = std::move(animations);
    mark_linkset_changed_unlocked(part);
    event_unlocked(WorldEventKind::AnimationsUpdated, part);
    return true;
}
bool World::set_dynamic_attributes(framework::Uuid id, std::string_view expected, std::string xml) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end() || entry->second.dynamic_attributes_xml != expected)
        return false;
    entry->second.dynamic_attributes_xml = std::move(xml);
    mark_linkset_changed_unlocked(entry->second);
    return true;
}
bool World::set_legacy_material_textures(const SceneObject& previous, std::vector<std::uint8_t> textures,
                                         bool publish_update) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(previous.id);
    if (entry == objects_.end())
        return false;
    auto& part = entry->second;
    if (part.texture_entry != previous.texture_entry || part.owner != previous.owner ||
        part.group != previous.group || part.parent_id != previous.parent_id)
        return false;
    part.texture_entry = std::move(textures);
    mark_linkset_changed_unlocked(part);
    constexpr std::int32_t changed_texture = 16;
    if (publish_update)
        event_unlocked(WorldEventKind::Updated, part, changed_texture);
    return true;
}
void World::set_texture_animation(framework::Uuid id, const std::vector<std::uint8_t>& bytes) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& part = entry->second;
    part.texture_animation = bytes;
    mark_linkset_changed_unlocked(part);
    event_unlocked(WorldEventKind::Updated, part);
}
void World::set_particle_system(framework::Uuid id, std::vector<std::uint8_t> bytes,
                                std::optional<float> timed_age, std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    auto& part = entry->second;
    if (particles_expired_unlocked(id, now))
        part.particle_system.clear();
    particle_expiry_.erase(id);
    if (bytes.empty() && part.particle_system.empty())
        return;
    const bool changed = bytes.empty() || !timed_age || *timed_age != 0;
    if (!bytes.empty() && timed_age && *timed_age > .0001 && std::isfinite(*timed_age))
        particle_expiry_.emplace(id, now.time_since_epoch() + std::chrono::duration<double>(*timed_age + .1));
    part.particle_system = std::move(bytes);
    if (changed)
        mark_linkset_changed_unlocked(part);
    event_unlocked(WorldEventKind::Updated, part);
}
void World::expire_particle_systems(std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(mutex_);
    for (auto expiry = particle_expiry_.begin(); expiry != particle_expiry_.end();) {
        if (now.time_since_epoch() <= expiry->second) {
            ++expiry;
            continue;
        }
        if (auto part = objects_.find(expiry->first); part != objects_.end())
            part->second.particle_system.clear();
        expiry = particle_expiry_.erase(expiry);
    }
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
bool World::update_face_media(const SceneObject& previous, std::string xml, std::string version,
                              std::vector<std::uint8_t> textures) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(previous.id);
    if (entry == objects_.end())
        return false;
    auto& object = entry->second;
    if (object.media_url != previous.media_url || object.face_media_xml != previous.face_media_xml ||
        object.texture_entry != previous.texture_entry || object.shape.face_count() != previous.shape.face_count())
        return false;

    object.face_media_xml = std::move(xml);
    object.media_url = std::move(version);
    object.texture_entry = std::move(textures);
    mark_linkset_changed_unlocked(object);
    constexpr std::int32_t changed_media = 2048;
    event_unlocked(WorldEventKind::Updated, object, changed_media);
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
bool World::link_prim(framework::Uuid id, framework::Uuid root_id, int physical_capacity, bool insert) {
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
    constexpr std::uint32_t physics = 1u;
    const auto in_linksets = [&](const SceneObject& part) {
        return part.id == id || part.id == root_id ||
               (!part.is_attachment && (part.parent_id == child.local_id || part.parent_id == root.local_id));
    };
    if ((root.flags & physics) && physical_capacity > 0) {
        const auto shapes = std::ranges::count_if(objects_, [&](const auto& entry) {
            return in_linksets(entry.second) && entry.second.physics.shape != 1;
        });
        if (shapes > physical_capacity) {
            for (auto& [part_id, part] : objects_) {
                if (in_linksets(part)) {
                    part.flags &= ~physics;
                    part.changed = true;
                }
            }
        }
    }
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
        part.link_membership_order = assign_link_membership_unlocked(root.id);
        part.changed = true;
        event_unlocked(WorldEventKind::Updated, part, changed_link);
    };
    root.changed = true;
    event_unlocked(WorldEventKind::Updated, root, changed_link);
    std::vector<SceneObject*> existing_children;
    for (auto& [part_id, part] : objects_) {
        if (!part.is_attachment && part.parent_id == root.local_id) {
            if (insert)
                existing_children.push_back(&part);
            else
                event_unlocked(WorldEventKind::Updated, part, changed_link);
        }
    }
    child.sale_type = 0;
    child.sale_price = 10;
    temporary_expiry_.erase(child.id);
    free_link_memberships_.erase(child.id);
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
    std::ranges::sort(existing_children, {}, &SceneObject::link_order);
    for (auto* part : existing_children) {
        part->link_order = ++link_sequence_;
        part->changed = true;
        event_unlocked(WorldEventKind::Updated, *part, changed_link);
    }
    return true;
}
bool World::delink_child(framework::Uuid id, framework::Uuid expected_root, bool notify_remaining) {
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
    if (child.physics.shape == no_physics_shape)
        child.physics.shape = default_physics_shape(child);
    child.attached_to_backup = true;
    free_link_memberships_[root.id].push_back(child.link_membership_order);
    child.link_order = child.link_membership_order = 0;
    if (child.flags & 0x20000000u)
        temporary_expiry_[child.id] = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    child.changed = root.changed = true;
    constexpr std::int32_t changed_link = 32;
    event_unlocked(WorldEventKind::Updated, child, changed_link);
    event_unlocked(WorldEventKind::Updated, root, notify_remaining ? changed_link : 0);
    for (const auto& [part_id, part] : objects_)
        if (!part.is_attachment && part.parent_id == root.local_id)
            event_unlocked(WorldEventKind::Updated, part, notify_remaining ? changed_link : 0);
    return true;
}
framework::Uuid World::linkset_root_id_unlocked(framework::Uuid id) const {
    const auto part = objects_.find(id);
    if (part == objects_.end())
        return {};
    if (part->second.is_attachment || !part->second.parent_id)
        return id;
    const auto root = by_local_.find(part->second.parent_id);
    return root != by_local_.end() ? root->second : framework::Uuid{};
}
void World::set_prim_buoyancy(framework::Uuid id, float buoyancy) {
    if (!std::isfinite(buoyancy))
        return;
    std::lock_guard lock(mutex_);
    const auto part = objects_.find(linkset_root_id_unlocked(id));
    if (part != objects_.end() && !part->second.is_attachment)
        part->second.buoyancy = buoyancy;
}
void World::set_prim_force(framework::Uuid id, framework::Vector3 force) {
    if (!std::isfinite(force.x) || !std::isfinite(force.y) || !std::isfinite(force.z))
        return;
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(linkset_root_id_unlocked(id));
    if (root != objects_.end())
        root->second.force = force;
}
framework::Vector3 World::prim_force(framework::Uuid id) const {
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(linkset_root_id_unlocked(id));
    return root != objects_.end() ? root->second.force : framework::Vector3{};
}
void World::set_prim_torque(framework::Uuid id, framework::Vector3 torque, bool local) {
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(linkset_root_id_unlocked(id));
    if (root == objects_.end())
        return;
    if (local) {
        const auto rotation = root->second.rotation;
        const auto world_rotation = root->second.parent_id
            ? framework::sdk_multiply_quaternion(rotation, rotation) : rotation;
        torque = framework::sdk_rotate_vector(torque, world_rotation);
    }
    if (std::isfinite(torque.x) && std::isfinite(torque.y) && std::isfinite(torque.z))
        root->second.torque = torque;
}
framework::Vector3 World::prim_torque(framework::Uuid id) const {
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(linkset_root_id_unlocked(id));
    return root != objects_.end() ? root->second.torque : framework::Vector3{};
}
void World::set_rotation_axes(framework::Uuid id, std::uint8_t axes, bool enabled) {
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(linkset_root_id_unlocked(id));
    if (root == objects_.end())
        return;
    auto& object = root->second;
    const auto previous = object.rotation_axis_locks;
    const auto updated = static_cast<std::uint8_t>(enabled ? previous & ~axes : previous | axes);
    if (updated == previous)
        return;
    object.rotation_axis_locks = updated;
    if (updated && (object.flags & 1u))
        object.angular_velocity = {};
    object.changed = true;
}

void World::set_prim_inertia(framework::Uuid id, std::optional<framework::PrimInertiaOverride> inertia) {
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(linkset_root_id_unlocked(id));
    if (root == objects_.end() || root->second.inertia_override == inertia)
        return;
    root->second.inertia_override = std::move(inertia);
    root->second.changed = true;
}

void World::set_prim_look_at(framework::Uuid id, framework::PrimLookAt target) {
    const auto rotation = target.rotation;
    if (!std::isfinite(target.strength) || !std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
        !std::isfinite(rotation.z) || !std::isfinite(rotation.w))
        return;
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(linkset_root_id_unlocked(id));
    if (root == objects_.end() || root->second.is_attachment)
        return;
    auto& object = root->second;
    if (target.active()) {
        if (object.flags & 1u)
            object.look_at = target;
    } else {
        if (object.look_at.active() || target.strength > 0.f)
            object.angular_velocity = {};
        object.look_at = {};
    }
}
void World::set_prim_move_target(framework::Uuid id, framework::PrimMoveTarget target) {
    const auto position = target.position;
    if (!std::isfinite(target.tau) ||
        (target.active() && (!std::isfinite(position.x) || !std::isfinite(position.y) ||
                             !std::isfinite(position.z))))
        return;
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(linkset_root_id_unlocked(id));
    if (root != objects_.end() && !root->second.is_attachment)
        root->second.move_target = target.active() ? target : framework::PrimMoveTarget{};
}
void World::set_prim_hover(framework::Uuid id, framework::PrimHover hover) {
    if (!std::isfinite(hover.height) || !std::isfinite(hover.tau))
        return;
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(linkset_root_id_unlocked(id));
    if (root != objects_.end() && !root->second.is_attachment)
        root->second.hover = hover.height == 0.f ? framework::PrimHover{} : hover;
}
void World::set_prim_velocity(framework::Uuid id, framework::Vector3 velocity, bool local, bool angular) {
    std::lock_guard lock(mutex_);
    const auto part = objects_.find(id);
    const auto root = objects_.find(linkset_root_id_unlocked(id));
    if (part == objects_.end() || root == objects_.end() || root->second.is_attachment)
        return;
    auto& object = root->second;
    const auto collider = collision_prim(object, object);
    if (!collider || !collider->dynamic)
        return;
    if (local) {
        const auto rotation = part == root ? object.rotation
            : framework::sdk_multiply_quaternion(object.rotation, part->second.rotation);
        velocity = framework::sdk_rotate_vector(velocity, rotation);
    }
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
        return;
    if (!angular) {
        const float speed_squared = velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z;
        if (speed_squared > 100000.f) {
            const float scale = 100.f / std::sqrt(speed_squared);
            velocity = {velocity.x * scale, velocity.y * scale, velocity.z * scale};
        }
    }
    auto& current = angular ? object.angular_velocity : object.velocity;
    if (current == velocity)
        return;
    current = velocity;
    event_unlocked(WorldEventKind::Updated, object);
}
std::optional<PrimPhysicsProperties> World::update_extra_physics(framework::Uuid id,
                                                                 PrimPhysicsProperties requested,
                                                                 bool update_shape) {
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
    if (update_shape) {
        const bool use_default = requested.shape > convex_hull_shape || (root && requested.shape == no_physics_shape);
        current.shape = use_default ? default_physics_shape(part) : requested.shape;
    }
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
bool World::set_script_permission_mask(framework::Uuid id, std::int32_t selector, std::int32_t value) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;
    auto& part = entry->second;
    if (!framework::set_lsl_permission_mask(
            {&part.base_mask, &part.owner_mask, &part.group_mask, &part.everyone_mask, &part.next_mask},
            selector, value))
        return false;
    mark_linkset_changed_unlocked(part);
    return true;
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
static void transfer_part_owner(SceneObject& part, framework::Uuid owner, framework::Uuid group,
                                bool apply_next_permissions) {
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
    part.changed = true;
}

bool World::transfer_temporary_attachment(const SceneObject& expected, framework::Uuid owner, framework::Uuid group,
                                          bool apply_next_permissions) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(expected.id);
    if (entry == objects_.end() || entry->second.owner != expected.owner ||
        entry->second.is_attachment || entry->second.parent_id)
        return false;
    auto& root = entry->second;
    root.sale_type = 0;
    root.sale_price = 10;
    for (auto& [id, part] : objects_) {
        if (id != root.id && (part.is_attachment || !root.local_id || part.parent_id != root.local_id))
            continue;
        transfer_part_owner(part, owner, group, apply_next_permissions);
        event_unlocked(WorldEventKind::Updated, part);
    }
    return true;
}

bool World::sell_original(const SceneObject& expected, framework::Uuid owner, framework::Uuid group,
                          bool apply_next_permissions, bool propagate_permissions, bool included_with_parcel) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(expected.id);
    if (entry == objects_.end())
        return false;
    auto& root = entry->second;
    const bool listed_original = root.sale_type == 1 && root.sale_price == 0;
    if (root.owner != expected.owner ||
        (included_with_parcel ? root.is_attachment : !listed_original) ||
        (!root.is_attachment && root.parent_id))
        return false;
    for (auto& [id, part] : objects_) {
        if (id != root.id && (part.is_attachment || !root.local_id || part.parent_id != root.local_id))
            continue;
        transfer_part_owner(part, owner, group, apply_next_permissions);
        if (id == root.id) {
            part.sale_type = 0;
            part.sale_price = 10;
            part.click_action = 0;
        }
        constexpr std::int32_t changed_owner = 128;
        event_unlocked(WorldEventKind::Updated, part,
                       propagate_permissions || id == root.id ? changed_owner : 0);
    }
    return true;
}
void World::set_group_flags(framework::Uuid id, bool physical, bool temporary, bool phantom,
                            std::optional<bool> volume_detect) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return;
    const auto& root = entry->second;
    constexpr std::uint32_t physics = 1u, phantom_flag = 0x400u, temporary_on_rez = 0x20000000u;
    const bool detect = volume_detect.value_or((root.flags & prim_flag_volume_detect) != 0);
    const auto flags = (physical ? physics : 0u) | (temporary ? temporary_on_rez : 0u);
    constexpr auto group_flags = physics | phantom_flag | temporary_on_rez | prim_flag_volume_detect;
    if (!root.is_attachment && !root.parent_id) {
        if (temporary && !(root.flags & temporary_on_rez))
            temporary_expiry_[id] = std::chrono::steady_clock::now() + std::chrono::minutes(1);
        if (!temporary)
            temporary_expiry_.erase(id);
    }
    for (auto& [part_id, part] : objects_) {
        if (part_id != id && (!root.local_id || part.is_attachment || part.parent_id != root.local_id))
            continue;
        const bool was_detecting = (part.flags & prim_flag_volume_detect) != 0;
        const bool part_phantom = detect || (phantom && !was_detecting);
        const auto updated = (part.flags & ~group_flags) | flags |
                             (part_phantom ? phantom_flag : 0u) | (detect ? prim_flag_volume_detect : 0u);
        if (updated == part.flags)
            continue;
        part.flags = updated;
        part.changed = true;
        event_unlocked(WorldEventKind::Updated, part);
    }
}
void World::set_part_volume_detect(framework::Uuid id, bool enabled) {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end() || entry->second.is_attachment)
        return;
    auto& part = entry->second;
    const auto parent = by_local_.find(part.parent_id);
    if (parent == by_local_.end())
        return;
    const auto& root = objects_.at(parent->second);
    if (root.is_attachment || (root.flags & prim_flag_volume_detect))
        return;

    constexpr std::uint32_t phantom = 0x400u;
    auto flags = part.flags;
    if (enabled)
        flags |= prim_flag_volume_detect | phantom;
    else if (flags & prim_flag_volume_detect)
        flags &= ~(prim_flag_volume_detect | phantom);
    else
        flags = (flags & ~phantom) | (root.flags & phantom);
    if (flags != part.flags) {
        part.flags = flags;
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
    if (root.local_id)
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
float World::group_scale_limit(framework::Uuid id, float limit, bool maximum) const {
    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return 1.f;

    const auto& root = entry->second;
    float dimension = maximum ? std::min({root.scale.x, root.scale.y, root.scale.z})
                              : std::max({root.scale.x, root.scale.y, root.scale.z});
    for (const auto& [part_id, part] : objects_) {
        if (part_id != id && (part.is_attachment || part.parent_id != root.local_id))
            continue;
        dimension = maximum ? std::max({dimension, part.scale.x, part.scale.y, part.scale.z})
                            : std::min({dimension, part.scale.x, part.scale.y, part.scale.z});
    }
    if (maximum)
        return dimension >= limit ? 1.f : limit / (dimension + .001f);
    if (dimension <= limit)
        return 1.f;
    return limit / (dimension > .002f ? dimension - .001f : dimension);
}

bool World::resize_group_uniform(framework::Uuid id, double factor, float minimum, float maximum) {
    if (!std::isfinite(factor) || factor < 1.e-6 || factor > 1.e6)
        return false;

    std::lock_guard lock(mutex_);
    const auto entry = objects_.find(id);
    if (entry == objects_.end())
        return false;

    auto& root = entry->second;
    std::vector<SceneObject*> parts{&root};
    for (auto& [part_id, part] : objects_)
        if (part_id != id && !part.is_attachment && part.parent_id == root.local_id)
            parts.push_back(&part);

    for (const auto* part : parts) {
        for (const float dimension : {part->scale.x, part->scale.y, part->scale.z}) {
            const float scaled = static_cast<float>(dimension * factor);
            if (scaled < minimum || scaled > maximum)
                return false;
        }
    }

    const float scale = static_cast<float>(factor);
    const auto multiply = [scale](framework::Vector3 vector) {
        return framework::Vector3{vector.x * scale, vector.y * scale, vector.z * scale};
    };
    constexpr std::int32_t changed_scale = 8;
    for (auto* part : parts) {
        part->scale = multiply(part->scale);
        if (part != &root)
            part->position = multiply(part->position);
        part->changed = true;
        event_unlocked(WorldEventKind::Updated, *part, part == &root ? changed_scale : 0);
    }
    return true;
}

bool World::delink_root(framework::Uuid id, bool membership_order) {
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
    const auto order = membership_order ? &SceneObject::link_membership_order : &SceneObject::link_order;
    std::ranges::sort(children, {}, order);
    auto& replacement = *children.front();
    auto& free = free_link_memberships_[root.id];
    for (const auto* child : children)
        free.push_back(child->link_membership_order);
    const auto origin = replacement.position;
    const auto rotation = replacement.rotation;
    const framework::Quaternion inverse{-rotation.x, -rotation.y, -rotation.z, rotation.w};
    const auto offset = framework::sdk_rotate_vector(origin, root.rotation);
    replacement.position = {root.position.x + offset.x, root.position.y + offset.y,
                            root.position.z + offset.z};
    replacement.rotation = framework::sdk_multiply_quaternion(root.rotation, rotation);
    replacement.parent_id = 0;
    if (replacement.physics.shape == no_physics_shape)
        replacement.physics.shape = default_physics_shape(replacement);
    replacement.rezzed_at = root.rezzed_at;
    replacement.attached_to_backup = true;
    replacement.link_order = replacement.link_membership_order = 0;
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
        if (membership_order)
            child->link_order = child->link_membership_order = ++link_sequence_;
        child->rezzed_at = root.rezzed_at;
        child->changed = true;
        event_unlocked(WorldEventKind::Updated, *child, changed_link);
    }
    event_unlocked(WorldEventKind::Updated, root, changed_link);
    return true;
}
void World::schedule_linkset_update(framework::Uuid id, std::int32_t script_changes) {
    std::lock_guard lock(mutex_);
    const auto root = objects_.find(id);
    if (root == objects_.end())
        return;
    root->second.changed = true;
    event_unlocked(WorldEventKind::Updated, root->second, script_changes);
    for (const auto& [part_id, part] : objects_) {
        if (!part.is_attachment && part.parent_id == root->second.local_id && part.parent_id)
            event_unlocked(WorldEventKind::Updated, part, script_changes);
    }
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
bool World::derez_script_object(framework::Uuid source, framework::Uuid target, bool temporary,
                               std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(mutex_);
    const auto host = objects_.find(source);
    const auto destination = objects_.find(target);
    if (host == objects_.end() || destination == objects_.end())
        return false;

    const auto& source_part = host->second;
    auto& root = destination->second;
    if (root.is_attachment || root.parent_id || root.owner != source_part.owner)
        return false;

    auto source_root = source;
    if (source_part.parent_id && !source_part.is_attachment) {
        const auto parent = by_local_.find(source_part.parent_id);
        if (parent == by_local_.end())
            return false;
        source_root = parent->second;
    }
    if (source_root == target || root.rezzer != source_root)
        return false;
    if (!temporary)
        return remove_unlocked(target);

    constexpr std::uint32_t temporary_on_rez = 0x20000000u;
    if (!(root.flags & temporary_on_rez)) {
        root.flags |= temporary_on_rez;
        temporary_expiry_[target] = now + std::chrono::minutes(1);
    }
    return true;
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
    particle_expiry_.erase(id);
    auto i = objects_.find(id);
    if (i == objects_.end())
        return false;
    auto object = i->second;
    free_link_memberships_.erase(id);
    if (!object.is_attachment && object.parent_id) {
        if (const auto root = by_local_.find(object.parent_id); root != by_local_.end())
            free_link_memberships_[root->second].push_back(object.link_membership_order);
    }
    if (!send_update) {
        std::erase_if(events_, [&](const auto& event) {
            return event.object.id == id ||
                   ((object.is_attachment || !object.parent_id) && !event.object.is_attachment &&
                    event.object.parent_id == object.local_id);
        });
    }
    if (object.is_attachment || object.parent_id == 0) {
        for (auto child = objects_.begin(); child != objects_.end();) {
            const auto& part = child->second;
            if (!part.is_attachment && part.parent_id == object.local_id && part.parent_id != 0) {
                if (send_update)
                    event_unlocked(WorldEventKind::Removed, part);
                by_local_.erase(part.local_id);
                selected_.erase(part.id);
                particle_expiry_.erase(part.id);
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
    const auto parent = by_local_.find(object.parent_id);
    const bool worn = object.is_attachment ||
        (parent != by_local_.end() && objects_.at(parent->second).is_attachment);
    // Restoring or starting scripts refreshes these flags. The task registry tracks actual inventory edits.
    if (!worn)
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
