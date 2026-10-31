// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include <functional>
#include <optional>
#include <vector>
namespace opensim::scene {
struct RezLocationRequest {
 framework::Vector3 ray_start,ray_end;
 framework::Uuid target;
 framework::Quaternion rotation{0,0,0,1};
 std::uint8_t bypass_raycast=0,ray_end_is_intersection=0;
 bool front_faces_only=true,face_center=false;
 framework::Vector3 scale{.5f,.5f,.5f};
};
struct RezContact {std::uint32_t consumer_id=0;framework::Vector3 position,normal;};
struct RezIntersection {bool hit=false;framework::Vector3 point,normal,axis_aligned_face_normal;};
struct RezLocationBackend {
 float water_height=20.f;
 // An absent filtered callback means SupportsRayCastFiltered()==false.
 std::function<std::vector<RezContact>(framework::Vector3,framework::Vector3,float,int,std::uint16_t)> filtered_raycast;
 std::function<std::optional<framework::Uuid>(std::uint32_t)> part_uuid;
 // nullopt means the target part does not exist; hit=false means it exists
 // but its TestIntersectionOBB missed. Those source branches differ.
 std::function<std::optional<RezIntersection>(framework::Uuid,framework::Vector3,framework::Vector3,framework::Quaternion,bool,bool)> target_intersection;
 std::function<RezIntersection(framework::Vector3,framework::Vector3,bool,bool)> closest_intersection;
};
// Complete Scene.GetNewRezLocation control flow. Physics/geometry are supplied
// by the scene backend. Missing callbacks needed by a branch throw explicitly.
framework::Vector3 get_new_rez_location(const RezLocationRequest&,const RezLocationBackend&);
}
