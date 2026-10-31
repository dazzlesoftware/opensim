// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/agent_data.hpp"
#include "opensim/framework/integer_text.hpp"
#include "opensim/framework/vector_text.hpp"
#include <cmath>
#include <iostream>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
namespace opensim::framework {
using network::Value;
namespace {
Value uuid(const Uuid& id){Value value(id.str());value.kind=Value::Kind::Uuid;return value;}
Value real(float number){Value value;value.kind=Value::Kind::Real;value.real=number;return value;}
Value array(){Value value;value.kind=Value::Kind::Array;return value;}
bool sse41(){
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int info[4];__cpuid(info,1);return (info[2]&(1<<19))!=0;
#elif defined(__x86_64__) || defined(__i386__)
    unsigned a,b,c,d;return __get_cpuid(1,&a,&b,&c,&d)&&(c&bit_SSE4_1);
#else
    return false;
#endif
}
template<class T>void required(const std::shared_ptr<T>& value){if(!value)throw std::invalid_argument("Null agent transfer member");}
}
void AgentData::set_look_at(const Vector3& value){
    if(value.x==0&&value.y==0){at_axis={0,0,1};left_axis={0,1,0};return;}
    at_axis={value.x,value.y,0};const float xx=at_axis.x*at_axis.x,yy=at_axis.y*at_axis.y;const float length=xx+yy;
    if(length>1e-6f){const auto root=std::sqrt(length);if(sse41()){at_axis.x/=root;at_axis.y/=root;at_axis.z/=root;}else{const float reciprocal=1.f/root;at_axis.x*=reciprocal;at_axis.y*=reciprocal;at_axis.z*=reciprocal;}}
    else at_axis={};left_axis={-at_axis.y,at_axis.x,0};up_axis={0,0,1};
}
Value AgentData::pack(EntityTransferContext* context){
    auto data=Value::object({});data.set("message_type","AgentData");data.set("region_id",region_id.str());data.set("circuit_code",std::to_string(circuit_code));data.set("agent_uuid",uuid(agent_id));data.set("session_uuid",uuid(session_id));
    for(auto [key,vector]:{std::pair{"position",position},{"velocity",velocity},{"center",center},{"size",size},{"at_axis",at_axis},{"left_axis",left_axis},{"up_axis",up_axis}})data.set(key,vector3_string(vector));
    data.set("changed_grid",Value(sender_wants_to_wait_for_root));data.set("wait_for_root",Value(sender_wants_to_wait_for_root));data.set("far",real(far));data.set("aspect",real(aspect));if(throttles&&!throttles->empty())data.set("throttles",network::osd_from_binary(throttles));
    data.set("locomotion_state",std::to_string(locomotion_state));data.set("head_rotation",quaternion_string(head_rotation));data.set("body_rotation",quaternion_string(body_rotation));data.set("control_flags",std::to_string(control_flags));data.set("energy_level",real(energy_level));
    if(god_data){data.set("god_data",Value::alias(god_data));const auto& god=god_data->view();if(god.kind!=Value::Kind::Map)throw std::invalid_argument("GodData requires an OSD map");if(god.contains("ViewerUiIsGod"))data.set("god_level",Value(network::osd_boolean(god.get("ViewerUiIsGod"))?200:0));}
    data.set("always_run",Value(always_run));data.set("prey_agent",uuid(prey_agent));data.set("agent_access",std::to_string(agent_access));data.set("agent_cof",uuid(agent_cof));data.set("crossingflags",Value(static_cast<std::int32_t>(crossing_flags)));if(crossing_flags!=0)data.set("crossExtraFlags",Value(static_cast<std::int32_t>(cross_extra_flags)));
    data.set("active_group_id",uuid(active_group_id));data.set("active_group_name",active_group_name.value_or(""));if(active_group_title)data.set("active_group_title",*active_group_title);
    if(children_cap_seeds&&!children_cap_seeds->empty()){auto seeds=array();for(const auto& [handle,seed]:*children_cap_seeds){auto pair=Value::object({});pair.set("handle",std::to_string(handle));pair.set("seed",seed.value_or(""));seeds.array.push_back(std::move(pair));}data.set("children_seeds",std::move(seeds));}
    if(animations&&!animations->empty()){auto values=array();for(const auto& animation:*animations){required(animation);values.array.push_back(animation->pack());}data.set("animations",std::move(values));}
    if(default_animation)data.set("default_animation",default_animation->pack());if(animation_state)data.set("animation_state",animation_state->pack());
    required(movement_animation_overrides);if(!movement_animation_overrides->empty()){auto values=array();auto enumerated=movement_animation_overrides;for(const auto& [state,id]:*enumerated){auto pair=Value::object({});pair.set("state",state);pair.set("uuid",uuid(id));values.array.push_back(std::move(pair));}data.set("movementAO",std::move(values));}
    if(motion_state!=0)data.set("motion_state",Value(static_cast<std::int32_t>(motion_state)));if(appearance)data.set("packed_appearance",appearance->pack(context));
    if(controllers&&!controllers->empty()){auto values=array();for(const auto& control:*controllers){required(control);values.array.push_back(control->pack());}data.set("controllers",std::move(values));}
    if(callback_uri&&!callback_uri->empty())data.set("callback_uri",*callback_uri);if(new_callback_uri&&!new_callback_uri->empty())data.set("cb_uri",*new_callback_uri);
    if(attachment_objects){auto values=array();std::size_t index=0;auto enumerated=attachment_objects;for(auto object:*enumerated){required(object);auto info=Value::object({});info.set("sog",object->to_xml2());info.set("extra",object->extra_to_xml_string());info.set("modified",Value(object->has_group_changed()));required(attachment_object_states);info.set("state",attachment_object_states->get(index++).value_or(""));values.array.push_back(std::move(info));}data.set("attach_objects",std::move(values));}
    data.set("parent_part",uuid(parent_part));data.set("sit_offset",vector3_string(sit_offset));if(cached_friends_online&&!cached_friends_online->empty()){auto values=array();auto enumerated=cached_friends_online;for(const auto& id:*enumerated)values.array.push_back(uuid(id));data.set("cfonline",std::move(values));}return data;
}
void AgentData::unpack(Value& input,AgentTransferScene* scene,EntityTransferContext* context){
    if(input.reference){unpack(*input.reference,scene,context);return;}if(input.kind!=Value::Kind::Map)throw std::invalid_argument("AgentData requires an OSD map");auto& data=input;
    if(data.contains("region_id"))try_parse_sdk_uuid(network::osd_string(data.get("region_id")),region_id);if(data.contains("circuit_code"))try_parse_unsigned_integer(network::osd_string(data.get("circuit_code")),circuit_code);
    auto id=[&](const char* key,Uuid& value){if(data.contains(key))value=network::osd_uuid(data.get(key));};id("agent_uuid",agent_id);id("session_uuid",session_id);
    auto vector=[&](const char* key,Vector3& value){if(data.contains(key))parse_vector3(network::osd_string(data.get(key)),value);};vector("position",position);vector("velocity",velocity);vector("center",center);vector("size",size);vector("at_axis",at_axis);vector("left_axis",left_axis);vector("up_axis",up_axis);
    if(data.contains("wait_for_root"))sender_wants_to_wait_for_root=network::osd_boolean(data.get("wait_for_root"));if(data.contains("far"))far=static_cast<float>(network::osd_real(data.get("far")));if(data.contains("aspect"))aspect=static_cast<float>(network::osd_real(data.get("aspect")));if(data.contains("throttles"))throttles=network::osd_binary(data.get("throttles"));
    if(data.contains("locomotion_state"))try_parse_unsigned_integer(network::osd_string(data.get("locomotion_state")),locomotion_state);if(data.contains("head_rotation"))parse_quaternion(network::osd_string(data.get("head_rotation")),head_rotation);if(data.contains("body_rotation"))parse_quaternion(network::osd_string(data.get("body_rotation")),body_rotation);if(data.contains("control_flags"))try_parse_unsigned_integer(network::osd_string(data.get("control_flags")),control_flags);if(data.contains("energy_level"))energy_level=static_cast<float>(network::osd_real(data.get("energy_level")));
    if(data.contains("god_data")){auto& value=data.map.at("god_data");if(!value.reference){auto node=std::make_shared<Value>(std::move(value));value=Value::alias(node);}god_data=value.reference;}
    if(data.contains("always_run"))always_run=network::osd_boolean(data.get("always_run"));id("prey_agent",prey_agent);if(data.contains("agent_access"))try_parse_unsigned_integer(network::osd_string(data.get("agent_access")),agent_access);id("agent_cof",agent_cof);
    if(data.contains("crossingflags"))crossing_flags=static_cast<std::uint8_t>(network::osd_integer(data.get("crossingflags")));if(crossing_flags!=0&&data.contains("crossExtraFlags"))cross_extra_flags=static_cast<std::uint8_t>(network::osd_integer(data.get("crossExtraFlags")));id("active_group_id",active_group_id);if(data.contains("active_group_name"))active_group_name=network::osd_string(data.get("active_group_name"));if(data.contains("active_group_title"))active_group_title=network::osd_string(data.get("active_group_title"));
    if(data.contains("children_seeds")){const auto& seeds=data.get("children_seeds").view();if(seeds.kind==Value::Kind::Array){children_cap_seeds=std::make_shared<ChildrenSeeds>();for(const auto& item:seeds.array){const auto& pair=item.view();if(pair.kind!=Value::Kind::Map||!pair.contains("handle"))continue;std::uint64_t handle;if(!try_parse_unsigned_integer(network::osd_string(pair.get("handle")),handle))continue;if(pair.contains("seed")){if(!children_cap_seeds->contains(handle))(*children_cap_seeds)[handle]=network::osd_string(pair.get("seed"));}else{if(children_cap_seeds->contains(handle))throw std::invalid_argument("Duplicate child seed handle");(*children_cap_seeds)[handle]="";}}}}
    if(data.contains("animations")){const auto& values=data.get("animations").view();if(values.kind==Value::Kind::Array){animations=std::make_shared<std::vector<std::shared_ptr<Animation>>>(values.array.size());std::size_t i=0;for(const auto& value:values.array)if(value.view().kind==Value::Kind::Map)(*animations)[i++]=std::make_shared<Animation>(&value);}}
    if(data.contains("default_animation")&&data.get("default_animation").view().kind==Value::Kind::Map){try{default_animation=std::make_shared<Animation>(&data.get("default_animation"));}catch(const std::exception&){default_animation.reset();}}
    if(data.contains("animation_state")&&data.get("animation_state").view().kind==Value::Kind::Map){try{animation_state=std::make_shared<Animation>(&data.get("animation_state"));}catch(const std::exception&){animation_state.reset();}}
    required(movement_animation_overrides);movement_animation_overrides->clear();if(data.contains("movementAO")){const auto& values=data.get("movementAO").view();if(values.kind==Value::Kind::Array)for(const auto& value:values.array){const auto& pair=value.view();if(pair.kind==Value::Kind::Map&&pair.contains("state")&&pair.contains("uuid"))(*movement_animation_overrides)[network::osd_string(pair.get("state"))]=network::osd_uuid(pair.get("uuid"));}}
    if(data.contains("motion_state"))motion_state=static_cast<std::uint8_t>(network::osd_integer(data.get("motion_state")));
    if(data.contains("packed_appearance")&&data.get("packed_appearance").view().kind==Value::Kind::Map)appearance=std::make_shared<AvatarAppearance>(&data.get("packed_appearance"));
    else{
        appearance=std::make_shared<AvatarAppearance>();if(data.contains("texture_entry")){auto bytes=network::osd_binary(data.get("texture_entry"));appearance->set_texture_entries(std::make_shared<TextureEntry>(std::span<const std::uint8_t>(*bytes)));}if(data.contains("visual_params"))appearance->set_visual_params(network::osd_binary(data.get("visual_params")));
        if(data.contains("wearables")){const auto& wears=data.get("wearables").view();if(wears.kind==Value::Kind::Array)for(std::size_t i=0;i<wears.array.size()/2;++i){if(wears.array[i].view().kind!=Value::Kind::Array)throw std::invalid_argument("Legacy wearable requires an OSD array");appearance->set_wearable(static_cast<int>(i),std::make_shared<AvatarWearable>(wears.array[i]));}}
        if(data.contains("attachments")){const auto& values=data.get("attachments").view();if(values.kind==Value::Kind::Array)for(const auto& value:values.array)if(value.view().kind==Value::Kind::Map)appearance->append_attachment(std::make_shared<AvatarAttachment>(value));}
    }
    if(data.contains("controllers")){const auto& values=data.get("controllers").view();if(values.kind==Value::Kind::Array){controllers=std::make_shared<std::vector<std::shared_ptr<ControllerData>>>(values.array.size());std::size_t i=0;for(const auto& value:values.array)if(value.view().kind==Value::Kind::Map)(*controllers)[i++]=std::make_shared<ControllerData>(&value);}}
    if(data.contains("callback_uri"))callback_uri=network::osd_string(data.get("callback_uri"));if(data.contains("cb_uri"))new_callback_uri=network::osd_string(data.get("cb_uri"));
    if(data.contains("attach_objects")){const auto& values=data.get("attach_objects").view();if(values.kind==Value::Kind::Array){attachment_objects=std::make_shared<TransferList<std::shared_ptr<AgentTransferObject>>>();attachment_object_states=std::make_shared<TransferList<std::optional<std::string>>>();for(const auto& value:values.array){const auto& info=value.view();if(info.kind!=Value::Kind::Map)continue;if(!scene)throw std::invalid_argument("Null transfer scene");auto object=scene->deserialize_object(network::osd_string(info.get("sog")));required(object);object->extra_from_xml_string(network::osd_string(info.get("extra")));object->has_group_changed(network::osd_boolean(info.get("modified")));required(attachment_objects);attachment_objects->push_back(object);required(attachment_object_states);attachment_object_states->push_back(network::osd_string(info.get("state")));}}}
    id("parent_part",parent_part);vector("sit_offset",sit_offset);if(data.contains("cfonline")){const auto& values=data.get("cfonline").view();if(values.kind==Value::Kind::Array){cached_friends_online=std::make_shared<TransferList<Uuid>>();for(const auto& value:values.array)cached_friends_online->push_back(network::osd_uuid(value));}}
}
void AgentData::dump(std::ostream& output)const{output<<"------------ AgentData ------------\nUUID: "<<agent_id.str()<<"\nRegion: "<<region_id.str()<<"\nPosition: "<<vector3_string(position)<<'\n';}
void AgentData::dump()const{dump(std::cout);}
}
