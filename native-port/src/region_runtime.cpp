// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/services/scene_client_verification.hpp"
#include "opensim/scene/avatar_update.hpp"
#include "opensim/scene/avatar_packets.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/field_string.hpp"
#include "opensim/scene/object_add.hpp"
#include "opensim/scene/object_update.hpp"
#include "opensim/scene/rez_location.hpp"
#include "opensim/scene/chat_packet.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/scene/viewer_physics.hpp"
#include "opensim/scene/root_position.hpp"
#include "opensim/scene/task_inventory_file.hpp"
#include "opensim/scene/xfer_download.hpp"
#include "opensim/scene/script_running.hpp"
#include "opensim/scene/script_task_update.hpp"
#include "opensim/scene/script_control.hpp"
#include "opensim/scene/task_inventory_registry.hpp"
#include "opensim/scene/inventory_script_rez.hpp"
#include "opensim/scene/script_instance_create.hpp"
#include "opensim/scene/task_inventory_move.hpp"
#include "opensim/scene/task_inventory_remove.hpp"
#include "opensim/scene/task_inventory_update.hpp"
#include "opensim/scene/inventory_drop_permission.hpp"
#include "opensim/scene/user_inventory_operations.hpp"
#include "opensim/scene/inventory_copy.hpp"
#include "opensim/scene/inventory_asset_update.hpp"
#include "opensim/scene/scene_inventory_asset_update.hpp"
#include "opensim/scene/agent_inventory_add.hpp"
#include "opensim/scene/inventory_notifications.hpp"
#include "opensim/scene/inventory_item_create.hpp"
#include "opensim/script/asset_text.hpp"
#include "opensim/scene/object_properties.hpp"
#include <future>
#include <bit>
#include <cmath>
#include <algorithm>
#include <thread>
#include <deque>
namespace opensim::scene {
using namespace framework;using namespace services;using namespace network;
namespace {
class Actor final:public AdmissionPresence {
public:
 Uuid id;std::atomic<bool> root=false;std::atomic<AdmissionPresenceState> status=AdmissionPresenceState::running;
 explicit Actor(Uuid value):id(value){}
 Uuid agent_id()const override{return id;}bool deleted()const override{return status==AdmissionPresenceState::removed;}
 bool child()const override{return !root;}AdmissionPresenceState state()const override{return status;}
 void state(AdmissionPresenceState value)override{status=value;}
 void do_not_close_after_teleport(bool)override{}bool has_client()const override{return true;}
 void teleport_flags(std::uint32_t)override{}void adjust_known_seeds()override{}
};
}
struct RegionRuntime::State:SceneAdmissionBackend,SceneCapabilityBackend,SceneClientVerificationBackend,std::enable_shared_from_this<RegionRuntime::State> {
 using SceneClientVerificationBackend::remove_circuit;
 GridRegion descriptor;IPresenceService& sessions;RegionPolicies policies;AccessRegion access_region;
 World world;mutable AgentCircuitManager circuits;std::unique_ptr<UdpSocket> socket;
 std::unique_ptr<ViewerRegion> viewers;std::unique_ptr<SceneCapabilities> capability_runtime;
 std::unique_ptr<ViewerPhysics> physics;
 std::unique_ptr<LegacyInventoryRequests> legacy_inventory;
 struct InventoryCommand {ViewerAgent agent;std::vector<LegacyItemRequest> items;LegacyFolderRequest folder;};
 std::mutex inventory_mutex;std::deque<InventoryCommand> inventory_items,inventory_folders;
 bool inventory_items_running=false,inventory_folders_running=false;
 std::chrono::steady_clock::time_point next_inventory_folder{};
 std::shared_ptr<server::RegistryCapabilityListener> registry;
 SceneAdmission admission;SceneClientVerification verification;
 std::recursive_mutex remove_mutex,new_mutex;std::mutex actors_mutex;
 std::map<Uuid,std::shared_ptr<Actor>> actors;std::atomic<bool> running=false;
 struct ObjectState {std::uint32_t crc=0;bool selected=false;};
 std::mutex objects_mutex;std::map<Uuid,ObjectState> object_state;
 struct ScriptCommand {Uuid actor,object;std::optional<services::InventoryItem> item;std::string event;};
 struct RunningScript {services::InventoryItem item;std::unique_ptr<script::LslInstance> instance;std::shared_ptr<framework::TaskInventoryItem> task;};
 std::mutex commands_mutex;std::vector<ScriptCommand> script_commands;
 // Instances and task-script metadata are owned by the host poll thread.
 std::map<Uuid,RunningScript> scripts;
 struct TaskPart {std::unique_ptr<TaskInventoryRegistry> inventory;std::unique_ptr<TaskInventoryFile> file;std::unique_ptr<PartScriptEvents> events;bool scripted=false;};
 struct TaskCommand {ViewerAgent actor;lludp::Packet packet;std::int32_t creation_date=0;};
 mutable std::recursive_mutex task_data_mutex;
 struct ScriptUpdateJob {Uuid agent,item,object;bool running;AssetBytes data;std::promise<std::vector<std::string>> result;};
 struct InventoryAssetJob {Uuid agent,item,object;AssetBytes data;std::promise<Uuid> result;};
 std::deque<std::shared_ptr<InventoryAssetJob>> inventory_asset_jobs;
 std::atomic<std::int32_t> active_scripts{0};
 bool script_updates_open=false;std::deque<std::shared_ptr<ScriptUpdateJob>> script_update_jobs;
 std::map<Uuid,TaskPart> task_parts;std::vector<TaskCommand> task_commands;
 struct InventoryScriptCommand {ViewerAgent actor;Uuid item;std::uint32_t local;};std::vector<InventoryScriptCommand> inventory_script_commands;
 std::optional<ViewerAgent> task_recipient;std::unique_ptr<XferFiles> xfer;
 static std::int32_t xfer_tick(){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));}

 State(GridRegion r,IPresenceService& s,RegionPolicies p):descriptor(std::move(r)),sessions(s),policies(std::move(p)),world(descriptor.id,descriptor.size_x,descriptor.size_y),admission(*this),verification(*this){
  if(!policies.access||!policies.authorize)throw std::invalid_argument("Region access and authorization policies are required");
  access_region.id=descriptor.id;access_region.scope=descriptor.scope;access_region.name=descriptor.name;access_region.size_x=descriptor.size_x;access_region.size_y=descriptor.size_y;
  world.terrain().clear_land();
  xfer=std::make_unique<XferFiles>(xfer_tick);
  if(policies.inventory)legacy_inventory=std::make_unique<LegacyInventoryRequests>(*policies.inventory);
 }
 AccessRegion& region()override{return access_region;}
 std::recursive_mutex& remove_client_mutex()override{return remove_mutex;}std::recursive_mutex& new_user_mutex()override{return new_mutex;}
 void log_incoming(const std::shared_ptr<AgentCircuitData>& c,std::uint32_t,const GridRegion*,const std::string&)override{debug("Incoming agent "+c->agent_id.str());}
 bool logins_enabled()const override{return running;}
 std::vector<std::optional<std::string>> allowed_viewers()const override{return {};}
 std::vector<std::optional<std::string>> banned_viewers()const override{return {};}
 std::shared_ptr<AdmissionPresence> presence(const Uuid& a)override{std::lock_guard lock(actors_mutex);auto i=actors.find(a);return i==actors.end()?nullptr:i->second;}
 bool in_transit(const Uuid&)override{return false;}void sleep(int ms)override{std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
 bool telehub_allow_landmarks()const override{return true;}
 bool check_land_access(const Uuid& a,bool,bool,bool,const Vector3& p,std::string& reason)override{return policies.access(a,p,reason);}
 void remove_circuit(const std::shared_ptr<AgentCircuitData>& c)override{circuits.remove_circuit(c->agent_id);}
 void close_agent(const Uuid& a,bool)override{viewers->disconnect(a);}
 void remove_cached_account(const Uuid&)override{}
 void add_circuit(const std::shared_ptr<AgentCircuitData>& c)override{circuits.add_new_circuit(c);}
 bool has_presence_service()const override{return true;}
 std::optional<AdmissionGridPresence> grid_presence(const Uuid& s)override{auto p=sessions.agent(s);return p?std::optional(AdmissionGridPresence{p->user_id}):std::nullopt;}
 bool see_into_region()const override{return true;}
 bool authorize_user(const std::shared_ptr<AgentCircuitData>& c,bool,std::string& reason)override{return policies.authorize(c,reason);}
 std::shared_ptr<AdmissionCaps> caps()override{return capability_runtime?std::shared_ptr<AdmissionCaps>(&capability_runtime->capabilities(),[](auto*){}):nullptr;}
 void cache_user_name(const std::shared_ptr<AgentCircuitData>&)override{}
 std::string scene_name()const override{return descriptor.name.value_or("");}
 std::optional<std::string> region_name()const override{return descriptor.name;}
 std::optional<std::string> external_host()const override{return descriptor.external_host;}
 std::shared_ptr<CapabilityRuntimeListener> listener()const override{return registry;}
 std::uint32_t region_size_x()const override{return descriptor.size_x;}std::uint32_t region_size_y()const override{return descriptor.size_y;}
 AgentCircuitManager* authenticate_handler()const override{return &circuits;}
 std::shared_ptr<UserAgentVerificationModule> user_verification_module()override{return {};}
 std::shared_ptr<VerifiedScenePresence> scene_presence(const Uuid&)override{return {};}
 IPresenceService* presence_service()override{return &sessions;}
 bool check_client(Uuid a,const HttpRequest& r)override{auto ip=IpAddress::parse(r.remote_ip);if(!ip)return false;IpEndpoint endpoint{std::make_shared<IpAddress>(*ip),static_cast<std::uint16_t>(r.remote_port)};return verification.check_client(a,&endpoint);}
 void debug(const std::string& s)override{if(policies.log)policies.log(s);}void warn(const std::string& s)override{debug(s);}void error(const std::string& s)override{debug(s);}void console(const std::string& s)override{debug(s);}
 void cleanup_exception(const std::exception& e)override{error(e.what());}void add_command(CapabilityCommand)override{}
 void send_avatar(const ViewerAgent& a,Uuid recipient){
  AvatarUpdate update;update.id=a.circuit->agent_id;update.local_id=a.local_id;update.position=a.position;update.velocity=a.velocity;update.rotation=a.rotation;update.flying=a.flying;update.first_name=a.circuit->first_name.value_or("");update.last_name=a.circuit->last_name.value_or("");if(a.circuit->appearance)update.size=a.circuit->appearance->avatar_size();
  if(recipient==a.circuit->agent_id)viewers->transport().enqueue_if_session(recipient,a.circuit->session_id,avatar_object_update(update,descriptor.handle(),1.f),ThrottleOutPacketType::Task);
  else viewers->transport().enqueue(recipient,avatar_object_update(update,descriptor.handle(),1.f),ThrottleOutPacketType::Task);
 }
 void send_appearance(const ViewerAgent& a,Uuid recipient){auto appearance=a.circuit->appearance;if(!appearance||!appearance->texture||!appearance->visual_params)return;auto texture=appearance->texture->bakes_bytes();viewers->transport().enqueue(recipient,avatar_appearance(a.circuit->agent_id,*appearance->visual_params,texture,appearance->hover_z),ThrottleOutPacketType::Task);}
 void entered(const ViewerAgent& a){
  {std::lock_guard lock(actors_mutex);auto i=actors.find(a.circuit->agent_id);if(i!=actors.end())i->second->root=true;}
  sessions.report(a.circuit->session_id,descriptor.id);viewers->send_terrain(a.circuit->agent_id);
  if(physics)physics->entered(a);
  for(auto& other:viewers->agents())if(other.root){send_avatar(a,other.circuit->agent_id);send_appearance(a,other.circuit->agent_id);if(other.circuit->agent_id!=a.circuit->agent_id){send_avatar(other,a.circuit->agent_id);send_appearance(other,a.circuit->agent_id);}}
  for(auto& object:world.objects())send_object(object,a.circuit->agent_id);
  if(policies.entered)policies.entered(a);
 }
 void alert(Uuid a,const std::string& text){auto p=lludp::make_packet("AlertMessage");std::vector<std::uint8_t> bytes(text.begin(),text.end());bytes.push_back(0);p.block("AlertData")["Message"]=std::move(bytes);viewers->transport().send(a,std::move(p));}
 void send_object(const SceneObject& object,Uuid recipient){
  ObjectUpdateOptions options;options.recipient=recipient;{std::lock_guard lock(objects_mutex);auto i=object_state.find(object.id);if(i!=object_state.end()){options.pseudo_crc=i->second.crc;options.create_selected=i->second.selected;if(recipient==object.owner)i->second.selected=false;}}
  options.generated_client_flags=policies.client_flags?policies.client_flags(recipient,object):object.flags;
  viewers->transport().enqueue(recipient,object_update(object,descriptor.handle(),1.f,options),ThrottleOutPacketType::Task);
 }
 void flush_objects(){for(auto& event:world.drain_events()){
  if(event.kind==WorldEventKind::Updated){std::lock_guard lock(objects_mutex);++object_state[event.object.id].crc;}
  for(auto& a:viewers->agents())if(a.root){if(event.kind!=WorldEventKind::Removed)send_object(event.object,a.circuit->agent_id);else{auto p=lludp::make_packet("KillObject");auto b=lludp::make_block(p.spec->blocks[0]);b["ID"]=std::uint64_t(event.object.local_id);p.blocks["ObjectData"].push_back(std::move(b));viewers->transport().enqueue(a.circuit->agent_id,std::move(p),ThrottleOutPacketType::Task);}}
  if(event.kind==WorldEventKind::Removed){std::lock_guard lock(objects_mutex);object_state.erase(event.object.id);}
 }}
 bool inventory_active(const ViewerAgent& a){if(!running||!viewers||!a.circuit)return false;auto current=viewers->agent(a.circuit->agent_id);return current&&!current->logout_requested&&current->circuit->session_id==a.circuit->session_id;}
 InventoryPacketSink inventory_sink(const ViewerAgent& a){return [this,a](lludp::Packet packet,bool split){viewers->transport().enqueue_if_session(a.circuit->agent_id,a.circuit->session_id,std::move(packet),ThrottleOutPacketType::Asset,split);};}
 bool inventory_packet(const ViewerAgent& a,const lludp::Packet& p){
  if(!legacy_inventory)return false;
  if(p.spec->name=="FetchInventory"){
   InventoryCommand command;command.agent=a;for(const auto& b:p.blocks.at("InventoryData"))command.items.push_back({std::get<Uuid>(b.at("OwnerID")),std::get<Uuid>(b.at("ItemID"))});
   std::lock_guard lock(inventory_mutex);inventory_items.push_back(std::move(command));inventory_items_running=true;return true;
  }
  if(p.spec->name=="FetchInventoryDescendents"){
   const auto& b=p.block("InventoryData");LegacyFolderRequest request{std::get<Uuid>(b.at("FolderID")),std::get<bool>(b.at("FetchFolders")),std::get<bool>(b.at("FetchItems"))};
   if(legacy_inventory->receive_folder(a.circuit->agent_id,request,inventory_sink(a))==LegacyFolderDisposition::Queued){InventoryCommand command;command.agent=a;command.folder=request;std::lock_guard lock(inventory_mutex);inventory_folders.push_back(std::move(command));inventory_folders_running=true;}return true;
  }
  return false;
 }
 void run_inventory(){
  if(!legacy_inventory)return;
  for(;;){std::optional<InventoryCommand> command;{std::lock_guard lock(inventory_mutex);if(!inventory_items_running)break;if(inventory_items.empty()){inventory_items_running=false;break;}command=std::move(inventory_items.front());inventory_items.pop_front();}
   try{legacy_inventory->process_items(command->agent.circuit->agent_id,command->items,[&]{return inventory_active(command->agent);},inventory_sink(command->agent));}catch(const std::exception& e){error(std::string("Legacy item inventory: ")+e.what());std::lock_guard lock(inventory_mutex);inventory_items_running=false;break;}
  }
  if(std::chrono::steady_clock::now()<next_inventory_folder)return;
  for(;;){std::optional<InventoryCommand> command;{std::lock_guard lock(inventory_mutex);if(!inventory_folders_running)break;if(inventory_folders.empty()){inventory_folders_running=false;break;}command=std::move(inventory_folders.front());inventory_folders.pop_front();}
   try{if(legacy_inventory->process_folder(command->agent.circuit->agent_id,command->folder,[&]{return inventory_active(command->agent);},inventory_sink(command->agent))){next_inventory_folder=std::chrono::steady_clock::now()+std::chrono::milliseconds(50);break;}}catch(const std::exception& e){error(std::string("Legacy folder inventory: ")+e.what());std::lock_guard lock(inventory_mutex);inventory_folders_running=false;break;}
  }
 }
 static TaskInventoryFileItem file_item(const framework::TaskInventoryItem& item){TaskInventoryFileItem out;out.item=item.item_id;out.asset=item.asset_id;out.creator=item.creator_id;out.last_owner=item.last_owner_id;out.owner=item.owner_id;out.group=item.group_id;out.creation_date=item.creation_date;out.flags=item.flags;out.base_permissions=item.base_permissions;out.current_permissions=item.current_permissions;out.group_permissions=item.group_permissions;out.everyone_permissions=item.everyone_permissions;out.next_permissions=item.next_permissions;out.type=item.type;out.inventory_type=item.inventory_type;out.name=item.name.value_or("");out.description=item.description.value_or("");return out;}
 static std::shared_ptr<framework::TaskInventoryItem> task_model(const TaskInventoryFileItem& item,Uuid parent){auto out=std::make_shared<framework::TaskInventoryItem>();out->item_id=item.item;out->asset_id=item.asset;out->creator_id=item.creator;out->last_owner_id=item.last_owner;out->owner_id=item.owner;out->group_id=item.group;out->creation_date=item.creation_date;out->flags=item.flags;out->base_permissions=item.base_permissions;out->current_permissions=item.current_permissions;out->group_permissions=item.group_permissions;out->everyone_permissions=item.everyone_permissions;out->next_permissions=item.next_permissions;out->type=item.type;out->inventory_type=item.inventory_type;out->name=item.name;out->description=item.description;out->parent_id=out->parent_part_id=parent;return out;}
 static InventoryItem service_item(const framework::TaskInventoryItem& task){InventoryItem item;item.id=task.item_id;item.asset_id=task.asset_id;item.creator_id=task.creator_id.str();item.owner=task.owner_id;item.group_id=task.group_id;item.parent=task.parent_part_id;item.creation_date=std::bit_cast<std::int32_t>(task.creation_date);item.flags=std::bit_cast<std::int32_t>(task.flags);item.base_permissions=std::bit_cast<std::int32_t>(task.base_permissions);item.current_permissions=std::bit_cast<std::int32_t>(task.current_permissions);item.group_permissions=std::bit_cast<std::int32_t>(task.group_permissions);item.everyone_permissions=std::bit_cast<std::int32_t>(task.everyone_permissions);item.next_permissions=std::bit_cast<std::int32_t>(task.next_permissions);item.asset_type=task.type;item.inventory_type=task.inventory_type;item.name=task.name.value_or("");item.description=task.description.value_or("");item.group_owned=!task.group_id.is_zero()&&task.owner_id==task.group_id;return item;}
 TaskInventoryRegistry& task_inventory(Uuid object){auto& part=task_parts[object];if(!part.inventory){auto model=world.object(object);if(!model)throw std::runtime_error("Task inventory part disappeared");part.scripted=(model->flags&0x40u)!=0;TaskInventoryRegistryHooks hooks;hooks.part=[this,object]{auto part=world.object(object);if(!part)throw std::runtime_error("Task inventory part disappeared");return TaskInventoryPart{object,part->group,part->local_id};};hooks.changed=[this,object](std::uint32_t mask,std::optional<Uuid>){for(auto& [id,script]:scripts)if(script.item.parent==object&&script.instance)script.instance->post_event({"changed",{std::bit_cast<std::int32_t>(mask)},{}});};hooks.aggregate_permissions=[this,object]{refresh_script_flags(object);};hooks.deep_permissions=[]{};hooks.clear_scripted=[this,object]{task_parts.at(object).scripted=false;refresh_script_flags(object);};hooks.schedule_full=hooks.group_changed=[this,object]{refresh_script_flags(object);};hooks.remove_script=[this,object](std::uint32_t,Uuid item){script_events(object).remove(item);scripts.erase(item);refresh_script_flags(object);};hooks.log=[this](const auto& text){error(text);};part.inventory=std::make_unique<TaskInventoryRegistry>(std::move(hooks));}return *part.inventory;}
 std::shared_ptr<framework::TaskInventoryItem> add_task_script(const InventoryItem& item){std::lock_guard data_lock(task_data_mutex);TaskInventoryFileItem task;task.item=item.id;task.asset=item.asset_id;framework::try_parse_sdk_uuid(item.creator_id,task.creator);task.owner=item.owner;task.group=item.group_id;task.creation_date=static_cast<std::uint32_t>(item.creation_date);task.flags=static_cast<std::uint32_t>(item.flags);task.base_permissions=static_cast<std::uint32_t>(item.base_permissions);task.current_permissions=static_cast<std::uint32_t>(item.current_permissions);task.group_permissions=0;task.everyone_permissions=static_cast<std::uint32_t>(item.everyone_permissions);task.next_permissions=static_cast<std::uint32_t>(item.next_permissions);task.type=item.asset_type;task.inventory_type=item.inventory_type;task.name=item.name;task.description=item.description;auto model=task_model(task,item.parent);task_inventory(item.parent).add(model,false);return model;}
 void request_task_file(const ViewerAgent& actor,std::uint32_t local){std::lock_guard data_lock(task_data_mutex);
  auto object=world.object(local);if(!object||!xfer)return;auto& inventory=task_inventory(object->id);auto& part=task_parts.at(object->id);
  if(!part.file){TaskInventoryFileBackend backend;
   backend.can_edit_inventory=[this](Uuid id,Uuid agent){auto object=world.object(id);if(!object)return false;auto& policy=policies.can_edit_inventory?policies.can_edit_inventory:policies.can_edit;return policy&&policy(agent,*object);};
   backend.add_file=[this](const auto& filename,auto bytes){return xfer->add(filename,std::move(bytes));};
   backend.reply=[this](Uuid id,std::int16_t serial,std::vector<std::uint8_t> filename){if(!task_recipient)return;auto p=task_inventory_reply(id,serial,filename);viewers->transport().enqueue_if_session(task_recipient->circuit->agent_id,task_recipient->circuit->session_id,std::move(p),ThrottleOutPacketType::Task,false);};
   part.file=std::make_unique<TaskInventoryFile>(object->id,std::move(backend));
  }
  std::vector<TaskInventoryFileItem> snapshot;for(const auto& item:inventory.items())snapshot.push_back(file_item(*item));task_recipient=actor;try{part.file->request(actor.circuit->agent_id,inventory.serial(),snapshot,policies.viewer_ui_god&&policies.viewer_ui_god(actor.circuit->agent_id));}catch(...){task_recipient.reset();throw;}task_recipient.reset();
 }
 std::optional<AgentInventoryClient> inventory_client(Uuid agent){
  auto actor=viewers->agent(agent);if(!actor||!inventory_active(*actor))return {};
  AgentInventoryClient client;client.agent=agent;auto session=actor->circuit->session_id;
  client.remove_items=[this,actor=*actor,agent,session](const std::vector<Uuid>& ids){if(!inventory_active(actor))return;InventoryNotificationBackend backend;if(capability_runtime&&capability_runtime->event_queue())backend.enqueue=[this](auto bytes,Uuid recipient){return capability_runtime->event_queue()->enqueue(std::move(bytes),recipient);};backend.debug=[this](auto text){debug(text);};send_remove_inventory_items(agent,session,&ids,backend);};
  client.bulk_item=[this,actor=*actor,agent](auto item){if(inventory_active(actor)&&capability_runtime&&capability_runtime->event_queue())capability_runtime->event_queue()->bulk_inventory(&item->item,agent);};
  client.alert=[this,actor=*actor,agent,session](const std::string& text,bool modal){if(inventory_active(actor))viewers->transport().enqueue_if_session(agent,session,agent_alert(agent,text,modal),ThrottleOutPacketType::Task);};
  client.create_update=[this,actor=*actor,agent,session](auto item,std::uint32_t callback){if(inventory_active(actor))viewers->transport().enqueue_if_session(agent,session,inventory_item_create(agent,item->item,{},callback),ThrottleOutPacketType::Asset);};
  return client;
 }
 AgentInventoryAddController agent_inventory_add(){
  if(!policies.inventory_service)throw std::runtime_error("Task inventory transfer requires an InventoryService");
  AgentInventoryAddBackend backend;backend.add_item=[this](auto item){return policies.inventory_service->add_item(inventory_storage_projection(*item));};
  backend.system_folder=[this](Uuid agent,std::int8_t type){return policies.inventory_service->for_type(agent,type);};backend.root_folder=[this](Uuid agent){return policies.inventory_service->root(agent);};backend.folder=[this](Uuid agent,Uuid id){return policies.inventory_service->folder(agent,id);};
  backend.is_god=[this](Uuid agent){return policies.inventory_is_god&&policies.inventory_is_god(agent);};backend.upload_complete=[this](auto item,int level){if(policies.inventory_upload_complete)policies.inventory_upload_complete(item->item,level);};backend.client=[this](Uuid agent){return inventory_client(agent);};backend.debug=backend.warn=[this](auto text){debug(text);};return AgentInventoryAddController(std::move(backend));
 }
 TaskInventoryMoveController task_inventory_move(){
  TaskInventoryMoveBackend backend;backend.part=[this](std::uint32_t local)->std::optional<TaskInventoryMovePart>{auto part=world.object(local);if(!part)return {};return TaskInventoryMovePart{part->id,part->owner,local};};
  backend.task_item=[this](const auto& part,Uuid key){return task_inventory(part.id).find(key);};
  backend.can_copy=[this](Uuid key,Uuid object,Uuid agent){if(policies.can_copy_task_inventory)return policies.can_copy_task_inventory(key,object,agent);auto part=world.object(object);return part&&part->owner==agent;};
  backend.propagate_permissions=[this]{return policies.propagate_permissions;};backend.bypass_permissions=[this]{return policies.bypass_inventory_permissions;};backend.unix_time=[](){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()));};
  backend.add_client=[this](const auto& client,auto item){auto target=inventory_client(client.agent);if(!target)throw std::runtime_error("Inventory transfer client disconnected");agent_inventory_add().add(*target,std::move(item));};backend.add_offline=[this](auto item){agent_inventory_add().add(std::move(item));};
  backend.client=[this](Uuid agent)->std::optional<TaskInventoryMoveClient>{auto client=inventory_client(agent);if(!client)return {};return TaskInventoryMoveClient{agent,[client=*client](const auto& text,bool modal){client.alert(text.value_or(""),modal);}};};
  backend.remove_script_events=[this](const auto& part,Uuid key){script_events(part.id).remove(key);};backend.active_script_count=[this](const auto&,int delta){active_scripts.fetch_add(delta,std::memory_order_relaxed);};backend.remove_item=[this](const auto& part,Uuid key){task_inventory(part.id).remove(key);};
  backend.debug=backend.warn=backend.error=[this](auto text){debug(text);};return TaskInventoryMoveController(std::move(backend));
 }
 void transfer_task_inventory(const ViewerAgent& actor,const lludp::Packet& packet){
  if(!policies.inventory_service)throw std::runtime_error("Task inventory transfer requires an InventoryService");
  std::lock_guard lock(task_data_mutex);auto agent=actor.circuit->agent_id,session=actor.circuit->session_id;
  auto send_alert=[this,actor,agent,session](const std::optional<std::string>& text,bool modal){if(inventory_active(actor))viewers->transport().enqueue_if_session(agent,session,agent_alert(agent,text,modal),ThrottleOutPacketType::Task);};
  const auto& data=packet.block("InventoryData");auto local=static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at("LocalID")));auto key=std::get<Uuid>(data.at("ItemID"));
  if(packet.spec->name=="MoveTaskInventory"){task_inventory_move().client_move({agent,send_alert},std::get<Uuid>(packet.block("AgentData").at("FolderID")),local,key);return;}
  TaskInventoryRemoveBackend backend;backend.part=[this](std::uint32_t local)->std::optional<TaskInventoryRemovePart>{auto part=world.object(local);if(!part)return {};return TaskInventoryRemovePart{part->id,true};};backend.can_edit=[this](Uuid object,Uuid agent){return can_edit_inventory(object,agent);};backend.group_item=[this](const auto& part,std::uint32_t local,Uuid key)->std::shared_ptr<framework::TaskInventoryItem>{auto object=world.object(part.id);return object&&object->local_id==local?task_inventory(part.id).find(key):nullptr;};
  backend.trash_folder=[this](Uuid agent)->std::optional<Uuid>{auto folder=policies.inventory_service->for_type(agent,14);if(folder)return folder->id;return {};};
  backend.move_to_user=[this](const auto& client,Uuid folder,const auto& part,Uuid key){auto object=world.object(part.id);if(!object)return TaskInventoryMoveResult{false,"Part not found"};auto result=task_inventory_move().move({client.agent,client.alert},folder,{object->id,object->owner,object->local_id},key);return TaskInventoryMoveResult{bool(result.item),result.message};};
  backend.remove_script_events=[this](const auto& part,Uuid key){script_events(part.id).remove(key);};backend.active_script_count=[this](const auto&,int delta){active_scripts.fetch_add(delta,std::memory_order_relaxed);};backend.group_remove=[this](const auto& part,std::uint32_t,Uuid key){task_inventory(part.id).remove(key);};backend.invalidate_effective=[this](const auto& part){refresh_script_flags(part.id);};backend.properties=[this](const auto& part,const auto& client){send_part_properties(part.id,client.agent);};backend.warn=[this](Uuid id,const auto& message){warn("Task inventory removal "+id.str()+": "+message.value_or(""));};
  TaskInventoryRemoveController(std::move(backend)).remove({agent,send_alert},key,local);
 }
 void update_task_inventory(const ViewerAgent& actor,const lludp::Packet& packet){
  auto request=task_inventory_update_request(packet);if(!request)return;
  std::lock_guard lock(task_data_mutex);TaskInventoryUpdateBackend backend;
  backend.part=[this](std::uint32_t local)->std::optional<TaskInventoryUpdatePart>{auto part=world.object(local);if(!part)return {};return TaskInventoryUpdatePart{part->id,part->owner,local};};backend.task_item=[this](const auto& part,Uuid key){return task_inventory(part.id).find(key);};
  backend.inventory_item=[this](Uuid agent,Uuid key){return policies.inventory?policies.inventory->item(agent,key):std::optional<InventoryItem>{};};backend.library_item=policies.library_inventory_item;
  backend.can_drop=[this](const auto& item,const auto& client,const auto& part){auto object=world.object(part.id);if(!object)return false;if(policies.can_drop_inventory)return policies.can_drop_inventory(client.agent,*object,item);InventoryDropPermissionBackend permission;permission.bypass=[this]{return policies.bypass_inventory_permissions;};permission.root_flags=[object](const auto&){return object->flags;};permission.object_permissions=[object](Uuid agent,const auto&,bool){return agent==object->owner?object->owner_mask:0u;};return can_drop_object_inventory(&item,InventoryDropPresence{client.agent,false,policies.inventory_is_god&&policies.inventory_is_god(client.agent)},InventoryDropPart{part.id,part.owner,true,false,object->owner_mask},permission);};backend.can_edit_object=[this](const auto& part,const auto& client){auto object=world.object(part.id);return object&&policies.can_edit&&policies.can_edit(client.agent,*object);};
  backend.add_group=[this](Uuid agent,const auto& part,std::uint32_t local,const auto& source,Uuid copy,bool modify){auto item=source;item.creator_id=item.creator_id.substr(0,item.creator_id.find(';'));InventoryScriptRezController(inventory_script_backend()).add_to_group(agent,{part.id,part.owner},local,item,copy,modify);};
  backend.fetch_remote_assets=policies.fetch_remote_inventory_assets;backend.properties=[this](const auto& part,const auto& client){send_part_properties(part.id,client.agent);};backend.bypass_permissions=[this]{return policies.bypass_inventory_permissions;};backend.remove_user_items=[this](const auto& client,const auto& keys){if(!policies.inventory_service)throw std::runtime_error("Inventory drop requires a user inventory service");policies.inventory_service->delete_items(client.agent,keys);};
  backend.can_edit_inventory=[this](Uuid object,Uuid agent){return can_edit_inventory(object,agent);};if(policies.task_inventory_transaction)backend.transaction_update=[this](const auto& client,const auto& part,Uuid transaction,auto current){policies.task_inventory_transaction(client.agent,part.id,transaction,std::move(current));};backend.is_god=[this](Uuid agent){return policies.inventory_is_god&&policies.inventory_is_god(agent);};backend.update_item=[this](const auto& part,auto item){return task_inventory(part.id).update(std::move(item));};backend.error=backend.warn=backend.info=[this](auto text){debug(text);};
  auto agent=actor.circuit->agent_id;TaskInventoryUpdateClient client{agent,[this,agent](Uuid group){return policies.inventory_group_member&&policies.inventory_group_member(agent,group);},[this,agent](Uuid group)->std::uint64_t{return policies.inventory_group_powers?policies.inventory_group_powers(agent,group):0;}};TaskInventoryUpdateController(std::move(backend)).update(client,request->transaction,std::move(request->item),request->local);
 }
 static InventoryCopyItem copy_item(std::optional<InventoryItem> value){if(!value)return {};auto result=std::make_shared<TaskInventoryAgentItem>();result->item=std::move(*value);auto at=result->item.creator_id.find(';');result->creator_data=at==std::string::npos?std::string{}:result->item.creator_id.substr(at+1);if(at!=std::string::npos)result->item.creator_id.resize(at);return result;}
 InventoryCopyBackend inventory_copy_backend(const ViewerAgent& actor){
  InventoryCopyBackend backend;backend.can_create_user=[this](auto type,auto agent){return !policies.can_create_user_inventory||policies.can_create_user_inventory(type,agent);};backend.has_presence=[this](auto agent){return bool(viewers->agent(agent));};backend.library=[this]()->std::optional<InventoryCopyLibrary>{if(!policies.library_inventory_owner||!policies.library_inventory_item)return {};auto owner=policies.library_inventory_owner();if(!owner)return {};return InventoryCopyLibrary{*owner,[this](Uuid key){return copy_item(policies.library_inventory_item(key));}};};backend.item=[this](Uuid agent,Uuid key){return copy_item(policies.inventory_service->item(agent,key));};backend.bypass_permissions=[this]{return policies.bypass_inventory_permissions;};backend.can_copy_user=[this](Uuid agent,Uuid key){return !policies.can_copy_user_inventory||policies.can_copy_user_inventory(agent,key);};backend.unix_time=user_inventory_unix_time;backend.add=[this](auto item){return agent_inventory_add().add(std::move(item));};
  backend.create_update=[this,actor](Uuid agent,auto item,std::uint32_t callback){if(inventory_active(actor))viewers->transport().enqueue_if_session(agent,actor.circuit->session_id,inventory_item_create(agent,item->item,{},callback),ThrottleOutPacketType::Asset);};backend.alert=[this,actor](Uuid agent,const std::string& text){if(inventory_active(actor))viewers->transport().enqueue_if_session(agent,actor.circuit->session_id,agent_alert(agent,text,false),ThrottleOutPacketType::Task);};backend.debug=backend.error=backend.warn=[this](auto text){debug(text);};return backend;
 }
 void copy_inventory_items(const ViewerAgent& actor,const lludp::Packet& packet){
  InventoryCopyController controller(inventory_copy_backend(actor));
  for(const auto& fields:packet.blocks.at("InventoryData"))controller.copy(actor.circuit->agent_id,static_cast<std::uint32_t>(std::get<std::uint64_t>(fields.at("CallbackID"))),std::get<Uuid>(fields.at("OldAgentID")),std::get<Uuid>(fields.at("OldItemID")),std::get<Uuid>(fields.at("NewFolderID")),framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("NewName"))));
 }
 void create_inventory_item(const ViewerAgent& actor,const lludp::Packet& packet,std::int32_t date){
  if(!policies.inventory_service)throw std::runtime_error("Inventory creation requires an InventoryService");
  InventoryNewBackend backend;backend.can_create=[this](auto type,auto agent){return !policies.can_create_user_inventory||policies.can_create_user_inventory(type,agent);};backend.folder=[this](auto agent,auto id){return policies.inventory_service->folder(agent,id);};backend.folder_type=[this](auto agent,auto type){return policies.inventory_service->for_type(agent,type);};
  backend.transactions=[this]()->std::optional<std::function<bool(const InventoryNewRequest&)>>{if(policies.inventory_creation_transaction)return policies.inventory_creation_transaction;return {};};backend.presence=[this](auto agent){auto presence=viewers->agent(agent);return InventoryNewPresence{bool(presence),bool(presence)};};
  backend.landmark=[this](auto agent){if(policies.inventory_landmark)return policies.inventory_landmark(agent);auto presence=viewers->agent(agent);if(!presence)throw std::runtime_error("Landmark presence disappeared");return generate_inventory_landmark(descriptor.id,presence->position,descriptor.handle());};
  backend.create_asset=[](const auto& request,auto data){auto asset=std::make_shared<framework::AssetBase>(Uuid::random(),request.name,request.asset_type,request.agent.str());asset->description(request.description);asset->data=std::make_shared<std::vector<std::uint8_t>>(std::move(data));return asset;};backend.store_asset=[this](auto asset){if(!policies.store_asset)throw std::runtime_error("Landmark creation requires an asset service");policies.store_asset(std::move(asset));};backend.environment=[this]()->std::optional<std::function<Uuid(std::uint8_t)>>{if(policies.environment_default_asset)return policies.environment_default_asset;return {};};
  auto controller=std::make_shared<InventoryCopyController>(inventory_copy_backend(actor));backend.create=[controller](const auto& request,auto asset,auto flags,auto everyone,auto group){TaskInventoryAgentItem parameters;auto& item=parameters.item;item.creator_id=request.agent.str();parameters.creator_data=std::string{};item.parent=request.folder;item.name=request.name.value_or("");item.description=request.description.value_or("");item.flags=static_cast<std::int32_t>(flags);item.asset_id=asset;item.asset_type=request.asset_type;item.inventory_type=request.inventory_type;item.base_permissions=item.current_permissions=0x9e000;item.everyone_permissions=static_cast<std::int32_t>(everyone);item.next_permissions=std::bit_cast<std::int32_t>(request.next_permissions);item.group_permissions=static_cast<std::int32_t>(group);item.creation_date=request.creation_date;controller->create(request.agent,request.callback,parameters);};backend.debug=backend.error=[this](auto message){debug("Inventory creation: "+message);};
  const auto& fields=packet.block("InventoryBlock");InventoryNewRequest request;request.agent=actor.circuit->agent_id;request.transaction=std::get<Uuid>(fields.at("TransactionID"));request.folder=std::get<Uuid>(fields.at("FolderID"));request.callback=static_cast<std::uint32_t>(std::get<std::uint64_t>(fields.at("CallbackID")));request.name=framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("Name")));request.description=framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("Description")));request.inventory_type=static_cast<std::int8_t>(std::get<std::int64_t>(fields.at("InvType")));request.asset_type=static_cast<std::int8_t>(std::get<std::int64_t>(fields.at("Type")));request.subtype=static_cast<std::uint8_t>(std::get<std::uint64_t>(fields.at("WearableType")));request.next_permissions=static_cast<std::uint32_t>(std::get<std::uint64_t>(fields.at("NextOwnerMask")));request.creation_date=date;InventoryNewController(std::move(backend)).create(std::move(request));
 }
 void link_inventory_item(const ViewerAgent& actor,const lludp::Packet& packet){
  const auto& fields=packet.block("InventoryBlock");InventoryCopyController(inventory_copy_backend(actor)).link(actor.circuit->agent_id,static_cast<std::uint32_t>(std::get<std::uint64_t>(fields.at("CallbackID"))),std::get<Uuid>(fields.at("FolderID")),std::get<Uuid>(fields.at("OldItemID")),framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("Name"))),framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("Description"))),static_cast<std::int8_t>(std::get<std::int64_t>(fields.at("InvType"))),static_cast<std::int8_t>(std::get<std::int64_t>(fields.at("Type"))));
 }
 void user_inventory_operation(const ViewerAgent& actor,const lludp::Packet& packet){
  if(!policies.inventory_service)throw std::runtime_error("User inventory operation requires an InventoryService");
  if(packet.spec->name=="LinkInventoryItem"){link_inventory_item(actor,packet);return;}
  if(packet.spec->name=="CopyInventoryItem"){copy_inventory_items(actor,packet);return;}
  UserInventoryBackend backend;backend.item=[this](Uuid agent,Uuid key)->UserInventoryItem{auto item=policies.inventory_service->item(agent,key);return item?std::make_shared<InventoryItem>(std::move(*item)):nullptr;};backend.update=[this](auto item){return policies.inventory_service->update_item(*item);};backend.move=[this](Uuid agent,UserInventoryMoveList& list){std::vector<InventoryItem> items;for(std::size_t n=0;n<list.size();++n){auto item=list.at(n);if(!item)throw std::invalid_argument("Null inventory move item");items.push_back(*item);}return policies.inventory_service->move_items(agent,items);};backend.delete_items=[this](Uuid agent,const auto& keys){policies.inventory_service->delete_items(agent,keys);};backend.delete_folders=[this](Uuid agent,const auto& keys){policies.inventory_service->delete_folders(agent,keys);};
  backend.bulk=[this,actor](Uuid agent,auto item){if(!inventory_active(actor)||!capability_runtime||!capability_runtime->event_queue())return;auto projected=*item;projected.creator_id=projected.creator_id.substr(0,projected.creator_id.find(';'));capability_runtime->event_queue()->bulk_inventory(&projected,agent);};backend.transaction=policies.user_inventory_transaction;backend.unix_time=user_inventory_unix_time;backend.creator=[](const auto& item){Uuid creator;framework::try_parse_sdk_uuid(item.creator_id.substr(0,item.creator_id.find(';')),creator);return creator;};backend.debug=backend.warn=backend.error=[this](auto text){debug(text);};UserInventoryOperations operations(std::move(backend));auto agent=actor.circuit->agent_id;
  auto ids=[&](const char* block,const char* key){std::vector<Uuid> values;for(const auto& fields:packet.blocks.at(block))values.push_back(std::get<Uuid>(fields.at(key)));return values;};
  if(packet.spec->name=="UpdateInventoryItem"){for(auto& request:user_inventory_update_requests(packet))operations.update(agent,request.transaction,request.key,std::move(request.item));}
  else if(packet.spec->name=="MoveInventoryItem"){auto list=user_inventory_move_request(agent,packet);operations.move(agent,&list);}
  else if(packet.spec->name=="RemoveInventoryItem")operations.remove_items(agent,ids("InventoryData","ItemID"));
  else if(packet.spec->name=="RemoveInventoryFolder")operations.remove_folders(agent,ids("FolderData","FolderID"));
  else{operations.remove_folders(agent,ids("FolderData","FolderID"));operations.remove_items(agent,ids("ItemData","ItemID"));}
 }
 void run_task_inventory(){
  std::vector<TaskCommand> commands;{std::lock_guard lock(commands_mutex);commands.swap(task_commands);}
  for(auto& command:commands){if(!inventory_active(command.actor))continue;auto& p=command.packet;try{
   if(p.spec->name=="CreateInventoryItem"){create_inventory_item(command.actor,p,command.creation_date);continue;}
   if(p.spec->name=="LinkInventoryItem"||p.spec->name=="CopyInventoryItem"||p.spec->name=="UpdateInventoryItem"||p.spec->name=="MoveInventoryItem"||p.spec->name=="RemoveInventoryItem"||p.spec->name=="RemoveInventoryFolder"||p.spec->name=="RemoveInventoryObjects"){user_inventory_operation(command.actor,p);continue;}
   if(p.spec->name=="UpdateTaskInventory"){update_task_inventory(command.actor,p);continue;}
   if(p.spec->name=="MoveTaskInventory"||p.spec->name=="RemoveTaskInventory"){transfer_task_inventory(command.actor,p);continue;}
   if(p.spec->name=="SetScriptRunning"||p.spec->name=="ScriptReset"){
    ScriptControlBackend backend;backend.can_edit=[this](Uuid item,Uuid object,Uuid agent){return can_edit_script(item,object,agent);};backend.can_reset=[this](Uuid object,Uuid,Uuid agent){return can_edit_inventory(object,agent);};backend.part_local_id=[this](Uuid object)->std::optional<std::uint32_t>{auto part=world.object(object);if(part)return part->local_id;return {};};
    backend.instance=[this](Uuid item)->std::optional<ScriptControlInstance>{auto it=scripts.find(item);if(it==scripts.end()||!it->second.instance)return {};return ScriptControlInstance{[this,item](bool value){scripts.at(item).instance->running(value);},[this,item](bool value){scripts.at(item).task->script_running=value;},[this,item]{auto& entry=scripts.at(item);entry.task->permissions_granter={};entry.task->permissions_mask=0;entry.instance->reset();script_events(entry.item.parent).set(item,entry.instance->event_mask());}};};
    ScriptControlController* controller=nullptr;backend.start=[&](auto local,auto item){controller->on_start(local,item);};backend.stop=[&](auto local,auto item){controller->on_stop(local,item);};backend.reset_event=[&](auto local,auto item){controller->on_reset(local,item);};ScriptControlController controls(std::move(backend));controller=&controls;auto& b=p.block("Script");auto object=std::get<Uuid>(b.at("ObjectID")),item=std::get<Uuid>(b.at("ItemID"));if(p.spec->name=="SetScriptRunning")controls.set_running(command.actor.circuit->agent_id,object,item,std::get<bool>(b.at("Running")));else controls.process_reset(command.actor.circuit->agent_id,object,item);continue;
   }
   if(p.spec->name=="GetScriptRunning"){
    ScriptRunningBackend backend;backend.find_instance=[this](Uuid item)->std::optional<ScriptRunningInstance>{auto i=scripts.find(item);if(i==scripts.end()||!i->second.instance)return {};return ScriptRunningInstance{[this,item]{return scripts.at(item).instance->running();},[this,item](bool running){scripts.at(item).task->script_running=running;}};};
    backend.reply=[this,actor=command.actor](Uuid agent,Uuid object,Uuid item,bool running){if(!inventory_active(actor))return;if(capability_runtime&&capability_runtime->event_queue())capability_runtime->event_queue()->script_running(object,item,running,agent);else viewers->transport().enqueue_if_session(agent,actor.circuit->session_id,script_running_reply_packet(object,item,running),ThrottleOutPacketType::Task);};
    GetScriptRunningController controller(std::move(backend));auto& b=p.block("Script");controller.request(command.actor.circuit->agent_id,std::get<Uuid>(b.at("ObjectID")),std::get<Uuid>(b.at("ItemID")));continue;
   }
   if(p.spec->name=="RequestTaskInventory"){request_task_file(command.actor,static_cast<std::uint32_t>(std::get<std::uint64_t>(p.block("InventoryData").at("LocalID"))));continue;}
   auto& b=p.block("XferID");auto id=std::get<std::uint64_t>(b.at("ID"));
   if(p.spec->name=="ConfirmXferPacket"){xfer->ack(id,static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("Packet"))));continue;}
   if(p.spec->name=="AbortXfer"){xfer->abort(id);continue;}
   auto metrics=viewers->transport().transfer_metrics(command.actor.circuit->agent_id,command.actor.circuit->session_id);if(!metrics)continue;auto& filename=std::get<std::vector<std::uint8_t>>(b.at("Filename"));std::string name(filename.begin(),std::find(filename.begin(),filename.end(),0));std::weak_ptr<State> weak=shared_from_this();auto agent=command.actor.circuit->agent_id,session=command.actor.circuit->session_id;int initial_ping=metrics->ping_ms;
   xfer->request(name,id,metrics->task_throttle,initial_ping,[weak,agent,session,initial_ping]{if(auto state=weak.lock()){auto metrics=state->viewers->transport().transfer_metrics(agent,session);if(metrics)return metrics->ping_ms;}return initial_ping;},[weak,agent,session](auto id,auto number,const auto& data,int offset,int length){if(auto state=weak.lock())state->viewers->transport().enqueue_if_session(agent,session,xfer_packet(id,number,data,offset,length),ThrottleOutPacketType::Task,false);});
  }catch(const std::exception& e){error(std::string("Task inventory/Xfer: ")+e.what());}}
  xfer->heartbeat();
  std::lock_guard data_lock(task_data_mutex);for(auto i=task_parts.begin();i!=task_parts.end();)if(!world.object(i->first))i=task_parts.erase(i);else ++i;
 }
 bool can_edit_inventory(Uuid object,Uuid agent)const{auto part=world.object(object);if(!part)return false;auto& policy=policies.can_edit_inventory?policies.can_edit_inventory:policies.can_edit;return policy&&policy(agent,*part);}
 bool can_edit_script(Uuid item,Uuid object,Uuid agent)const{
  auto part=world.object(object);if(!part||!can_edit_inventory(object,agent))return false;std::lock_guard lock(task_data_mutex);auto p=task_parts.find(object);if(p==task_parts.end())return false;if(!p->second.inventory)return false;auto i=p->second.inventory->find(item);if(!i)return false;if(policies.can_edit_script)return policies.can_edit_script(agent,*part,file_item(*i));return i->owner_id==agent&&(i->current_permissions&0xc000u)==0xc000u;
 }
 void send_part_properties(Uuid id,Uuid agent){auto object=world.object(id);if(!object)return;ObjectPropertiesData data;data.id=id;data.creator=object->creator;data.owner=object->owner;data.group=object->group;data.last_owner=object->last_owner;data.from_inventory_item=object->from_inventory_item;data.creation_date=object->creation_date;data.base_mask=object->base_mask;data.owner_mask=object->owner_mask;data.group_mask=object->group_mask;data.everyone_mask=object->everyone_mask;data.next_owner_mask=object->next_mask;data.sale_type=object->sale_type;data.sale_price=object->sale_price;data.category=object->category;data.name=object->name;data.description=object->description;data.touch_name=object->touch_name;data.sit_name=object->sit_name;{std::lock_guard lock(task_data_mutex);auto it=task_parts.find(id);if(it!=task_parts.end()&&it->second.inventory)data.inventory_serial=it->second.inventory->serial();}auto recipient=viewers->agent(agent);if(recipient)viewers->transport().enqueue_if_session(agent,recipient->circuit->session_id,object_properties(data),ThrottleOutPacketType::Task,false);}
 PartScriptEvents& script_events(Uuid object){auto& part=task_parts[object];if(!part.events){PartScriptEventsBackend backend;backend.has_group=backend.has_scene=[this,object]{return world.object(object).has_value();};backend.has_root=backend.has_group;backend.is_root=[] {return true;};backend.allowed_drop=[this,object]{auto part=world.object(object);return part&&(part->flags&0x10000u)!=0;};backend.remove_targets=[this](Uuid item){if(!policies.remove_script_targets)throw std::runtime_error("Script target registry is not connected");policies.remove_script_targets(item);};backend.email=[this]()->std::optional<PartEmailEvents>{return policies.script_email_events?policies.script_email_events():std::nullopt;};backend.aggregate_group=[this,object]{auto& events=*task_parts.at(object).events;if(policies.script_events_changed)policies.script_events_changed(object,events.aggregated(),events.local_flags());refresh_script_flags(object);};backend.physics_events=[](){throw std::runtime_error("Child-part script physics subscriptions are not connected");};backend.full_update=[this,object]{refresh_script_flags(object);};part.events=std::make_unique<PartScriptEvents>(object,std::move(backend));}return *part.events;}
 void refresh_script_flags(Uuid object){auto p=task_parts.find(object);bool empty=p==task_parts.end()||!p->second.inventory||p->second.inventory->items().empty();bool scripted=p!=task_parts.end()&&p->second.scripted;std::optional<std::uint32_t> flags;if(p!=task_parts.end()&&p->second.events)flags=p->second.events->local_flags();world.set_inventory_state(object,empty,scripted,flags&&(*flags&0x80u)!=0,flags);}
 void mark_scripted(Uuid object){auto& part=task_parts[object];if(!part.scripted){part.scripted=true;refresh_script_flags(object);}}

 std::unique_ptr<script::LslInstance> create_script_instance(std::shared_ptr<const script::LslProgram> program,const InventoryItem& item,const SceneObject& object,bool start_running=true){
  script::LslContext context{object.id,object.owner,object.group,item.id,object.name,object.position};script::LslHost host;std::weak_ptr<State> weak=shared_from_this();host.chat=[weak](const auto& chat){if(auto state=weak.lock())state->script_chat(chat);};host.clock_ticks=[](){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()/100+621355968000000000LL;};return std::make_unique<script::LslInstance>(std::move(program),context,std::move(host),start_running);
 }
 bool can_edit_inventory_asset(Uuid key,Uuid object,Uuid agent,bool script){
  if(script&&object.is_zero()&&policies.can_edit_user_inventory_script)return policies.can_edit_user_inventory_script(key,agent);
  if(!script&&policies.can_edit_inventory_notecard)return policies.can_edit_inventory_notecard(key,object,agent);
  if(policies.bypass_inventory_permissions)return true;
  if(object.is_zero()){auto item=policies.inventory_service?policies.inventory_service->item(agent,key):std::nullopt;return item&&item->owner==agent&&(static_cast<std::uint32_t>(item->current_permissions)&0xc000u)==0xc000u;}
  if(!can_edit_inventory(object,agent))return false;auto root=world.object(object);if(!root||(root->owner==agent&&!(root->owner_mask&0x4000u)))return false;auto part=task_parts.find(object);if(part==task_parts.end()||!part->second.inventory)return false;auto item=part->second.inventory->find(key);return item&&item->owner_id==agent&&(item->current_permissions&0xc000u)==0xc000u;
 }
 std::shared_ptr<SceneAssetClient> asset_client(const ViewerAgent& actor){
  auto client=std::make_shared<SceneAssetClient>();client->agent=actor.circuit->agent_id;
  client->agent_alert=[this,actor](const auto& text,bool modal){if(inventory_active(actor))viewers->transport().enqueue_if_session(actor.circuit->agent_id,actor.circuit->session_id,agent_alert(actor.circuit->agent_id,text,modal),ThrottleOutPacketType::Task);};
  client->alert=[this,actor](const auto& text){if(!inventory_active(actor))return;auto packet=lludp::make_packet("AlertMessage");std::vector<std::uint8_t> bytes(text.begin(),text.end());bytes.push_back(0);packet.block("AlertData")["Message"]=std::move(bytes);viewers->transport().enqueue_if_session(actor.circuit->agent_id,actor.circuit->session_id,std::move(packet),ThrottleOutPacketType::Task);};return client;
 }
 InventoryAssetUpdateBackend inventory_asset_backend(std::shared_ptr<SceneAssetClient> client){
  InventoryAssetUpdateBackend b;b.item=[this](auto agent,auto key){auto item=policies.inventory_service->item(agent,key);return item?std::make_shared<InventoryItem>(*item):nullptr;};b.edit_notecard=[this](auto item,auto object,auto agent){return can_edit_inventory_asset(item,object,agent,false);};b.edit_script=[this](auto item,auto object,auto agent){return can_edit_inventory_asset(item,object,agent,true);};b.asset_permissions=[this](auto agent,auto asset){return policies.inventory_service->asset_permissions(agent,asset);};b.create_asset=create_inventory_asset;
  b.store=[this](auto asset){if(!policies.store_inventory_asset)throw std::runtime_error("Inventory asset update requires a result-returning asset service");return policies.store_inventory_asset(std::move(asset));};b.update=[this](auto item){return policies.inventory_service->update_item(*item);};b.agent_alert=[client](auto,const auto& text,bool modal){if(!client)throw std::runtime_error("Null inventory client");client->agent_alert(text,modal);};b.alert=[client](auto,const auto& text){if(!client)throw std::runtime_error("Null inventory client");client->alert(text);};b.error=b.warn=[this](const auto& text){error(text);};return b;
 }
 SceneInventoryAssetBackend scene_asset_backend(){
  SceneInventoryAssetBackend b;b.presence=[this](auto agent)->std::optional<std::shared_ptr<SceneAssetPresence>>{auto actor=viewers->agent(agent);if(!actor||actor->logout_requested)return {};auto presence=std::make_shared<SceneAssetPresence>();presence->client=[this,actor=*actor]{return asset_client(actor);};return presence;};
  b.inventory_access=[this]()->std::optional<SceneInventoryAccessUpdate>{if(!policies.inventory_service)return {};return [this](auto client,auto item,auto data){if(!client)throw std::runtime_error("Null inventory controlling client");return InventoryAssetUpdateController(inventory_asset_backend(client)).caps_update(client->agent,item,std::move(data));};};
  b.part=[this](auto object)->std::shared_ptr<SceneAssetPart>{if(!world.object(object))return {};auto adapter=std::make_shared<SceneAssetPart>();adapter->group_deleted=[this,object]{return !world.object(object);};adapter->item=[this,object](auto key){return task_inventory(object).find(key);};adapter->update=[this,object](auto item){task_inventory(object).update(std::move(item));};return adapter;};b.edit_inventory=[this](auto object,auto agent){return can_edit_inventory(object,agent);};b.edit_notecard=[this](auto item,auto object,auto agent){return can_edit_inventory_asset(item,object,agent,false);};b.asset_permissions=[this](auto agent,auto asset){if(!policies.inventory_service)throw std::runtime_error("Inventory asset permissions require an InventoryService");return policies.inventory_service->asset_permissions(agent,asset);};b.create_asset=create_scene_inventory_asset;b.store=[this](auto asset){if(!policies.store_asset)throw std::runtime_error("Task asset update requires an asset service");policies.store_asset(std::move(asset));};b.error=[this](const auto& text){error(text);};return b;
 }
 ScriptTaskUpdateBackend script_update_backend(){
  ScriptTaskUpdateBackend backend;backend.can_edit_script=[this](Uuid item,Uuid object,Uuid agent){return can_edit_script(item,object,agent);};
  backend.find_client=[this](Uuid agent)->std::optional<ScriptTaskUpdateClient>{auto client=viewers->agent(agent);if(!client||client->logout_requested)return {};return ScriptTaskUpdateClient{agent,[this,client=*client](const auto& text,bool){if(inventory_active(client))alert(client.circuit->agent_id,text);}};};
  backend.store=[this](auto asset){if(!policies.store_asset)throw std::runtime_error("Script update requires an asset service");policies.store_asset(std::move(asset));};
  backend.find_part=[this](Uuid object)->std::optional<ScriptTaskUpdatePart>{auto part=world.object(object);if(!part)return {};ScriptTaskUpdatePart adapter;adapter.local_id=part->local_id;
   adapter.get_item=[this,object](std::uint32_t local,Uuid item)->std::optional<ScriptTaskUpdateItem>{auto part=world.object(object);if(!part||part->local_id!=local)return {};auto p=task_parts.find(object);if(p==task_parts.end()||!p->second.inventory)return {};auto row=p->second.inventory->find(item);if(!row)return {};ScriptTaskUpdateItem result;result.id=item;result.name=row->name.value_or("");result.description=row->description.value_or("");result.set_running=[row](bool value){row->script_running=value;};result.set_asset=[this,row](Uuid asset){row->asset_id=asset;auto it=scripts.find(row->item_id);if(it!=scripts.end())it->second.item.asset_id=asset;};result.asset=[row]{return row->asset_id;};return result;};
   adapter.remove_instance=[this,object](Uuid item,bool){auto p=task_parts.find(object);if(p==task_parts.end()||!p->second.inventory||!p->second.inventory->find(item))return;script_events(object).remove(item);auto it=scripts.find(item);if(it!=scripts.end())it->second.instance.reset();refresh_script_flags(object);active_scripts.fetch_add(-1,std::memory_order_relaxed);};
   adapter.update_inventory=[this,object](const auto& item){auto& inventory=task_inventory(object);if(auto row=inventory.find(item.id))inventory.update(row);};
   // This profile evaluates represented owner permissions directly; no aggregate
   // permission cache exists to invalidate yet.
   adapter.invalidate_permissions=[]{};
   adapter.send_properties=[this,object](const auto& client){send_part_properties(object,client.agent);};
   adapter.compile=[this,object](Uuid item,int,bool,const std::string&,int)->ScriptTaskErrors{return compile_task(object,item);};
   adapter.resume_scripts=[]{};return adapter;
  };
  backend.updated=[this](auto agent,auto item,auto object,bool running,auto asset){if(policies.script_updated)policies.script_updated(agent,item,object,running,asset);};backend.log=[this](const auto& text){error(text);};backend.default_engine="NativeLSL";return backend;
 }
 std::vector<std::string> compile_task_item(Uuid object,std::shared_ptr<framework::TaskInventoryItem> input,std::optional<std::string_view> fallback={},int state_source=1){auto part=world.object(object);if(!part)return {"Task part not found"};std::optional<std::vector<std::string>> diagnostics;ScriptInstanceCreateBackend backend;
  backend.can_run=[this,part](auto item){return item&&(!policies.can_run_script||policies.can_run_script(*item,*part));};backend.mark_scripted=[this,object]{mark_scripted(object);};backend.scripts_disabled=[this]{return policies.scripts_disabled;};backend.trust_binaries=[] {return false;};backend.has_saved_state=[] {return false;};
  backend.find_item=[this,object](Uuid item)->std::optional<std::shared_ptr<framework::TaskInventoryItem>>{auto p=task_parts.find(object);if(p==task_parts.end()||!p->second.inventory)return {};auto task=p->second.inventory->find(item);if(task)return task;return {};};
  backend.load_asset=[this,input,fallback](Uuid id)->std::shared_ptr<framework::AssetBase>{if(policies.load_asset)return policies.load_asset(id);if(!fallback||id!=input->asset_id)return {};auto asset=std::make_shared<framework::AssetBase>(id,input->name,std::int8_t(10),input->creator_id.str());asset->data=std::make_shared<std::vector<std::uint8_t>>(fallback->begin(),fallback->end());return asset;};
  backend.local_id=[part]{return part->local_id;};
  backend.trigger_rez=[this,object,&diagnostics](std::uint32_t,Uuid item,const std::string& source,int,bool,const std::string&,int){auto part=world.object(object);auto task=task_inventory(object).find(item);if(!part||!task)throw std::runtime_error("Script item disappeared during compilation");auto& entry=scripts[item];entry.item=service_item(*task);entry.task=task;auto compiled=script::compile_lsl(source);diagnostics=std::vector<std::string>{};if(!compiled){for(const auto& d:compiled.diagnostics)diagnostics->push_back("("+std::to_string(d.line)+","+std::to_string(d.column)+"): "+d.message);entry.instance.reset();}else{entry.instance=create_script_instance(compiled.program,entry.item,*part,task->script_running);script_events(object).set(item,entry.instance->event_mask());}refresh_script_flags(object);};
  backend.store_errors=[&diagnostics](Uuid,std::optional<std::vector<std::string>> errors){if(errors)diagnostics=std::move(*errors);else if(!diagnostics)throw std::runtime_error("Script compiler did not publish diagnostics");};backend.active_scripts=[this](int delta){active_scripts.fetch_add(delta,std::memory_order_relaxed);};backend.schedule_full=[this,object]{refresh_script_flags(object);};backend.error=[this](const auto& text){error(text);};
  ScriptInstanceCreateController controller(std::move(backend));controller.create(std::move(input),0,false,"NativeLSL",state_source);if(!diagnostics)return {"timedout waiting for errors"};return std::move(*diagnostics);
 }
 std::vector<std::string> compile_task(Uuid object,Uuid item,std::optional<std::string_view> fallback={},int state_source=1){auto p=task_parts.find(object);if(p==task_parts.end()||!p->second.inventory)return {"Task part not found"};auto task=p->second.inventory->find(item);if(!task)return {"TaskItem ID "+item.str()+" could not be found"};return compile_task_item(object,std::move(task),fallback,state_source);}
 InventoryScriptRezBackend inventory_script_backend(){InventoryScriptRezBackend backend;backend.unix_time=[] {return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());};backend.inventory_item=[this](Uuid agent,Uuid item){auto value=policies.inventory?policies.inventory->item(agent,item):std::optional<InventoryItem>{};if(value)value->creator_id=value->creator_id.substr(0,value->creator_id.find(';'));return value;};backend.library_item=[this](Uuid item){auto value=policies.library_inventory_item?policies.library_inventory_item(item):std::optional<InventoryItem>{};if(value)value->creator_id=value->creator_id.substr(0,value->creator_id.find(';'));return value;};backend.scene_part=[this](std::uint32_t local)->std::optional<InventoryScriptPart>{auto part=world.object(local);if(part)return InventoryScriptPart{part->id,part->owner};return {};};backend.group_part=[this](Uuid context,std::uint32_t local)->std::optional<InventoryScriptPart>{auto part=world.object(local);if(part&&part->id==context)return InventoryScriptPart{part->id,part->owner};return {};};backend.can_edit_inventory=[this](Uuid object,Uuid agent){return can_edit_inventory(object,agent);};backend.propagate_permissions=[this]{return policies.propagate_permissions;};backend.root_flags=[this](const auto& part){auto object=world.object(part.id);return object?object->flags:0u;};
  backend.insert_task=[this](const auto& part,auto transferred,bool drop){std::lock_guard lock(task_data_mutex);auto task=task_model(transferred->item,part.id);task->script_running=transferred->running;if(task_inventory(part.id).add(task,drop))scripts.emplace(task->item_id,RunningScript{service_item(*task),{},task});};backend.invalidate_deep_permissions=[](const auto&){};backend.compile=[this](const auto& part,Uuid item,int,bool,const std::string&,int state){auto errors=compile_task(part.id,item,{},state);for(const auto& text:errors)error("Native script "+item.str()+": "+text);};backend.new_script=[this](Uuid agent,const auto& part,Uuid item){if(policies.new_script)policies.new_script(agent,part.id,item);};backend.resume=[](const auto&){};backend.properties=[this](const auto& part,Uuid agent){send_part_properties(part.id,agent);};backend.default_engine="NativeLSL";backend.log=[this](const auto& text){error(text);};return backend;
 }
 void run_inventory_scripts(){std::vector<InventoryScriptCommand> commands;{std::lock_guard lock(commands_mutex);commands.swap(inventory_script_commands);}for(const auto& command:commands)if(inventory_active(command.actor))try{std::lock_guard lock(task_data_mutex);InventoryScriptRezController controller(inventory_script_backend());InventoryItem wire;wire.id=command.item;controller.rez(command.actor.circuit->agent_id,wire,{},command.local);}catch(const std::exception& e){error(std::string("Inventory script rez: ")+e.what());}}
 void run_script_updates(){std::deque<std::shared_ptr<ScriptUpdateJob>> jobs;std::deque<std::shared_ptr<InventoryAssetJob>> assets;{std::lock_guard lock(commands_mutex);jobs.swap(script_update_jobs);assets.swap(inventory_asset_jobs);}for(auto& job:jobs)try{std::lock_guard lock(task_data_mutex);ScriptTaskUpdateController controller(script_update_backend());auto result=controller.update(job->agent,job->item,job->object,job->running,job->data);job->result.set_value(result.value());}catch(...){job->result.set_exception(std::current_exception());}for(auto& job:assets)try{std::lock_guard lock(task_data_mutex);job->result.set_value(SceneInventoryAssetController(scene_asset_backend()).update(job->agent,job->item,job->object,job->data));}catch(...){job->result.set_exception(std::current_exception());}}
 void quiesce_updates(){std::deque<std::shared_ptr<ScriptUpdateJob>> jobs;std::deque<std::shared_ptr<InventoryAssetJob>> assets;{std::lock_guard lock(commands_mutex);script_updates_open=false;jobs.swap(script_update_jobs);assets.swap(inventory_asset_jobs);}for(auto& job:jobs)job->result.set_exception(std::make_exception_ptr(std::runtime_error("Region script updates stopped")));for(auto& job:assets)job->result.set_exception(std::make_exception_ptr(std::runtime_error("Region inventory asset updates stopped")));}
 void packet(const ViewerAgent& a,const lludp::Packet& p){
  if(p.spec->name=="CreateInventoryItem"||p.spec->name=="LinkInventoryItem"||p.spec->name=="CopyInventoryItem"||p.spec->name=="UpdateInventoryItem"||p.spec->name=="MoveInventoryItem"||p.spec->name=="RemoveInventoryItem"||p.spec->name=="RemoveInventoryFolder"||p.spec->name=="RemoveInventoryObjects"||p.spec->name=="UpdateTaskInventory"||p.spec->name=="MoveTaskInventory"||p.spec->name=="RemoveTaskInventory"||p.spec->name=="SetScriptRunning"||p.spec->name=="ScriptReset"||p.spec->name=="GetScriptRunning"||p.spec->name=="RequestTaskInventory"||p.spec->name=="RequestXfer"||p.spec->name=="ConfirmXferPacket"||p.spec->name=="AbortXfer"){std::lock_guard lock(commands_mutex);task_commands.push_back({a,p,p.spec->name=="CreateInventoryItem"?user_inventory_unix_time():0});return;}
  if(inventory_packet(a,p))return;
  if(p.spec->name=="AgentWearablesRequest"){auto appearance=a.circuit->appearance;if(appearance&&appearance->wearables)viewers->transport().enqueue(a.circuit->agent_id,avatar_wearables(a.circuit->agent_id,a.circuit->session_id,*appearance->wearables,appearance->serial),ThrottleOutPacketType::Task);for(auto& other:viewers->agents())if(other.root)send_appearance(other,a.circuit->agent_id);return;}
  if(!a.root)return;
  if(p.spec->name=="RezScript"){
   auto& b=p.block("InventoryBlock");auto source_item=std::get<Uuid>(b.at("ItemID"));if(!source_item.is_zero()){auto local=static_cast<std::uint32_t>(std::get<std::uint64_t>(p.block("UpdateBlock").at("ObjectLocalID")));std::lock_guard lock(commands_mutex);inventory_script_commands.push_back({a,source_item,local});return;}auto object=world.object(std::get<Uuid>(b.at("FolderID")));if(!object||!policies.can_edit||!policies.can_edit(a.circuit->agent_id,*object))return;
   InventoryItem item;item.id=Uuid::random();item.parent=object->id;item.owner=std::get<Uuid>(b.at("OwnerID"));item.creator_id=std::get<Uuid>(b.at("CreatorID")).str();item.group_id=object->group;item.asset_id=Uuid("2074003b-8d5f-40e5-8b20-581c1c50aedb");item.asset_type=static_cast<int>(std::get<std::int64_t>(b.at("Type")));item.inventory_type=static_cast<int>(std::get<std::int64_t>(b.at("InvType")));
   auto field=[&](const char* name){return framework::field_to_string(std::get<std::vector<std::uint8_t>>(b.at(name)));};
   item.name=field("Name");item.description=field("Description");if(framework::trim_utf8(item.description).empty())item.description.clear();item.creation_date=static_cast<int>(std::get<std::int64_t>(b.at("CreationDate")));item.base_permissions=static_cast<int>(std::get<std::uint64_t>(b.at("BaseMask")));item.current_permissions=static_cast<int>(std::get<std::uint64_t>(b.at("OwnerMask")));item.everyone_permissions=static_cast<int>(std::get<std::uint64_t>(b.at("EveryoneMask")));item.next_permissions=static_cast<int>(std::get<std::uint64_t>(b.at("NextOwnerMask")));item.flags=static_cast<int>(std::get<std::uint64_t>(b.at("Flags")));
   std::lock_guard lock(commands_mutex);script_commands.push_back({a.circuit->agent_id,object->id,std::move(item),{}});return;
  }
  if(p.spec->name=="ObjectGrab"||p.spec->name=="ObjectDeGrab"||p.spec->name=="ObjectGrabUpdate"){
   std::optional<SceneObject> object;auto& b=p.block("ObjectData");if(p.spec->name=="ObjectGrabUpdate")object=world.object(std::get<Uuid>(b.at("ObjectID")));else object=world.object(static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("LocalID"))));
   if(object){std::lock_guard lock(commands_mutex);script_commands.push_back({a.circuit->agent_id,object->id,{},p.spec->name=="ObjectGrab"?"touch_start":p.spec->name=="ObjectDeGrab"?"touch_end":"touch"});}return;
  }
  if(p.spec->name=="AgentUpdate"){if(physics)physics->input(a);else for(auto& other:viewers->agents())if(other.root)send_avatar(a,other.circuit->agent_id);return;}
  if(p.spec->name=="SetAlwaysRun"){if(physics)physics->running(a);return;}
  if(p.spec->name=="ObjectAdd"){
   auto request=object_add_request(p);
   if(request.object.shape.pcode!=9||request.object.shape.state!=0||!request.ray_target.is_zero()){alert(a.circuit->agent_id,"This native region does not yet support this rez profile.");return;}
   auto finite=[](Vector3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
   if(!finite(request.ray_start)||!finite(request.ray_end)||!finite(request.object.scale)||request.object.scale.x<=0||request.object.scale.y<=0||request.object.scale.z<=0)return;
   RezLocationRequest ray;ray.ray_start=request.ray_start;ray.ray_end=request.ray_end;ray.target=request.ray_target;ray.rotation=request.object.rotation;ray.bypass_raycast=request.bypass_raycast;ray.ray_end_is_intersection=request.ray_end_is_intersection;RezLocationBackend geometry;
   request.object.position=get_new_rez_location(ray,geometry);std::string reason;
   if(!policies.can_rez||!policies.can_rez(a.circuit->agent_id,request.object.position,reason)){alert(a.circuit->agent_id,"You cannot create objects here.");return;}
   std::lock_guard lock(objects_mutex);auto created=world.add(request.object);if(created){auto ticks=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()/100+621355968000000000LL;object_state[created->id]={static_cast<std::uint32_t>(ticks),(request.add_flags&2u)!=0};}return;
  }
  if(p.spec->name=="ObjectPosition"||p.spec->name=="ObjectScale"||p.spec->name=="ObjectRotation")for(auto& b:p.blocks.at("ObjectData")){
   auto object=world.object(static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("ObjectLocalID"))));if(!object||!policies.can_edit||!policies.can_edit(a.circuit->agent_id,*object))continue;
   const char* field=p.spec->name=="ObjectPosition"?"Position":p.spec->name=="ObjectScale"?"Scale":"Rotation";auto& values=std::get<std::vector<double>>(b.at(field));
   if(!std::all_of(values.begin(),values.end(),[](double x){return std::isfinite(x);}))continue;
   if(p.spec->name=="ObjectRotation")object->rotation={static_cast<float>(values.at(0)),static_cast<float>(values.at(1)),static_cast<float>(values.at(2)),static_cast<float>(values.at(3))};
   else{Vector3 v{static_cast<float>(values.at(0)),static_cast<float>(values.at(1)),static_cast<float>(values.at(2))};if(p.spec->name=="ObjectPosition")object->position=v;else{if(v.x<=0||v.y<=0||v.z<=0)continue;object->scale=v;}}
   world.update_transform(object->id,object->position,object->rotation,object->scale);
  }
 }
 void script_chat(const script::LslChat& chat){
  if(chat.channel!=0&&chat.type==script::LslChatType::Say)return;auto object=world.object(chat.context.object);if(!object)return;
  ChatMessage message;message.message=chat.text;message.from_name=object->name;message.source=object->id;message.owner=object->owner;message.position=object->position;message.type=chat.type==script::LslChatType::Owner?8:1;
  for(auto& recipient:viewers->agents())if(recipient.root){if(chat.type==script::LslChatType::Owner){if(recipient.circuit->agent_id!=object->owner)continue;}else{float x=recipient.position.x-object->position.x,y=recipient.position.y-object->position.y,z=recipient.position.z-object->position.z;if(x*x+y*y+z*z>400.f)continue;}viewers->transport().send(recipient.circuit->agent_id,chat_packet(message));}
 }
 void run_scripts(){
  std::vector<ScriptCommand> commands;{std::lock_guard lock(commands_mutex);commands.swap(script_commands);}
  for(auto& command:commands){auto object=world.object(command.object);if(!object)continue;
   if(command.item){
    auto item=std::move(*command.item);
    if(policies.create_script_asset)try{item.asset_id=policies.create_script_asset(command.actor,item,policies.default_script);}catch(const std::exception& e){alert(command.actor,std::string("Script asset storage failed: ")+e.what());continue;}
    auto task=add_task_script(item);if(task_inventory(object->id).find(task->item_id)==task)scripts.emplace(task->item_id,RunningScript{service_item(*task),{},task});auto errors=compile_task_item(object->id,task,std::string_view(policies.default_script));if(!errors.empty())alert(command.actor,"Native script compile failed: "+errors.front());
    if(policies.new_script)policies.new_script(command.actor,object->id,item.id);send_part_properties(object->id,command.actor);
   }else{auto mask=command.event=="touch_start"?script_event::touch_start:command.event=="touch"?script_event::touch:script_event::touch_end;if(script_events(object->id).aggregated()&mask)for(auto& [id,running_script]:scripts)if(running_script.item.parent==object->id&&running_script.instance)running_script.instance->post_event({command.event,{std::int32_t(1)},{{command.actor}}});}
  }
  for(auto i=scripts.begin();i!=scripts.end();){auto object=world.object(i->second.item.parent);if(!object){i=scripts.erase(i);continue;}if(i->second.instance){i->second.instance->set_context({object->id,object->owner,object->group,i->second.item.id,object->name,object->position});auto result=i->second.instance->run(256);if(result.status==script::LslRunStatus::Faulted)error("Native script "+i->first.str()+": "+result.error);}++i;}
 }
 void removed(const ViewerAgent& a){
  if(physics)physics->left(a);
  {std::lock_guard lock(actors_mutex);actors.erase(a.circuit->agent_id);}
  if(capability_runtime){capability_runtime->client_closed(a.circuit->agent_id);capability_runtime->capabilities().remove_caps(a.circuit->agent_id,a.circuit->circuit_code);}
  circuits.remove_circuit(a.circuit->agent_id);sessions.logout(a.circuit->session_id);if(policies.logged_out)policies.logged_out(a);
  for(auto& other:viewers->agents())if(other.root){auto p=lludp::make_packet("KillObject");auto b=lludp::make_block(p.spec->blocks[0]);b["ID"]=std::uint64_t(a.local_id);p.blocks["ObjectData"].push_back(std::move(b));viewers->transport().enqueue(other.circuit->agent_id,std::move(p),ThrottleOutPacketType::Task);}
 }
};
RegionRuntime::RegionRuntime(GridRegion r,IPresenceService& p,RegionPolicies policies):state_(std::make_shared<State>(std::move(r),p,std::move(policies))){}
RegionRuntime::~RegionRuntime(){try{stop();}catch(const std::exception& e){try{state_->error(e.what());}catch(...){}}catch(...){}}
UdpEndpoint RegionRuntime::start(std::shared_ptr<server::RegistryCapabilityListener> listener){
 auto s=state_;if(s->running)throw std::logic_error("Region already started");s->registry=std::move(listener);if(!s->registry)throw std::invalid_argument("Region capability listener required");
 s->capability_runtime=std::make_unique<SceneCapabilities>(s);
 ViewerRegionOptions options;options.name=s->descriptor.name.value_or("");options.owner=s->descriptor.estate_owner;options.x=s->descriptor.x;options.y=s->descriptor.y;options.access=s->descriptor.access;
 std::weak_ptr<State> weak=s;
 options.prepare_root=[weak](ViewerAgent& a){if(auto v=weak.lock())a.position=root_landing_position(a.position,v->descriptor.size_x,v->descriptor.size_y,a.circuit->appearance?a.circuit->appearance->avatar_height:0,[&](float x,float y){return v->world.terrain().height_at(x,y);});};
 s->viewers=std::make_unique<ViewerRegion>(s->world,s->circuits,[weak](const auto& e,auto bytes){auto v=weak.lock();return v&&v->socket&&v->socket->send_to(e,bytes);},[weak](const auto& c,const auto&,const auto& e){auto v=weak.lock();if(!v||!v->running)return false;auto p=v->sessions.agent(c->session_id);if(!p||p->user_id!=c->agent_id.str())return false;auto ip=IpAddress::parse(e.address);if(!ip)return false;IpEndpoint endpoint{std::make_shared<IpAddress>(*ip),e.port};bool login=false;if(!v->verification.verify_client(c,&endpoint,login))return false;std::lock_guard lock(v->actors_mutex);if(v->actors.contains(c->agent_id)||(!v->policies.physics_library.empty()&&!v->actors.empty()))return false;v->actors[c->agent_id]=std::make_shared<Actor>(c->agent_id);return true;},options);
 s->viewers->callbacks({},[weak](const ViewerAgent& a){if(auto v=weak.lock())v->entered(a);},[weak](const ViewerAgent& a){if(auto v=weak.lock())v->viewers->disconnect(a.circuit->agent_id);},[weak](const ViewerAgent& a){if(auto v=weak.lock())v->removed(a);},[weak](const ViewerAgent& a,const lludp::Packet& p){if(auto v=weak.lock())try{v->packet(a,p);}catch(const std::exception& e){v->error("Viewer packet "+p.spec->name+": "+e.what());throw;}});
 if(!s->policies.physics_library.empty())s->physics=std::make_unique<ViewerPhysics>(s->world,*s->viewers,s->policies.physics_library,s->policies.animations,[weak](const ViewerAgent& a){if(auto v=weak.lock())for(auto& recipient:v->viewers->agents())if(recipient.root)v->send_avatar(a,recipient.circuit->agent_id);},[weak](const std::string& error){if(auto v=weak.lock())v->error(error);});
 auto bind=s->descriptor.internal_endpoint?UdpEndpoint{s->descriptor.internal_endpoint->address,s->descriptor.internal_endpoint->port}:UdpEndpoint{"0.0.0.0",9000};
 s->socket=std::make_unique<UdpSocket>(UdpSocketOptions{bind.address,bind.port},[weak](const auto& e,auto bytes){if(auto v=weak.lock())if(v->running)v->viewers->receive(e,bytes);});
 auto bound=s->socket->start();s->descriptor.internal_endpoint=RegionEndpoint{bound.address,bound.port};s->running=true;{std::lock_guard lock(s->commands_mutex);s->script_updates_open=true;}return bound;
}
bool RegionRuntime::can_edit_task_inventory(Uuid object,Uuid agent)const{return state_->running&&state_->can_edit_inventory(object,agent);}
bool RegionRuntime::can_edit_task_script(Uuid item,Uuid object,Uuid agent)const{return state_->running&&state_->can_edit_script(item,object,agent);}
void RegionRuntime::quiesce_script_updates(){state_->quiesce_updates();}
std::int32_t RegionRuntime::active_script_count()const{return state_->active_scripts.load(std::memory_order_relaxed);}
std::vector<std::string> RegionRuntime::update_task_script(Uuid agent,Uuid item,Uuid object,bool running,std::span<const std::uint8_t> data){auto s=state_;auto job=std::make_shared<State::ScriptUpdateJob>();job->agent=agent;job->item=item;job->object=object;job->running=running;job->data=std::make_shared<std::vector<std::uint8_t>>(data.begin(),data.end());auto result=job->result.get_future();{std::lock_guard lock(s->commands_mutex);if(!s->running||!s->script_updates_open)throw std::runtime_error("Region script updates stopped");s->script_update_jobs.push_back(job);}return result.get();}
Uuid RegionRuntime::update_inventory_asset(Uuid agent,Uuid item,Uuid object,std::span<const std::uint8_t> data){auto s=state_;auto job=std::make_shared<State::InventoryAssetJob>();job->agent=agent;job->item=item;job->object=object;job->data=std::make_shared<std::vector<std::uint8_t>>(data.begin(),data.end());auto result=job->result.get_future();{std::lock_guard lock(s->commands_mutex);if(!s->running||!s->script_updates_open)throw std::runtime_error("Region inventory asset updates stopped");s->inventory_asset_jobs.push_back(job);}return result.get();}
void RegionRuntime::poll(){if(state_->running){if(state_->physics)state_->physics->poll();state_->run_inventory();state_->run_script_updates();state_->run_inventory_scripts();state_->run_scripts();state_->run_task_inventory();state_->flush_objects();state_->viewers->poll();}}
void RegionRuntime::stop(){auto s=state_;s->quiesce_updates();s->running=false;std::exception_ptr first;auto attempt=[&](auto&& cleanup){try{cleanup();}catch(...){if(!first)first=std::current_exception();}};attempt([&]{if(s->socket)s->socket->stop();});attempt([&]{if(s->viewers)s->viewers->close();});s->physics.reset();attempt([&]{if(s->capability_runtime)s->capability_runtime->shutdown();});s->capability_runtime.reset();attempt([&]{s->sessions.logout_region(s->descriptor.id);});s->scripts.clear();{std::lock_guard lock(s->task_data_mutex);s->task_parts.clear();}s->task_recipient.reset();{std::lock_guard lock(s->inventory_mutex);s->inventory_items.clear();s->inventory_folders.clear();s->inventory_items_running=s->inventory_folders_running=false;}{std::lock_guard lock(s->commands_mutex);s->script_commands.clear();s->task_commands.clear();s->inventory_script_commands.clear();}if(first)std::rethrow_exception(first);}
GridRegion RegionRuntime::descriptor()const{return state_->descriptor;}World& RegionRuntime::world(){return state_->world;}ViewerRegion& RegionRuntime::viewers(){if(!state_->viewers)throw std::logic_error("Region not started");return *state_->viewers;}
SceneCapabilities* RegionRuntime::capabilities(){return state_->capability_runtime.get();}
bool RegionRuntime::query_access(GridRegion& r,const Uuid& a,const std::optional<std::string>&,bool,const Vector3& p,std::vector<Uuid>&,LoginTransferContext&,std::string& reason){if(r.id!=state_->descriptor.id||!state_->running){reason="Region unavailable";return false;}return state_->policies.access(a,p,reason);}
bool RegionRuntime::create_agent(const GridRegion* source,GridRegion& r,const std::shared_ptr<AgentCircuitData>& c,std::uint32_t flags,LoginTransferContext&,std::string& reason){auto s=state_;if(r.id!=s->descriptor.id||!s->running){reason="Region unavailable";return false;}std::lock_guard lock(s->new_mutex);if(!s->policies.physics_library.empty()&&c){std::lock_guard actors_lock(s->actors_mutex);for(auto& [id,actor]:s->actors)if(id!=c->agent_id){reason="Experimental ubODE region currently supports one avatar";return false;}}return s->admission.new_user_connection(c,flags,source,reason);}
}
