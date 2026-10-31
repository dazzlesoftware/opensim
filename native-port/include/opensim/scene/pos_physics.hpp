// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include <functional>
#include <memory>
#include <vector>
namespace opensim::scene {
class PosPhysicsScene;
struct PosCharacter {
 framework::Vector3 position,velocity,target_velocity,acceleration,rotational_velocity;
 bool flying=false,is_colliding=false;
 std::uint64_t terse_update_count=0;
 std::function<void()> on_terse_update;
 void set_size(framework::Vector3 v){size_=v;size_.z/=2.f;}
 framework::Vector3 size()const{return size_;}
 void set_velocity(framework::Vector3 v){target_velocity=v;}
private:
 friend class PosPhysicsScene;
 framework::Vector3 size_;
 void request_terse_update(){++terse_update_count;if(on_terse_update)on_terse_update();}
};
struct PosPrim {framework::Vector3 position,size;framework::Quaternion orientation;};
// Exact POS compatibility, including its source old-position X/Y clamps and
// collision tests. This engine is not ubODE/Bullet and does not walk normally.
class PosPhysicsScene {
public:
 std::shared_ptr<PosCharacter> add_avatar(framework::Vector3 position,
  framework::Vector3 velocity,framework::Vector3 size,bool flying);
 std::shared_ptr<PosPrim> add_prim(framework::Vector3 position,framework::Vector3 size,framework::Quaternion rotation);
 void remove_avatar(const std::shared_ptr<PosCharacter>&);
 void remove_prim(const std::shared_ptr<PosPrim>&);
 void set_terrain(std::vector<float> serialized_heights){heights_=std::move(serialized_heights);}
 // Source DeleteTerrain and SetWaterLevel are no-ops.
 void delete_terrain(){}
 void set_water_level(float){}
 float simulate(float time_step);
 static bool intersects(const PosCharacter&,const PosPrim&);
private:
 float height_at(framework::Vector3)const;
 bool colliding(const PosCharacter&)const;
 std::vector<std::shared_ptr<PosCharacter>> characters_;
 std::vector<std::shared_ptr<PosPrim>> prims_;
 std::vector<float> heights_;
};
}
