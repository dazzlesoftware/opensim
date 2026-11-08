// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/terrain.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include "opensim/framework/prim_geometry.hpp"
#include <atomic>
#include <array>
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
namespace opensim::scene {
struct PrimShape {
    int face_count() const;
    std::uint8_t pcode = 9, state = 0, path_curve = 16, profile_curve = 1;
    std::uint16_t path_begin = 0, path_end = 0, profile_begin = 0, profile_end = 0, profile_hollow = 0;
    std::uint8_t path_scale_x = 100, path_scale_y = 100, path_shear_x = 0, path_shear_y = 0, path_revolutions = 0;
    std::int8_t path_twist = 0, path_twist_begin = 0, path_radius_offset = 0, path_taper_x = 0, path_taper_y = 0,
                path_skew = 0;
};
struct PrimPhysicsProperties {
    std::uint8_t shape = 0;
    float density = 1000.f, friction = .6f, restitution = .5f, gravity = 1.f;
    bool operator==(const PrimPhysicsProperties&) const = default;
};
inline constexpr std::uint32_t prim_flag_volume_detect = 1u << 14;

struct PhysicalPrimPart {
    framework::Vector3 scale, offset;
    framework::Quaternion rotation;
    float density;
    framework::PrimType type;
    std::uint8_t mesh_segments;
    float hollow;
    framework::LinearPrimPath path;
    framework::Uuid mesh_asset;
    bool convex_mesh = false;
    framework::PrimContactMaterial contact_material;
    framework::Uuid id;
    bool volume_detect = false;
    bool phantom = false;
    bool operator==(const PhysicalPrimPart&) const = default;
};
struct PhysicalPrim {
    framework::Uuid id;
    framework::Vector3 position, scale, velocity, angular_velocity, acceleration;
    framework::Quaternion rotation;
    float density;
    bool dynamic;
    framework::PrimType type;
    std::uint8_t mesh_segments;
    float hollow;
    framework::LinearPrimPath path;
    std::vector<PhysicalPrimPart> children;
    framework::Uuid mesh_asset;
    bool convex_mesh = false;
    framework::PrimContactMaterial contact_material;
    float gravity_modifier = 1.f;
    float buoyancy = 0.f;
    framework::Vector3 force, torque;
    framework::PrimHover hover;
    framework::PrimMoveTarget move_target;
    framework::PrimLookAt look_at;
    bool volume_detect = false;
    framework::Uuid root_id;
    std::uint8_t rotation_axis_locks = 0;
    std::optional<framework::PrimInertiaOverride> inertia_override;
    bool phantom = false;
    bool operator==(const PhysicalPrim&) const = default;
};
// Single-root object projection for the scene owner. Full linked parts,
// permissions/effects, physics, inventory/scripts and storage are not implied.
struct SceneObject {
    SceneObject();
    framework::Uuid id, owner, creator, group, rezzer, last_owner;
    std::uint32_t local_id = 0, parent_id = 0, flags = 0;
    std::uint64_t link_order = 0;
    std::string name = "Object", description, touch_name, sit_name;
    std::string floating_text, media_url;
    // OpenSimulator stores the per-face media list as an OSMedia XML value.
    std::string face_media_xml;
    std::uint32_t text_argb = 0;
    framework::Uuid looped_sound;
    std::optional<framework::PrimInertiaOverride> inertia_override;
    std::uint8_t rotation_axis_locks = 0;
    framework::Uuid collision_sound;
    float collision_sound_volume = 0;
    float damage = -1.f;
    bool collision_sound_volume_override = false;
    float looped_sound_gain = 0;
    float sound_radius = 0;
    std::uint8_t sound_flags = 0;
    bool sound_queueing = false;
    std::int32_t creation_date = 0, sale_price = 0;
    std::chrono::system_clock::time_point rezzed_at;
    std::uint8_t sale_type = 0;
    std::uint8_t click_action = 0;
    bool pass_touches = false;
    bool pass_collisions = false;
    framework::Vector3 sit_target_position{};
    float sit_active_range = 0;
    framework::Vector3 stand_target{};
    framework::Vector3 camera_eye_offset{}, camera_at_offset{};
    bool force_mouselook = false;
    framework::Quaternion sit_target_orientation{0, 0, 0, 1};
    std::uint8_t material = 3;
    std::array<std::int32_t, 5> pay_prices{-2, -2, -2, -2, -2};
    std::uint32_t base_mask = 0x9e000, owner_mask = 0x9e000, group_mask = 0, everyone_mask = 0,
                  next_mask = 0x82000, category = 0;
    framework::Uuid from_inventory_item;
    framework::Uuid from_inventory_folder;
    framework::Uuid attachment_source_asset;
    framework::Vector3 saved_attachment_position;
    std::uint8_t last_attachment_point = 0;

