// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/rez_location.hpp"
#include <cmath>
#include <stdexcept>
namespace opensim::scene {
namespace {
using framework::Vector3;
Vector3 add(Vector3 a,Vector3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vector3 multiply(Vector3 a,float b){return {a.x*b,a.y*b,a.z*b};}
}
framework::Vector3 get_new_rez_location(const RezLocationRequest& r,const RezLocationBackend& backend){
 Vector3 dir{r.ray_end.x-r.ray_start.x,r.ray_end.y-r.ray_start.y,r.ray_end.z-r.ray_start.z};
 Vector3 water{0,0,-100.f};if(r.ray_start.z>backend.water_height&&r.ray_end.z<backend.water_height){float ratio=(backend.water_height-r.ray_start.z)/dir.z;water={r.ray_start.x+ratio*dir.x,r.ray_start.y+ratio*dir.y,backend.water_height};}
 auto above_water=[&](Vector3 p){return water.z>p.z?water:p;};
 if(r.ray_end_is_intersection!=1){
  float dist=std::sqrt((dir.x*dir.x+dir.y*dir.y)+dir.z*dir.z);
  if(dist!=0){Vector3 direction=multiply(dir,1.f/dist);dist+=1.f;
   if(backend.filtered_raycast){auto contacts=backend.filtered_raycast(r.ray_start,direction,dist,4,0x407a);
    if(!contacts.empty()){
     auto position=[&](const RezContact& c){Vector3 offset{c.normal.x*r.scale.x,c.normal.y*r.scale.y,c.normal.z*r.scale.z};offset=multiply(offset,.5f);return above_water(add(c.position,offset));};
     if(r.target.is_zero()){for(const auto& c:contacts)if(c.consumer_id==0)return position(c);}
     else {if(!backend.part_uuid)throw std::logic_error("rez backend requires local-ID part lookup");for(const auto& c:contacts){auto id=backend.part_uuid(c.consumer_id);if(id&&*id==r.target)return position(c);}}
     return position(contacts.front());
    }
   }
   if(!r.target.is_zero()){
    if(!backend.target_intersection)throw std::logic_error("rez backend requires target OBB intersection");
    auto intersection=backend.target_intersection(r.target,r.ray_start,direction,{0,0,0,1},r.front_faces_only,r.face_center);
    if(intersection){const auto& e=*intersection;if(e.hit){float offset=.5f;if(e.axis_aligned_face_normal.x!=0)offset=r.scale.x;if(e.axis_aligned_face_normal.y!=0)offset=r.scale.y;if(e.axis_aligned_face_normal.z!=0)offset=r.scale.z;offset=std::abs(offset);return above_water(add(e.point,multiply(e.normal,offset/2.f)));}}
    else {if(!backend.closest_intersection)throw std::logic_error("rez backend requires closest prim intersection");auto e=backend.closest_intersection(r.ray_start,direction,true,false);return above_water(e.hit?e.point:r.ray_end);}
   }
  }
 }
 auto pos=r.ray_end;pos.z+=r.scale.z/2.f;return above_water(pos);
}
}
