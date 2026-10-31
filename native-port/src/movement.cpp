// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/movement.hpp"
#include <cmath>
#if defined(_MSC_VER) && (defined(_M_X64)||defined(_M_IX86))
#include <intrin.h>
#elif defined(__x86_64__)||defined(__i386__)
#include <cpuid.h>
#endif
namespace opensim::scene {
namespace {
bool sdk_sse41(){
 static const bool supported=[](){
 #if defined(_MSC_VER) && (defined(_M_X64)||defined(_M_IX86))
 int info[4];__cpuid(info,1);return (info[2]&(1<<19))!=0;
 #elif defined(__x86_64__)||defined(__i386__)
 unsigned a,b,c,d;return __get_cpuid(1,&a,&b,&c,&d)&&(c&bit_SSE4_1)!=0;
 #else
 return false;
 #endif
 }();return supported;
}
framework::Vector3 rotate(framework::Vector3 v,framework::Quaternion q){
 float x=q.x+q.x,y=q.y+q.y,z=q.z+q.z;
 float wx=q.w*x,wy=q.w*y,wz=q.w*z,xx=q.x*x,xy=q.x*y,xz=q.x*z,yy=q.y*y,yz=q.y*z,zz=q.z*z;
 return {v.x*(1.f-yy-zz)+v.y*(xy-wz)+v.z*(xz+wy),v.x*(xy+wz)+v.y*(1.f-xx-zz)+v.z*(yz-wx),v.x*(xz-wy)+v.y*(yz+wx)+v.z*(1.f-xx-yy)};
}
void normalize(framework::Vector3& v){float squared=(v.x*v.x+v.y*v.y)+v.z*v.z;if(!(squared>1.e-6f)){v={};return;}float length=std::sqrt(squared);if(sdk_sse41()){v.x/=length;v.y/=length;v.z/=length;}else{float inverse=1.f/length;v.x*=inverse;v.y*=inverse;v.z*=inverse;}}
void multiply(framework::Vector3& v,float factor){v.x*=factor;v.y*=factor;v.z*=factor;}
}
void add_new_movement(MovementState& state,framework::Vector3 relative,float additional_speed,bool breaking){
 state.delayed_stop=-1;auto rotation=state.rotation;if(!state.flying&&!state.npc){rotation.x=0;rotation.y=0;}
 auto direction=rotate(relative,rotation);normalize(direction);if(relative.z==0&&!state.flying)direction.z=0;
 bool normal_speed=!state.moving_to_target||state.move_to_speed<=0;
 multiply(direction,normal_speed?state.speed_modifier*additional_speed:state.move_to_speed);
 if(state.falling&&(!state.has_physics_actor||!state.pid_hover_active)){if(breaking)direction.z=-9999.f;}
 else if(state.flying){if(normal_speed){if(state.colliding&&direction.z<0)direction={};else multiply(direction,4.f);}}
 else if(state.colliding){if((state.controls&0x10u)!=0&&normal_speed)direction.z=0;else if(direction.z<0)direction.z=0;}
 state.target_velocity=state.has_physics_actor?direction:framework::Vector3{};++state.animation_updates;
}
std::uint32_t remove_ignored_controls(std::uint32_t controls,std::uint32_t ignored){
 if(controls==0||ignored==0)return controls;
 // Source masks to normal movement bits before its later mouse/rotation tests;
 // those tests therefore never match. Preserve this observable behavior.
 ignored&=0x3fu;ignored|=ignored<<19;return controls&~ignored;
}
ControlProjection project_controls(std::uint32_t controls,std::uint32_t ignored,bool always_run,std::uint32_t previous){
 constexpr std::uint32_t normal=0x3fu,nudge=normal<<19;ControlProjection out;out.controls=remove_ignored_controls(controls,ignored);out.movement_flags=out.controls&(normal|nudge);
 if(out.movement_flags!=0){out.key_pressed=out.changed=true;if(!always_run&&(out.controls&(0x400u|0x1000u))==0)out.speed=.6f*4.096f;if(out.movement_flags&nudge)out.movement_flags|=out.movement_flags>>19;
  constexpr framework::Vector3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  for(unsigned i=0,mask=1;i<6;++i,mask<<=1)if(out.movement_flags&mask){out.relative.x+=directions[i].x;out.relative.y+=directions[i].y;out.relative.z+=directions[i].z;}
 }else if(previous&(normal|nudge))out.changed=true;return out;
}
}
