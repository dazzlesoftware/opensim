// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/world.hpp"
#include "opensim/framework/texture_entry.hpp"
#include <chrono>
#include <bit>
namespace opensim::scene {
SceneObject::SceneObject():texture_entry(framework::TextureEntry(framework::Uuid("89556747-24cb-43ed-920b-47caed15465f")).bytes()){creation_date=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()));}
SceneObject SceneObject::primitive(framework::Uuid owner,framework::Vector3 position,framework::Vector3 scale){SceneObject result;result.id=framework::Uuid::random();result.owner=result.creator=result.rezzer=result.last_owner=owner;result.position=position;result.scale=scale;return result;}
World::World(framework::Uuid region,std::uint32_t width,std::uint32_t height,std::uint32_t last_local_id):region_(region),terrain_(width,height),last_local_id_(last_local_id){}
std::uint32_t World::allocate_local_id(){return last_local_id_.fetch_add(1,std::memory_order_relaxed)+1u;}
void World::event_unlocked(WorldEventKind kind,const SceneObject& object){events_.push_back({kind,object,++sequence_});}
std::optional<SceneObject> World::add(SceneObject object,bool attach_to_backup,bool send_update){
 if(object.id.is_zero())return {};std::lock_guard lock(mutex_);if(objects_.contains(object.id))return {};
 if(object.shape.pcode!=9||object.shape.state==0)object.parent_id=0;
 if(object.local_id==0)object.local_id=allocate_local_id();object.attached_to_backup=attach_to_backup&&!object.is_attachment;if(attach_to_backup)object.changed=true;
 objects_[object.id]=object;by_local_[object.local_id]=object.id;if(send_update)event_unlocked(WorldEventKind::Added,object);return object;
}
std::optional<SceneObject> World::object(framework::Uuid id)const{std::lock_guard lock(mutex_);auto i=objects_.find(id);return i==objects_.end()?std::nullopt:std::optional(i->second);}
std::optional<SceneObject> World::object(std::uint32_t local_id)const{std::lock_guard lock(mutex_);auto i=by_local_.find(local_id);if(i==by_local_.end())return {};auto row=objects_.find(i->second);return row==objects_.end()?std::nullopt:std::optional(row->second);}
std::vector<SceneObject> World::objects()const{std::lock_guard lock(mutex_);std::vector<SceneObject> result;result.reserve(objects_.size());for(auto& [id,object]:objects_)result.push_back(object);return result;}
std::size_t World::size()const{std::lock_guard lock(mutex_);return objects_.size();}
bool World::update_transform(framework::Uuid id,framework::Vector3 position,framework::Quaternion rotation,framework::Vector3 scale){std::lock_guard lock(mutex_);auto i=objects_.find(id);if(i==objects_.end())return false;auto& object=i->second;object.position=position;object.rotation=rotation;object.scale=scale;object.changed=true;event_unlocked(WorldEventKind::Updated,object);return true;}
bool World::update_motion(framework::Uuid id,framework::Vector3 velocity,framework::Vector3 angular_velocity,framework::Vector3 acceleration){std::lock_guard lock(mutex_);auto i=objects_.find(id);if(i==objects_.end())return false;auto& object=i->second;object.velocity=velocity;object.angular_velocity=angular_velocity;object.acceleration=acceleration;object.changed=true;event_unlocked(WorldEventKind::Updated,object);return true;}
bool World::remove(framework::Uuid id){std::lock_guard lock(mutex_);auto i=objects_.find(id);if(i==objects_.end())return false;auto object=i->second;by_local_.erase(object.local_id);objects_.erase(i);event_unlocked(WorldEventKind::Removed,object);return true;}
bool World::set_inventory_state(framework::Uuid id,bool empty,bool scripted,bool touch,std::optional<std::uint32_t> local){std::lock_guard lock(mutex_);auto i=objects_.find(id);if(i==objects_.end())return false;auto& object=i->second;object.inventory_empty=empty;if(local)object.flags=(object.flags&~0x10280u)|(*local&0x10280u);object.flags=(object.flags&~0xc0u)|(scripted?0x40u:0u)|(touch?0x80u:0u);object.changed=true;event_unlocked(WorldEventKind::Updated,object);return true;}
std::vector<WorldEvent> World::drain_events(){std::lock_guard lock(mutex_);std::vector<WorldEvent> result;result.swap(events_);return result;}
}
