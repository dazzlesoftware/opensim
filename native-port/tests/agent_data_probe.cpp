// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/agent_data.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/network/osd_json.hpp"
#include <bit>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim;using network::Value;using framework::AgentData;
std::string hex(std::string_view s){std::string out;const char* h="0123456789abcdef";for(unsigned char c:s){out+=h[c>>4];out+=h[c&15];}return out;}
std::string decode(const std::string& s){std::string out;for(std::size_t i=0;i<s.size();i+=2)out+=static_cast<char>(std::stoi(s.substr(i,2),nullptr,16));return out;}
framework::Uuid id(int n){return framework::Uuid("00000000-0000-0000-0000-00000000000"+std::to_string(n));}
std::string bits(float n){std::ostringstream s;s<<std::hex<<std::setw(8)<<std::setfill('0')<<(std::isnan(n)?0x7fc00000u:std::bit_cast<std::uint32_t>(n));return s.str();}
std::string vector(framework::Vector3 v){return bits(v.x)+","+bits(v.y)+","+bits(v.z);}
std::string quaternion(framework::Quaternion v){return bits(v.x)+","+bits(v.y)+","+bits(v.z)+","+bits(v.w);}
std::string optional(const std::optional<std::string>& s){return s?hex(*s):"-";}
struct Context {AgentData* owner=nullptr;Value* command=nullptr;std::string trace;int next=0;bool mutated=false;void log(const std::string& action,int index,const std::string& text=""){trace+=action+std::to_string(index)+":"+hex(text)+";";if(network::osd_string(command->get("failure"))==action&&network::osd_integer(command->get("fail_index"))==index)throw std::runtime_error("controlled scene failure");}};
struct Object:framework::AgentTransferObject {Context& ctx;int index;std::string xml,extra="initial-extra";bool changed=false;Object(Context& c,int i,std::string x):ctx(c),index(i),xml(std::move(x)){}std::string to_xml2()override{ctx.log("xml",index);auto mode=network::osd_string(ctx.command->get("mutation"));if(!ctx.mutated&&!mode.empty()){ctx.mutated=true;if(mode=="add")ctx.owner->attachment_objects->push_back(std::make_shared<Object>(ctx,9,"new"));else if(mode=="replace")ctx.owner->attachment_objects=std::make_shared<framework::TransferList<std::shared_ptr<framework::AgentTransferObject>>>();else if(mode=="set_same")ctx.owner->attachment_objects->set(0,ctx.owner->attachment_objects->get(0));else if(mode=="agent")ctx.owner->agent_id=id(9);}return xml;}std::string extra_to_xml_string()override{ctx.log("extra-to",index);return extra;}void extra_from_xml_string(const std::string& value)override{ctx.log("extra-from",index,value);extra=value;}bool has_group_changed()const override{ctx.log("modified-get",index);return changed;}void has_group_changed(bool value)override{ctx.log("modified-set",index,value?"1":"0");changed=value;}};
struct Scene:framework::AgentTransferScene {Context& ctx;explicit Scene(Context& c):ctx(c){}std::shared_ptr<framework::AgentTransferObject> deserialize_object(const std::string& xml)override{const int index=ctx.next++;ctx.log("deserialize",index,xml);if(network::osd_boolean(ctx.command->get("scene_returns_null")))return {};return std::make_shared<Object>(ctx,index,xml);}};
void initialize(AgentData& p,Context& ctx){p.agent_id=id(7);p.region_id=id(9);p.session_id=id(8);p.circuit_code=0xffffffff;p.position={1,2,3};p.velocity={4,5,6};p.center={7,8,9};p.size={10,11,12};p.at_axis={13,14,15};p.left_axis={16,17,18};p.up_axis={19,20,21};p.sender_wants_to_wait_for_root=true;p.far=123.5f;p.aspect=1.5f;p.throttles=std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{1,2,3});p.locomotion_state=123;p.head_rotation={.1f,.2f,.3f,.4f};p.body_rotation={-.1f,-.2f,-.3f,.4f};p.control_flags=0xffffffff;p.energy_level=.75f;p.god_data=std::make_shared<Value>(Value::object({}));p.god_data->set("ViewerUiIsGod",Value(false));p.always_run=true;p.prey_agent=id(5);p.agent_access=255;p.active_group_id=id(6);p.active_group_name="Initial";p.active_group_title="Title";p.agent_cof=id(4);p.crossing_flags=1;p.cross_extra_flags=254;p.children_cap_seeds=std::make_shared<AgentData::ChildrenSeeds>();(*p.children_cap_seeds)[9]="nine";(*p.children_cap_seeds)[7]=std::nullopt;p.animations=std::make_shared<std::vector<std::shared_ptr<framework::Animation>>>();p.animations->push_back(std::make_shared<framework::Animation>(id(7),1,id(9)));p.default_animation=std::make_shared<framework::Animation>(id(8),2,id(9));p.animation_state=std::make_shared<framework::Animation>(id(9),3,id(9));p.motion_state=3;p.parent_part=id(3);p.sit_offset={21,22,23};p.controllers=std::make_shared<std::vector<std::shared_ptr<framework::ControllerData>>>();p.controllers->push_back(std::make_shared<framework::ControllerData>(id(7),id(8),0xffffffffu,0x80000000u));p.callback_uri="oldcb";p.new_callback_uri="oldnew";p.attachment_objects=std::make_shared<framework::TransferList<std::shared_ptr<framework::AgentTransferObject>>>();p.attachment_objects->push_back(std::make_shared<Object>(ctx,0,"<old/>"));p.attachment_objects->push_back(std::make_shared<Object>(ctx,1,"<old2/>"));p.attachment_object_states=std::make_shared<framework::TransferList<std::optional<std::string>>>();p.attachment_object_states->push_back("oldstate");p.attachment_object_states->push_back(std::nullopt);(*p.movement_animation_overrides)["Walk"]=id(7);(*p.movement_animation_overrides)["Sit"]=id(8);p.cached_friends_online=std::make_shared<framework::TransferList<framework::Uuid>>();p.cached_friends_online->push_back(id(7));p.cached_friends_online->push_back({});}
std::string state(AgentData& p,framework::EntityTransferContext* context){std::string out;
out+=p.agent_id.str()+"|";
out+=p.region_id.str()+"|";
out+=p.session_id.str()+"|";
out+=std::to_string(p.circuit_code)+"|";
out+=p.prey_agent.str()+"|";
out+=p.active_group_id.str()+"|";
out+=p.agent_cof.str()+"|";
out+=p.parent_part.str()+"|";
out+=std::to_string(p.locomotion_state)+"|";
out+=std::to_string(p.control_flags)+"|";
out+=std::to_string(p.agent_access)+"|";
out+=std::to_string(p.crossing_flags)+"|";
out+=std::to_string(p.cross_extra_flags)+"|";
out+=std::to_string(p.motion_state)+"|";
out+=bits(p.far)+"|";
out+=bits(p.aspect)+"|";
out+=bits(p.energy_level)+"|";
out+=std::to_string(p.sender_wants_to_wait_for_root)+"|";
out+=std::to_string(p.always_run)+"|";
out+=vector(p.position)+"|";
out+=vector(p.velocity)+"|";
out+=vector(p.center)+"|";
out+=vector(p.size)+"|";
out+=vector(p.at_axis)+"|";
out+=vector(p.left_axis)+"|";
out+=vector(p.up_axis)+"|";
out+=vector(p.sit_offset)+"|";
out+=quaternion(p.head_rotation)+"|";
out+=quaternion(p.body_rotation)+"|";
out+=optional(p.active_group_name)+"|";
out+=optional(p.active_group_title)+"|";
out+=optional(p.callback_uri)+"|";
out+=optional(p.new_callback_uri)+"|";
out+=(p.god_data?hex(network::osd_json(*p.god_data,true)):"-")+"|";if(!p.throttles)out+="-";else for(auto byte:*p.throttles){char b=static_cast<char>(byte);out+=hex(std::string_view(&b,1));}out+='|';if(!p.children_cap_seeds)out+='-';else for(auto& [h,s]:*p.children_cap_seeds)out+=std::to_string(h)+":"+optional(s)+";";out+='|';auto animation=[](const std::shared_ptr<framework::Animation>& a){return a?hex(network::osd_json(a->pack(),true)):"-";};if(!p.animations)out+='-';else for(auto& a:*p.animations)out+=animation(a)+";";out+="|"+animation(p.default_animation)+"|"+animation(p.animation_state)+"|";if(!p.movement_animation_overrides)out+='-';else for(auto& [key,id]:*p.movement_animation_overrides)out+=hex(key)+":"+id.str()+";";out+='|';if(!p.appearance)out+='-';else try{out+=framework::md5(network::osd_json(p.appearance->pack(context),true));}catch(...){out+='!';}out+='|';if(!p.controllers)out+='-';else for(auto& c:*p.controllers)out+=(c?hex(network::osd_json(c->pack(),true)):"-")+";";out+='|';if(!p.attachment_objects)out+='-';else for(auto so:*p.attachment_objects){auto object=std::dynamic_pointer_cast<Object>(so);out+=object?std::to_string(object->index)+":"+hex(object->xml)+":"+hex(object->extra)+":"+std::to_string(object->changed)+";":"-;";}out+='|';if(!p.attachment_object_states)out+='-';else for(auto& s:*p.attachment_object_states)out+=optional(s)+";";out+='|';if(!p.cached_friends_online)out+='-';else for(auto& id:*p.cached_friends_online)out+=id.str()+";";return out;}
int main(){std::string line;while(std::getline(std::cin,line)){auto command=network::parse_osd_json(decode(line));auto data=command.get("data");const int profile=network::osd_integer(command.get("profile"));Context ctx;ctx.command=&command;std::unique_ptr<AgentData> p;if(profile&32)p=std::make_unique<framework::CompleteAgentData>();else if(profile&16)p=std::make_unique<AgentData>(static_cast<const AgentData::LegacyTable*>(nullptr));else p=std::make_unique<AgentData>();ctx.owner=p.get();if(profile&1)initialize(*p,ctx);Scene scene(ctx);framework::EntityTransferContext context;if(command.contains("version"))context.outbound_version=static_cast<float>(network::osd_real(command.get("version")));auto context_ptr=network::osd_boolean(command.get("null_context"))?nullptr:&context;if(network::osd_boolean(command.get("null_overrides")))p->movement_animation_overrides.reset();bool unpack_failed=false,pack_failed=false;std::string packed;framework::IAgentData* view=p.get();try{if(!network::osd_boolean(command.get("skip_unpack"))){if(network::osd_boolean(command.get("null_map"))){Value invalid;view->unpack(invalid,&scene,context_ptr);}else view->unpack(data,network::osd_boolean(command.get("null_scene"))?nullptr:&scene,context_ptr);}if(command.contains("look"))p->set_look_at(framework::parse_sdk_vector3(network::osd_string(command.get("look"))));if(command.contains("interface_id"))view->agent_uuid(network::osd_uuid(command.get("interface_id")));if((profile&4)&&data.contains("god_data")&&data.get("god_data").view().kind==Value::Kind::Map){data.map.at("god_data").set("ViewerUiIsGod",Value(true));data.map.at("god_data").set("after","source");}}catch(...){unpack_failed=true;}
if(command.contains("states_mode")){auto mode=network::osd_string(command.get("states_mode"));if(mode=="null")p->attachment_object_states.reset();else if(mode=="empty")p->attachment_object_states=std::make_shared<framework::TransferList<std::optional<std::string>>>();else if(mode=="null_object"&&p->attachment_objects&&!p->attachment_objects->empty())p->attachment_objects->set(0,{});}
try{auto value=view->pack(context_ptr);if((profile&4)&&p->god_data&&p->god_data->view().kind==Value::Kind::Map){p->god_data->set("ViewerUiIsGod",Value(false));p->god_data->set("after_pack","shared");}if((profile&4)&&p->throttles&&!p->throttles->empty())(*p->throttles)[0]=55;packed=network::osd_json(value,true);}catch(...){pack_failed=true;}
std::ostringstream dump;p->dump(dump);std::cout<<unpack_failed<<"|"<<pack_failed<<"|"<<hex(packed)<<"|"<<hex(state(*p,context_ptr))<<"|"<<hex(ctx.trace)<<"|"<<hex(dump.str())<<"|"<<view->agent_uuid().str()<<"\n";}}
