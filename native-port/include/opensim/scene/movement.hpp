// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/quaternion_text.hpp"
#include "opensim/framework/uuid.hpp"
namespace opensim::scene {
struct MovementState {
    framework::Quaternion rotation{0, 0, 0, 1};
    bool flying = false, npc = false, colliding = false, moving_to_target = false;
    bool falling = false, has_physics_actor = true, pid_hover_active = false;
    float speed_modifier = 1, move_to_speed = -1;
    std::uint32_t controls = 0;
    double delayed_stop = -1;
    framework::Vector3 target_velocity;
    std::uint64_t animation_updates = 0;
};
// Source ScenePresence.AddNewMovement projection; the host applies the target
// to its PhysicsActor and dispatches the resulting animation update.
void add_new_movement(MovementState&, framework::Vector3 relative, float additional_speed,
                      bool breaking = false);
std::uint32_t remove_ignored_controls(std::uint32_t controls, std::uint32_t ignored);
struct ControlProjection {
    framework::Vector3 relative;
    float speed = 4.096f;
    std::uint32_t controls = 0, movement_flags = 0;
    bool changed = false, key_pressed = false;
};
// Direction/speed portion of HandleAgentUpdate. Autopilot, camera, script
// controls, sit/stand and animation transitions remain host responsibilities.
ControlProjection project_controls(std::uint32_t raw_controls, std::uint32_t ignored, bool always_run,
                                   std::uint32_t previous_movement_flags = 0);
}
