// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/viewer_physics.hpp"
#include "opensim/scene/movement.hpp"
#include "opensim/scene/movement_animation.hpp"
#include "opensim/scene/avatar_packets.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/physics/ubode.hpp"
#include <bit>
#include <chrono>
#include <cmath>
#include <mutex>
namespace opensim::scene {
using namespace framework;
namespace {
double milliseconds(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::int32_t tick_count(){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(milliseconds())));}
}
struct ViewerPhysics::Impl {
 enum class Kind {Enter,Input,Running,Leave};
 struct Command {Kind kind;ViewerAgent agent;};
 ViewerRegion& viewers;std::shared_ptr<const DefaultAnimations> catalog;
 Publish publish;Log log;physics::UbodeWorld world;
 std::mutex queue_mutex;std::vector<Command> queue;
 Uuid owner,owner_session;MovementState movement;std::unique_ptr<MovementAnimator> animator;
 std::uint32_t movement_flags=0,sequence_counter=0;std::int32_t animation_sequence=1;
 Uuid animation;bool always_run=false,stop_active=false;
 double last_clock=milliseconds(),last_publish=last_clock,last_animation=last_clock;
 float accumulated=0;
 static constexpr float scene_frame_time=.0909f;
 Impl(World& scene,ViewerRegion& v,const std::filesystem::path& library,std::shared_ptr<const DefaultAnimations> c,Publish p,Log l)
  :viewers(v),catalog(std::move(c)),publish(std::move(p)),log(std::move(l)),world(library,scene.terrain()){
  if(!catalog)throw std::invalid_argument("Physics animation catalog required");
 }
 void post(Kind kind,const ViewerAgent& a){std::lock_guard lock(queue_mutex);queue.push_back({kind,a});}
 void send_animation(Uuid recipient){auto active=viewers.agent(owner);if(!active||active->circuit->session_id!=owner_session)return;AvatarAnimationEntry entry{animation,{},animation_sequence};if(recipient==owner)viewers.transport().enqueue_if_session(recipient,owner_session,avatar_animations(owner,recipient,std::span(&entry,1)),network::ThrottleOutPacketType::Task);}
 bool change_animation(const std::string& name){
  auto id=catalog->resolve(name);auto next=std::bit_cast<std::int32_t>(++sequence_counter);if(next<0){sequence_counter=static_cast<std::uint32_t>(tick_count())&0xafff5fu;next=std::bit_cast<std::int32_t>(++sequence_counter);}
  if(id.is_zero()||id==animation)return false;animation=id;animation_sequence=next;
  for(auto& a:viewers.agents())if(a.root)send_animation(a.circuit->agent_id);return true;
 }
 void animate(){
  if(owner.is_zero())return;auto state=world.snapshot();MovementAnimationContext context;
  context.controls=movement.controls;context.stop_active=stop_active;context.always_run=always_run;
  context.flying=state.flying;context.colliding=state.colliding;context.velocity=state.velocity;
  MovementAnimationEffects effects;effects.jump=[this](float impulse){world.avatar_jump(impulse);};
  effects.set_flying=[this](bool value){world.set_flying(value);};
  effects.set_selected=[](bool){}; // Source OdeCharacter.Selected setter is a no-op.
  effects.try_set_animation=[this](const auto& name){return change_animation(name);};
  animator->update(context,effects);
 }
 void apply_target(Vector3 relative,float speed,bool breaking=false){
  auto state=world.snapshot();movement.flying=state.flying;movement.colliding=state.colliding;
  movement.falling=animator->state()==MotionControlState::falling;
  add_new_movement(movement,relative,speed,breaking);world.set_target_velocity(movement.target_velocity);animate();
 }
 void update_input(const ViewerAgent& agent,double now){
  auto q=agent.rotation;if(!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z)||!std::isfinite(q.w))return;
  movement.rotation=q;world.set_orientation(q);
  auto projected=project_controls(agent.controls,0,always_run,movement_flags);movement_flags=projected.movement_flags;movement.controls=projected.controls;
  bool changed=projected.changed;auto state=world.snapshot();bool flying=(movement.controls&0x2000u)!=0;
  if(flying!=state.flying){world.set_flying(flying);changed=true;}
  bool stop=(movement.controls&0x4000u)!=0;if(stop!=stop_active){stop_active=stop;changed=true;}
  auto relative=projected.relative;if(!flying&&state.colliding&&relative.z<0)relative.z=0;
  if(changed){auto phase=animator->state();
   if(stop_active)apply_target(relative,.2f*4.096f,movement_flags==0&&phase==MotionControlState::falling);
   else if(phase!=MotionControlState::flying&&phase!=MotionControlState::onsurface)apply_target(relative,projected.speed);
   else if(movement_flags!=0)apply_target(relative,projected.speed);
   else movement.delayed_stop=now+250.;
  }else if(movement.controls&0x8000u)animate();
 }
 void command(const Command& command,double now){
  const auto& a=command.agent;
  if(command.kind==Kind::Enter){
   if(!owner.is_zero())throw std::logic_error("ubODE connected profile supports one avatar");
   auto current=viewers.agent(a.circuit->agent_id);if(!current||!current->root||current->circuit->session_id!=a.circuit->session_id)return;
   auto size=a.circuit->appearance?a.circuit->appearance->avatar_box_size():Vector3{.45f,.6f,2.1f};
   try{world.add_avatar(a.position,size);world.set_orientation(a.rotation);}catch(...){try{world.remove_avatar();}catch(...){}throw;}always_run=a.always_run;world.set_running(always_run);
   owner=a.circuit->agent_id;owner_session=a.circuit->session_id;movement={};movement.rotation=a.rotation;movement_flags=0;stop_active=false;animator=std::make_unique<MovementAnimator>(tick_count);
   animation=catalog->resolve("STAND");animation_sequence=1;sequence_counter=static_cast<std::uint32_t>(tick_count())&0x5fffafu;
   accumulated=0;last_clock=last_animation=last_publish=now;
   for(auto& other:viewers.agents())if(other.root)send_animation(other.circuit->agent_id);
   return;
  }
  if(a.circuit->agent_id!=owner||a.circuit->session_id!=owner_session)return;
  if(command.kind==Kind::Leave){world.remove_avatar();owner={};owner_session={};animator.reset();return;}
  if(command.kind==Kind::Running){always_run=a.always_run;world.set_running(always_run);return;}
  update_input(a,now);
 }
 void poll(){
  const double now=milliseconds();std::vector<Command> commands;{std::lock_guard lock(queue_mutex);commands.swap(queue);}
  for(auto& entry:commands)try{command(entry,now);}catch(const std::exception& e){if(log)log(e.what());}
  if(owner.is_zero()){last_clock=now;accumulated=0;return;}
  accumulated+=static_cast<float>((now-last_clock)*.001);last_clock=now;
  // Source solver uses a20ms quantum,10ms half-step gate,1.2-frame work
  // budget and400ms lag drop. Other scene scheduling/mesh work is not modeled.
  const double deadline=now+static_cast<int>(scene_frame_time*1200.f);
  while(accumulated>.010f){world.step();accumulated-=physics::UbodeWorld::time_step;if(milliseconds()>deadline)break;}
  if(accumulated>.4f){accumulated=0;last_clock=milliseconds();}
  if(movement.delayed_stop>0&&now>movement.delayed_stop)apply_target({},4.096f);
  // Source RegionHeartbeatEnd reevaluates the animator every second heartbeat.
  if(now-last_animation>=scene_frame_time*2000.f){animate();last_animation=now;}
  if(now-last_publish>=scene_frame_time*1000.f){auto state=world.snapshot();auto a=viewers.update_physics(owner,owner_session,state.position,state.velocity,state.acceleration,state.flying);if(a&&publish)publish(*a);last_publish=now;}
 }
};
ViewerPhysics::ViewerPhysics(World& world,ViewerRegion& viewers,const std::filesystem::path& library,std::shared_ptr<const DefaultAnimations> catalog,Publish publish,Log log):impl_(std::make_unique<Impl>(world,viewers,library,std::move(catalog),std::move(publish),std::move(log))){}
ViewerPhysics::~ViewerPhysics()=default;
void ViewerPhysics::entered(const ViewerAgent& a){impl_->post(Impl::Kind::Enter,a);}
void ViewerPhysics::input(const ViewerAgent& a){impl_->post(Impl::Kind::Input,a);}
void ViewerPhysics::running(const ViewerAgent& a){impl_->post(Impl::Kind::Running,a);}
void ViewerPhysics::left(const ViewerAgent& a){impl_->post(Impl::Kind::Leave,a);}
void ViewerPhysics::poll(){impl_->poll();}
}
