// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_response.hpp"
#include "opensim/services/login_service.hpp"
#include "opensim/framework/identity.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <mutex>
#include <stdexcept>
namespace opensim::services {
using network::Value;
namespace {
Value array(){Value a;a.kind=Value::Kind::Array;return a;}
auto array_node(){return std::make_shared<Value>(array());}
Value raw(const LoginResponse::Text& text){return text?Value(*text):Value{};}
Value string(const LoginResponse::Text& text){return Value(text.value_or(""));}
Value uuid(const framework::Uuid& id){Value v(id.str());v.kind=Value::Kind::Uuid;return v;}
Value wrapped(Value value){auto result=array();result.array.push_back(std::move(value));return result;}
std::int32_t signed_value(std::uint32_t n){return std::bit_cast<std::int32_t>(n);}
std::int32_t epoch(double seconds){return !std::isfinite(seconds)||seconds<-2147483648.||seconds>=2147483648.?INT32_MIN:static_cast<std::int32_t>(seconds);}
std::mutex texture_mutex;LoginResponse::Text textures[]={"cce0f112-878f-4586-a2e2-a8f104bba271","dc4b9f0b-d008-45c6-96a4-01dd947ac621","ec4b9f0b-d008-45c6-96a4-01dd947ac621"};
constexpr const char* texture_names[]={"sun_texture_id","cloud_texture_id","moon_texture_id"};
}
Value LoginBuddyInfo::to_table()const{return Value::object({{"buddy_rights_has",Value(rights_have)},{"buddy_rights_given",Value(rights_given)},{"buddy_id",raw(id)}});}
void LoginBuddyList::add(const std::shared_ptr<LoginBuddyInfo>& buddy){if(std::find(buddies.begin(),buddies.end(),buddy)==buddies.end())buddies.push_back(buddy);}
Value LoginBuddyList::to_array()const{auto result=array();for(const auto& buddy:buddies){if(!buddy)throw std::invalid_argument("Null buddy");result.array.push_back(buddy->to_table());}return result;}
LoginResponse::Text LoginResponse::texture(std::size_t index){std::lock_guard lock(texture_mutex);if(index>=3)throw std::out_of_range("Texture index");return textures[index];}
void LoginResponse::texture(std::size_t index,Text value){std::lock_guard lock(texture_mutex);if(index>=3)throw std::out_of_range("Texture index");textures[index]=std::move(value);}
LoginResponse::Node LoginResponse::global_texture_hash(){static auto value=std::make_shared<Value>(Value::object({{"sun_texture_id",Value("cce0f112-878f-4586-a2e2-a8f104bba271")},{"cloud_texture_id",Value("dc4b9f0b-d008-45c6-96a4-01dd947ac621")},{"moon_texture_id",Value("ec4b9f0b-d008-45c6-96a4-01dd947ac621")}}));return value;}
LoginResponse::LoginResponse(Random random,Clock clock):clock_(std::move(clock)){
    if(!random)random=[](){return framework::Uuid::random();};if(!clock_)clock_=[](){return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();};
    login_flags_=array_node();global_textures_=array_node();event_categories_=array_node();ui_config_=array_node();classified_categories_=array_node();initial_outfit_=array_node();ui_config_hash_=std::make_shared<Value>(Value::object({}));
    inventory_root=array_node();inventory_skeleton=array_node();inventory_library=array_node();inventory_library_owner=array_node();active_gestures=array_node();
    const char* categories[]={"Shopping","Land Rental","Property Rental","Special Attraction","New Products","Employment","Wanted","Service","Personal"};for(int i=0;i<9;++i)add_classified(i+1,categories[i]);
    session=random();secure_session=random();agent=random();initial_outfit_->array.push_back(Value::object({{"folder_name","Nightclub Female"},{"gender","female"}}));
}
void LoginResponse::add_ui(std::string key,Text value){ui_config_hash_->set(std::move(key),raw(value));}
void LoginResponse::add_classified(std::int32_t id,Text name){classified_categories_->array.push_back(Value::object({{"category_name",raw(name)},{"category_id",Value(id)}}));}
Value LoginResponse::array_list_to_osd(const Node& list){
    if(!list)throw std::invalid_argument("Null response list");const auto& source=list->view();if(source.kind!=Value::Kind::Array)throw std::invalid_argument("Invalid response list");auto result=array();
    for(const auto& entry:source.array){const auto& fields=entry.view();if(fields.kind!=Value::Kind::Map)throw std::invalid_argument("Invalid response table");auto item=Value::object({});for(const auto& key:fields.ordered_keys()){const auto& value=fields.get(key).view();item.set(key,value.kind==Value::Kind::Map||value.kind==Value::Kind::Array?Value{}:value);}result.array.push_back(std::move(item));}return result;
}
Value LoginResponse::to_xml_rpc(){
    try{
        auto response=Value::object({});login_flags_->array.push_back(Value::object({{"daylight_savings",raw(dst)},{"stipend_since_login",raw(stipend_since_login)},{"gendered",raw(gendered)},{"ever_logged_in",raw(ever_logged_in)}}));
        response.set("first_name",raw(first_name));response.set("last_name",raw(last_name));response.set("agent_access",raw(agent_access));response.set("agent_access_max",raw(agent_access_max));global_textures_->array.push_back(Value::alias(global_texture_hash()));add_ui("allow_first_life",allow_first_life);ui_config_->array.push_back(Value::alias(ui_config_hash_));
        response.set("sim_port",Value(signed_value(sim_port)));response.set("sim_ip",raw(sim_address));response.set("http_port",Value(signed_value(sim_http_port)));
        response.set("agent_id",agent.str());response.set("real_id",real_id.str());response.set("session_id",session.str());response.set("secure_session_id",secure_session.str());response.set("circuit_code",Value(circuit_code));response.set("seconds_since_epoch",Value(epoch(clock_())));
        response.set("login-flags",Value::alias(login_flags_));response.set("global-textures",Value::alias(global_textures_));response.set("seed_capability",raw(seed_capability));response.set("event_categories",Value::alias(event_categories_));response.set("event_notifications",array());response.set("classified_categories",Value::alias(classified_categories_));response.set("ui-config",Value::alias(ui_config_));
        if(inventory_skeleton){response.set("inventory-skeleton",Value::alias(inventory_skeleton));response.set("inventory-root",Value::alias(inventory_root));}
        response.set("inventory-skel-lib",Value::alias(inventory_library));response.set("inventory-lib-root",Value::alias(inventory_library_root));response.set("gestures",Value::alias(active_gestures));response.set("inventory-lib-owner",Value::alias(inventory_library_owner));response.set("initial-outfit",Value::alias(initial_outfit_));response.set("start_location",raw(start_location));response.set("home",raw(home));response.set("look_at",raw(look_at));response.set("max-agent-groups",Value(max_agent_groups));response.set("message",raw(message));response.set("region_x",Value(signed_value(region_x)));response.set("region_y",Value(signed_value(region_y)));response.set("region_size_x",Value(region_size_x));response.set("region_size_y",Value(region_size_y));
        for(const auto& pair:{std::pair{"search",search_url},{"map-server-url",map_url},{"profile-server-url",profile_url},{"destination_guide_url",destinations_url},{"avatar_picker_url",avatars_url}})if(pair.second!=Text(""))response.set(pair.first,raw(pair.second));
        if(openid_url!=Text("")){response.set("openid_url",raw(openid_url));response.set("openid_token",agent.str()+":"+framework::md5(secure_session.str()));}if(buddy_list)response.set("buddy-list",buddy_list->to_array());if(currency!=Text(""))response.set("currency",raw(currency));if(classified_fee!=Text(""))response.set("classified_fee",raw(classified_fee));response.set("login","true");return response;
    }catch(const std::exception&){return login_failure("InternalError");}
}
Value LoginResponse::to_llsd()const{
    try{
        auto result=Value::object({});result.set("first_name",string(first_name));result.set("last_name",string(last_name));result.set("agent_access",string(agent_access));result.set("agent_access_max",string(agent_access_max));result.set("sim_port",Value(signed_value(sim_port)));result.set("sim_ip",string(sim_address));result.set("agent_id",uuid(agent));result.set("real_id",uuid(real_id));result.set("session_id",uuid(session));result.set("secure_session_id",uuid(secure_session));result.set("circuit_code",Value(circuit_code));result.set("seconds_since_epoch",Value(epoch(clock_())));
        result.set("login-flags",wrapped(Value::object({{"daylight_savings",string(dst)},{"stipend_since_login",string(stipend_since_login)},{"gendered",string(gendered)},{"ever_logged_in",string(ever_logged_in)}})));auto global=Value::object({});for(int i=0;i<3;++i)global.set(texture_names[i],string(texture(i)));result.set("global-textures",wrapped(std::move(global)));result.set("seed_capability",string(seed_capability));result.set("event_categories",array_list_to_osd(event_categories_));result.set("classified_categories",array_list_to_osd(classified_categories_));result.set("ui-config",wrapped(Value::object({{"allow_first_life",string(allow_first_life)}})));
        for(const auto& pair:{std::pair{"inventory-skeleton",inventory_skeleton},{"inventory-skel-lib",inventory_library},{"inventory-root",inventory_root},{"inventory-lib-root",inventory_library_root},{"inventory-lib-owner",inventory_library_owner},{"gestures",active_gestures},{"initial-outfit",initial_outfit_}})result.set(pair.first,array_list_to_osd(pair.second));
        result.set("start_location",string(start_location));result.set("home",string(home));result.set("look_at",string(look_at));result.set("max-agent-groups",Value(max_agent_groups));result.set("message",string(message));result.set("region_x",Value(signed_value(region_x)));result.set("region_y",Value(signed_value(region_y)));
        for(const auto& pair:{std::pair{"map-server-url",map_url},{"profile-server-url",profile_url},{"search",search_url},{"classified_fee",classified_fee}})if(pair.second!=Text(""))result.set(pair.first,string(pair.second));if(openid_url!=Text("")){result.set("openid_url",string(openid_url));result.set("openid_token",agent.str()+":"+framework::md5(secure_session.str()));}if(buddy_list){auto values=std::make_shared<Value>(buddy_list->to_array());result.set("buddy-list",array_list_to_osd(values));}result.set("login","true");return result;
    }catch(const std::exception&){return login_failure("InternalError");}
}
}
