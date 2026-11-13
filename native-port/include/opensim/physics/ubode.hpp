// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt; native engine keeps its ODE BSD license.
#pragma once
#include "opensim/scene/terrain.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include "opensim/framework/prim_geometry.hpp"
#include <filesystem>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <set>
#include <string>
#include <vector>
namespace opensim::physics {
struct UbodeSnapshot {
    framework::Vector3 position, velocity, acceleration, body_position, body_velocity, controller_force;
    bool colliding = false, colliding_ground = false, flying = false, running = false;
    framework::Quaternion orientation{0, 0, 0, 1}, body_orientation{0, 0, 0, 1};
    std::array<float, 4> collision_plane{0, 0, 0, 1};
    framework::Vector3 angular_velocity;
};
struct UbodeWorldOptions {
    float scene_frame_time = .0909f; // OpenSimDefaults.ini FrameTime.
    float water_height = 20.f;
    bool report_prim_collisions = false;
};
struct PrimCollisionActor {
    framework::Uuid id;
    std::uint32_t avatar_local_id = 0;

    auto operator<=>(const PrimCollisionActor&) const = default;
};
struct CollisionImpact {
    framework::Vector3 position;
    float speed = 0.f;
    float depth = 0.f;
};
struct PrimCollisionReport {
    std::set<framework::Uuid> terrain;
    std::set<std::pair<framework::Uuid, PrimCollisionActor>> objects;
    std::map<std::pair<PrimCollisionActor, PrimCollisionActor>, CollisionImpact> impacts;
    std::set<std::pair<PrimCollisionActor, PrimCollisionActor>> avatar_contacts;
};
struct UbodeMassProperties {
    float mass = 0.f;
    framework::Vector3 center;
};
struct UbodeBodyState {
    framework::Vector3 position;
    framework::Quaternion rotation{0, 0, 0, 1};
    framework::Vector3 velocity;
    framework::Vector3 angular_velocity;
};
struct AssetPhysicsMesh;
struct UbodePrimForces {
    float gravity_modifier = 1.f;
    float buoyancy = 0.f;
    framework::Vector3 force, torque;
    framework::PrimHover hover;
    framework::PrimMoveTarget move_target;
    framework::PrimLookAt look_at;
    std::optional<framework::PrimInertiaOverride> inertia_override;
    std::uint8_t rotation_axis_locks = 0;
    bool operator==(const UbodePrimForces&) const = default;
};
struct UbodeColliderProperties {
    framework::PrimContactMaterial material;
    bool volume_detect = false;
    bool phantom = false;
};
struct UbodePrimPart {
    framework::Vector3 size, offset;
    framework::Quaternion rotation{0, 0, 0, 1};
    float mass;
    framework::PrimType type = framework::PrimType::Box;
    std::uint8_t mesh_segments = 0;
    float hollow = 0.f;
    framework::LinearPrimPath path;
    std::shared_ptr<const AssetPhysicsMesh> asset_mesh;
    framework::PrimContactMaterial contact_material;
    framework::Uuid id;
    bool volume_detect = false;
    bool phantom = false;
};
enum class HoverReference { Ground, GroundAndWater, Absolute };
// Source avatar controller and native rigid bodies in the bundled ubODE solver.
// Public methods belong to the constructing thread; independent worlds share
// serialized/refcounted native initialization and thread-data allocation.
class UbodeWorld {
  public:
    UbodeWorld(const std::filesystem::path& explicit_library, const scene::Terrain&,
               UbodeWorldOptions options = {});
    // Creates another controller in the same solver and collision space.
    explicit UbodeWorld(UbodeWorld& scene);
    ~UbodeWorld();
    UbodeWorld(const UbodeWorld&) = delete;
    void add_avatar(framework::Vector3 position, framework::Vector3 size, bool flying = false,
                    PrimCollisionActor collision_actor = {});
    void resize_avatar(framework::Vector3 size);
    void remove_avatar();
    void set_target_velocity(framework::Vector3);
    void set_momentum(framework::Vector3);
    void move_avatar_position(framework::Vector3);
    void snap_avatar_position(framework::Vector3);
    void set_flying(bool);
    void set_running(bool);
    void set_buoyancy(float);
    void set_hover(float height, float tau, HoverReference reference, bool active);
    void set_water_height(float height);
    void set_orientation(framework::Quaternion);
    // Source AvatarJump -> queued Force change applied before the next Move.
    void avatar_jump(float impulse_z = 9.4f);
    void push_avatar(framework::Vector3 impulse);
    void step(std::size_t frames = 1);
    void step_with(std::span<UbodeWorld* const> peers, std::size_t frames = 1);
    void update_terrain(const scene::Terrain&);
    void add_dynamic_box(framework::Uuid id, framework::Vector3 size, float mass,
                         const UbodeBodyState& state);
    void add_dynamic_linkset(framework::Uuid id, std::span<const UbodePrimPart> parts,
                             const UbodeBodyState& state, UbodePrimForces forces = {});
    void add_static_box(framework::Uuid id, framework::Vector3 size, framework::Vector3 position,
                        framework::Quaternion rotation);
    void add_static_prim(framework::Uuid id, framework::PrimType type, framework::Vector3 size,
                         framework::Vector3 position, framework::Quaternion rotation,
                         std::uint8_t mesh_segments = 0, float hollow = 0.f,
                         framework::LinearPrimPath path = {},
                         std::shared_ptr<const AssetPhysicsMesh> asset_mesh = {},
                         framework::PrimContactMaterial contact_material = {}, bool volume_detect = false,
                         float mass = 0.f);
    void remove_box(framework::Uuid id);
    void apply_prim_impulse(framework::Uuid id, framework::Vector3 impulse, bool angular);
    bool update_body_motion(framework::Uuid id, std::optional<framework::Vector3> position,
                            std::optional<framework::Vector3> linear, std::optional<framework::Vector3> angular,
                            const std::optional<UbodePrimForces>& forces,
                            std::optional<std::uint8_t> rotation_locks = std::nullopt,
                            std::optional<framework::Quaternion> rotation = std::nullopt);
    bool update_collider_properties(framework::Uuid id, std::span<const UbodeColliderProperties> properties);
    void update_prim_look_at();
    std::vector<PrimCollisionReport> take_prim_collision_reports();
    UbodeBodyState body_state(framework::Uuid id) const;
    std::optional<UbodeMassProperties> mass_properties(framework::Uuid actor, framework::Uuid part = {}) const;
    std::vector<std::pair<framework::Uuid, float>> top_colliders() const;
    std::optional<framework::PrimInertiaData> inertia_data(
        framework::Uuid root, const std::optional<framework::PrimInertiaOverride>& requested = {}) const;
    UbodeSnapshot snapshot() const;
    float avatar_mass() const;
    float terrain_height(float x, float y) const;
    framework::Vector3 terrain_normal(float x, float y) const;
    std::string configuration() const;
    static constexpr float time_step = .020f;

  private:
    void add_prims(framework::Uuid id, std::span<const UbodePrimPart> parts,
                   const UbodeBodyState& state, bool dynamic, UbodePrimForces forces = {});
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
