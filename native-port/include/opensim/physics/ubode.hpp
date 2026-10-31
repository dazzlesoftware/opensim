// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt; native engine keeps its ODE BSD license.
#pragma once
#include "opensim/scene/terrain.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include <filesystem>
#include <memory>
#include <string>
namespace opensim::physics {
struct UbodeSnapshot {
 framework::Vector3 position,velocity,acceleration,body_position,body_velocity,controller_force;
 bool colliding=false,colliding_ground=false,flying=false,running=false;
 framework::Quaternion orientation{0,0,0,1},body_orientation{0,0,0,1};
};
struct UbodeWorldOptions {float scene_frame_time=.0909f;}; // OpenSimDefaults.ini FrameTime.
// Source ubODE default profile: one avatar over terrain, no other collision
// geometry. Source controller + actual bundled native solver, fixed20ms frame.
// Public methods belong to the constructing thread; independent worlds share
// serialized/refcounted native initialization and thread-data allocation.
class UbodeWorld {
public:
 UbodeWorld(const std::filesystem::path& explicit_library,const scene::Terrain&,UbodeWorldOptions options={});
 ~UbodeWorld();
 UbodeWorld(const UbodeWorld&)=delete;
 void add_avatar(framework::Vector3 position,framework::Vector3 size,bool flying=false);
 void remove_avatar();
 void set_target_velocity(framework::Vector3);
 void set_flying(bool);
 void set_running(bool);
 void set_orientation(framework::Quaternion);
 // Source AvatarJump -> queued Force change applied before the next Move.
 void avatar_jump(float impulse_z=9.4f);
 void step(std::size_t frames=1);
 UbodeSnapshot snapshot()const;
 float terrain_height(float x,float y)const;
 framework::Vector3 terrain_normal(float x,float y)const;
 std::string configuration()const;
 static constexpr float time_step=.020f;
private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