    framework::Vector3 position, scale{.5f, .5f, .5f}, velocity, angular_velocity, acceleration;
    framework::Quaternion rotation{0, 0, 0, 1};
    PrimShape shape;
    PrimPhysicsProperties physics;
    float buoyancy = 0.f;
    framework::Vector3 force, torque;
    framework::PrimHover hover;
    framework::PrimMoveTarget move_target;
    framework::PrimLookAt look_at;
    std::vector<std::uint8_t> texture_entry;
    std::vector<std::uint8_t> texture_animation;
    std::vector<std::uint8_t> extra_params{0};
    std::vector<std::uint8_t> object_animations;
    std::vector<std::uint8_t> material_overrides;
    // The original OpenSimulator linkset-data binary payload belongs to the root.
    std::vector<std::uint8_t> linkset_data;
    std::string dynamic_attributes_xml;
    std::optional<std::int32_t> empty_linkset_data_limit;
    std::optional<std::string> rez_start_string;
    bool is_attachment = false, attached_to_backup = false, changed = false, inventory_empty = true;
    bool die_at_edge = false;
    bool return_at_edge = false;
    bool sandboxed = false;
    bool block_grab = false, block_grab_override = false;
    framework::Vector3 sandbox_origin;
    static SceneObject primitive(framework::Uuid owner, framework::Vector3 position,
                                 framework::Vector3 scale = {.5f, .5f, .5f});
};
enum class WorldEventKind { Added, Updated, Removed, TerseUpdated, MaterialsUpdated, AnimationsUpdated };
struct LandPrimGroup {
    framework::Uuid owner, group;
    framework::Vector3 position;
    std::uint32_t root_local_id;
    std::int32_t prims;
    bool selected;
};
struct WorldEvent {
    WorldEventKind kind;
    SceneObject object;
    std::uint64_t sequence = 0;
    std::int32_t script_changes = 0;
};
class World {
public:
    explicit World(framework::Uuid region = {}, std::uint32_t width = 256, std::uint32_t height = 256,
                   std::uint32_t last_local_id = 720000);
    framework::Uuid region_id() const {
        return region_;
    }
    Terrain& terrain() {
        return terrain_;
    }
    const Terrain& terrain() const {
        return terrain_;
    }
    // Shared with avatar IDs. SceneBase uses unchecked Interlocked.Increment;
    // uint32 wrap, including zero, is deliberate source behavior.
    std::uint32_t allocate_local_id();
    std::optional<SceneObject> add(SceneObject object, bool attach_to_backup = true, bool send_update = true);
    bool add_crossing_linkset(std::vector<SceneObject> parts, std::uint32_t wearer_local_id = 0);
    bool drop_attachment(framework::Uuid root, std::uint32_t wearer_local_id, framework::Vector3 position);
    bool attach_from_ground(framework::Uuid root, std::uint32_t wearer_local_id, std::uint8_t point,
                            framework::Vector3 offset, framework::Uuid item, framework::Uuid folder,
                            framework::Uuid asset);
    std::optional<SceneObject> object(framework::Uuid id) const;
    std::optional<SceneObject> object(std::uint32_t local_id) const;
    std::vector<SceneObject> objects() const;
    std::vector<PhysicalPrim> collision_prims() const;
    bool apply_physical_prim_motion(const PhysicalPrim& expected, const PhysicalPrim& motion);
    std::vector<LandPrimGroup> land_prim_groups(bool include_temporary = false) const;
    std::vector<SceneObject> attachments(std::uint32_t parent) const;
    std::int32_t link_number(framework::Uuid id) const;
    framework::Uuid link_key(framework::Uuid id, std::int32_t number) const;
    framework::Vector3 geometric_center(framework::Uuid id) const;
    std::array<framework::Vector3, 2> bounding_box(framework::Uuid id) const;
    std::int32_t prim_count(framework::Uuid id, bool include_attachments) const;
    std::size_t size() const;
    bool update_transform(framework::Uuid id, framework::Vector3 position, framework::Quaternion rotation,
                          framework::Vector3 scale);
    bool patch_transform(framework::Uuid id, std::optional<framework::Vector3> position,
                         std::optional<framework::Quaternion> rotation, std::optional<framework::Vector3> scale,
                         bool preserve_children = false);
    bool update_object_text(framework::Uuid id, bool description, std::string value);
    bool update_click_action(framework::Uuid id, std::uint8_t action, bool broadcast = false);
    void set_searchable(framework::Uuid id, bool searchable);
    void set_pass_touches(framework::Uuid id, bool pass);
    void set_pass_collisions(framework::Uuid id, bool pass);
    void set_die_at_edge(framework::Uuid root, bool enabled);
    void set_return_at_edge(framework::Uuid root, bool enabled);
    void set_sandbox(framework::Uuid part, framework::Uuid root, bool enabled);
    static constexpr std::uint32_t cast_shadows_flag = 1u << 23;
    void enable_cast_shadows(framework::Uuid part);
    void set_grab_block(framework::Uuid id, bool whole_linkset, bool enabled);
    void set_sit_target(framework::Uuid id, framework::Vector3 position, framework::Quaternion orientation);
    void set_sit_active_range(framework::Uuid id, float range);
    void set_stand_target(framework::Uuid id, framework::Vector3 offset);
    void set_seat_camera(framework::Uuid id, std::optional<framework::Vector3> eye,
                         std::optional<framework::Vector3> at, std::optional<bool> mouselook);
    void set_allowed_drop(framework::Uuid id, bool allowed);
    bool update_shape(framework::Uuid id, PrimShape shape);
    bool update_texture(framework::Uuid id, const std::vector<std::uint8_t>& bytes);
    void set_texture_animation(framework::Uuid id, const std::vector<std::uint8_t>& bytes);
    bool update_face_media(const SceneObject& previous, std::string xml, std::string version,
                           std::vector<std::uint8_t> textures);
    std::optional<std::uint8_t> update_material(framework::Uuid id, std::uint8_t material);
    void update_extra_param(framework::Uuid id, std::uint16_t type, bool enabled,
                            const std::vector<std::uint8_t>& data);
    bool set_render_materials(const SceneObject& previous, std::vector<std::uint8_t> extra_params,
                              std::vector<std::uint8_t> overrides);
    bool set_legacy_material_textures(const SceneObject& previous, std::vector<std::uint8_t> textures,
                                      bool publish_update = true);
    bool set_dynamic_attributes(framework::Uuid id, std::string_view expected, std::string xml);
    bool set_object_animations(framework::Uuid id, std::vector<std::uint8_t> animations);
    void set_floating_text(framework::Uuid id, std::string text, std::uint32_t argb);
    void set_interaction_label(framework::Uuid id, bool sit, std::string text);
    void set_looped_sound(framework::Uuid id, framework::Uuid asset, float gain, float radius,
                          std::uint8_t sync_flags);
    void set_sound_radius(framework::Uuid id, float radius);
    void set_sound_queueing(framework::Uuid id, bool queue);
    void set_collision_sound(framework::Uuid id, framework::Uuid asset, float volume,
                             bool default_volume_override = false);
    void set_pay_prices(framework::Uuid id, const std::array<std::int32_t, 5>& prices);
    std::optional<SceneObject> prepare_attached_sound(framework::Uuid id, float default_radius);
    bool set_selected(framework::Uuid id, bool selected);
    bool update_angular_velocity(framework::Uuid id, framework::Vector3 angular_velocity,
                                 bool full_refresh = false);
    bool update_motion(framework::Uuid id, framework::Vector3 velocity, framework::Vector3 angular_velocity,
                       framework::Vector3 acceleration);
    bool remove(framework::Uuid id, bool send_update = true);
    void publish_added_linkset(framework::Uuid root);
    bool delink_child(framework::Uuid id, framework::Uuid expected_root);
    bool delink_root(framework::Uuid id);
    void set_group_flags(framework::Uuid root, bool physical, bool temporary, bool phantom,
                         std::optional<bool> volume_detect = {});
    void set_part_volume_detect(framework::Uuid part, bool enabled);
    void set_linkset_damage(framework::Uuid part, float damage);
    bool set_script_permission_mask(framework::Uuid id, std::int32_t selector, std::int32_t value);
    bool set_sale_info(framework::Uuid root, std::uint8_t type, std::int32_t price);
    bool set_linkset_data(framework::Uuid root, std::vector<std::uint8_t> data,
                          std::optional<std::int32_t> empty_limit = {});
    bool sell_original(const SceneObject& expected, framework::Uuid owner, framework::Uuid group,
                       bool apply_next_permissions, bool propagate_permissions, bool included_with_parcel);
    std::optional<PrimPhysicsProperties> update_extra_physics(framework::Uuid id, PrimPhysicsProperties requested,
                                                            bool update_shape = true);
    void set_prim_buoyancy(framework::Uuid id, float buoyancy);
    void set_rotation_axes(framework::Uuid id, std::uint8_t axes, bool enabled);
    void set_prim_inertia(framework::Uuid id, std::optional<framework::PrimInertiaOverride> inertia);
    void set_prim_force(framework::Uuid id, framework::Vector3 force);
    framework::Vector3 prim_force(framework::Uuid id) const;
    void set_prim_torque(framework::Uuid id, framework::Vector3 torque, bool local);
    framework::Vector3 prim_torque(framework::Uuid id) const;
    void set_prim_velocity(framework::Uuid id, framework::Vector3 velocity, bool local, bool angular);
    void set_prim_hover(framework::Uuid id, framework::PrimHover hover);
    void set_prim_move_target(framework::Uuid id, framework::PrimMoveTarget target);
    void set_prim_look_at(framework::Uuid id, framework::PrimLookAt target);
    void expire_temporary_objects(std::chrono::steady_clock::time_point now);
    bool resize_group(framework::Uuid root, framework::Vector3 scale, float minimum, float maximum,
                      std::optional<framework::Vector3> position = {});
    float group_scale_limit(framework::Uuid root, float limit, bool maximum) const;
    bool resize_group_uniform(framework::Uuid root, double factor, float minimum, float maximum);
    bool link_prim(framework::Uuid id, framework::Uuid root, int physical_capacity = 0);
    bool set_inventory_state(framework::Uuid id, bool empty, bool scripted, bool touch,
                             std::optional<std::uint32_t> script_local_flags = {});
    std::vector<WorldEvent> drain_events();

private:
    std::optional<SceneObject> add_unlocked(SceneObject object, bool attach_to_backup, bool send_update);
    bool remove_unlocked(framework::Uuid id, bool send_update = true);
    void mark_linkset_changed_unlocked(SceneObject& part);
    framework::Uuid linkset_root_id_unlocked(framework::Uuid id) const;
    std::map<framework::Uuid, std::chrono::steady_clock::time_point> temporary_expiry_;
    void event_unlocked(WorldEventKind kind, const SceneObject& object, std::int32_t script_changes = 0);
    framework::Uuid region_;
    Terrain terrain_;
    std::atomic<std::uint32_t> last_local_id_;
    mutable std::mutex mutex_;
    std::map<framework::Uuid, SceneObject> objects_;
    std::map<std::uint32_t, std::vector<framework::Uuid>> attachments_;
    std::map<std::uint32_t, framework::Uuid> by_local_;
    std::set<framework::Uuid> selected_;
    std::vector<WorldEvent> events_;
    std::uint64_t sequence_ = 0, link_sequence_ = 0;
};
}
