// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/terrain.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <string>
namespace opensim::scene {
struct PrimShape {
 std::uint8_t pcode=9,state=0,path_curve=16,profile_curve=1;
 std::uint16_t path_begin=0,path_end=0,profile_begin=0,profile_end=0,profile_hollow=0;
 std::uint8_t path_scale_x=100,path_scale_y=100,path_shear_x=0,path_shear_y=0,path_revolutions=0;
 std::int8_t path_twist=0,path_twist_begin=0,path_radius_offset=0,path_taper_x=0,path_taper_y=0,path_skew=0;
};
// Single-root object projection for the scene owner. Full linked parts,
// permissions/effects, physics, inventory/scripts and storage are not implied.
struct SceneObject {
 SceneObject();
 framework::Uuid id,owner,creator,group,rezzer,last_owner;
 std::uint32_t local_id=0,parent_id=0,flags=0;
 std::string name="Object",description,touch_name,sit_name;
 std::int32_t creation_date=0,sale_price=0;std::uint8_t sale_type=0;
 std::uint32_t base_mask=0x9e000,owner_mask=0x9e000,group_mask=0,everyone_mask=0,next_mask=0x82000,category=0;
 framework::Uuid from_inventory_item;

 framework::Vector3 position,scale{.5f,.5f,.5f},velocity,angular_velocity,acceleration;
 framework::Quaternion rotation{0,0,0,1};
 PrimShape shape;
 std::vector<std::uint8_t> texture_entry;
 bool is_attachment=false,attached_to_backup=false,changed=false,inventory_empty=true;
 static SceneObject primitive(framework::Uuid owner,framework::Vector3 position,
                              framework::Vector3 scale={.5f,.5f,.5f});
};
enum class WorldEventKind {Added,Updated,Removed};
struct WorldEvent {WorldEventKind kind;SceneObject object;std::uint64_t sequence=0;};
class World {
public:
 explicit World(framework::Uuid region={},std::uint32_t width=256,std::uint32_t height=256,
                std::uint32_t last_local_id=720000);
 framework::Uuid region_id()const{return region_;}
 Terrain& terrain(){return terrain_;}
 const Terrain& terrain()const{return terrain_;}
 // Shared with avatar IDs. SceneBase uses unchecked Interlocked.Increment;
 // uint32 wrap, including zero, is deliberate source behavior.
 std::uint32_t allocate_local_id();
 std::optional<SceneObject> add(SceneObject object,bool attach_to_backup=true,bool send_update=true);
 std::optional<SceneObject> object(framework::Uuid id)const;
 std::optional<SceneObject> object(std::uint32_t local_id)const;
 std::vector<SceneObject> objects()const;
 std::size_t size()const;
 bool update_transform(framework::Uuid id,framework::Vector3 position,
                       framework::Quaternion rotation,framework::Vector3 scale);
 bool update_motion(framework::Uuid id,framework::Vector3 velocity,
                    framework::Vector3 angular_velocity,framework::Vector3 acceleration);
 bool remove(framework::Uuid id);
 bool set_inventory_state(framework::Uuid id,bool empty,bool scripted,bool touch,std::optional<std::uint32_t> script_local_flags={});
 std::vector<WorldEvent> drain_events();
private:
 void event_unlocked(WorldEventKind kind,const SceneObject& object);
 framework::Uuid region_;Terrain terrain_;std::atomic<std::uint32_t> last_local_id_;
 mutable std::mutex mutex_;
 std::map<framework::Uuid,SceneObject> objects_;
 std::map<std::uint32_t,framework::Uuid> by_local_;
 std::vector<WorldEvent> events_;std::uint64_t sequence_=0;
};
}
