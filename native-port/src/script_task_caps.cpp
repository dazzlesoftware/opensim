// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/script_task_caps.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/ip_address.hpp"
#include <stdexcept>
namespace opensim::services {
namespace {
std::string escape(std::string_view s){std::string r;for(char c:s)switch(c){case '&':r+="&amp;";break;case '<':r+="&lt;";break;case '>':r+="&gt;";break;default:r+=c;}return r;}
std::string str(std::string_view s){return s.empty()?"<string />":"<string>"+escape(s)+"</string>";}
std::string error(std::string_view s){return "<llsd><map><key>message</key>"+str(s)+"<key>identifier</key><uuid>00000000-0000-0000-0000-000000000000</uuid></map></llsd>";}
network::HttpResponse response(){network::HttpResponse r;r.content_type="text/html;charset=UTF-8";return r;}
}
TaskScriptUpload::TaskScriptUpload(framework::Uuid a,framework::Uuid i,framework::Uuid o,bool run,std::string p,std::string ip,ScriptTaskCapsBackend b):agent_(a),item_(i),object_(o),running_(run),path_(std::move(p)),remote_ip_(std::move(ip)),backend_(std::move(b)){if(backend_.timer)backend_.timer(path_,30000);}
TaskScriptUpload::~TaskScriptUpload(){try{expire();}catch(...){}}
bool TaskScriptUpload::stop(){
 {std::lock_guard lock(mutex_);if(!active_)return false;active_=false;}
 // Registry/timer callbacks can safely inspect active(), without lock inversion.
 if(backend_.remove_upload)backend_.remove_upload(path_);
 if(backend_.timer)backend_.timer(path_,0);
 return true;
}
void TaskScriptUpload::expire(){stop();}
bool TaskScriptUpload::active()const{std::lock_guard lock(mutex_);return active_;}
network::HttpResponse TaskScriptUpload::handle(const network::HttpRequest& req){
 auto r=response();
 if(req.method!="POST"){r.status=404;return r;}
 if(req.body.empty()){r.status=400;return r;}
 if(req.body.size()>10000000){r.status=413;return r;}
 try{
  if(!stop()){r.status=404;return r;}
  // Remote addresses must be canonical IPAddress strings supplied by transport.
  auto ip=framework::IpAddress::parse(req.remote_ip), expected=framework::IpAddress::parse(remote_ip_);
  if(!ip||!expected||ip->v6()!=expected->v6()||ip->bytes()!=expected->bytes()||(ip->v6()&&ip->scope_id()!=expected->scope_id())){r.status=401;return r;}
  if(!backend_.update){r.status=410;return r;}
  try{
   auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(req.body.data()),req.body.size());
   auto errors=backend_.update(agent_,item_,object_,running_,bytes);
   std::string out="<llsd><map><key>new_asset</key><uuid>"+item_.str()+"</uuid><key>compiled</key><boolean>"+(errors.empty()?"1":"0")+"</boolean><key>state</key><string>complete</string><key>errors</key>";
   if(errors.empty())out+="<array />";else{out+="<array>";for(const auto& e:errors)out+=str(e);out+="</array>";}
   out+="</map></llsd>";
   if(backend_.dump)backend_.dump(bytes);
   r.body=std::move(out);
  }catch(...){r.body=error("could not compile script");}
 }catch(...){r.status=500;}
 return r;
}
UpdateScriptTaskCore::UpdateScriptTaskCore(framework::Uuid a,std::string origin,ScriptTaskCapsBackend b):agent_(a),origin_(std::move(origin)),backend_(std::move(b)){
 if(!backend_.object_exists||!backend_.can_edit_inventory||!backend_.can_edit_script||!backend_.new_path||!backend_.register_upload||!backend_.remove_upload||!backend_.timer||!backend_.update)throw std::invalid_argument("Incomplete script task capability backend");
}
network::HttpResponse UpdateScriptTaskCore::handle(const network::HttpRequest& req,const network::Value& map)const{
 auto r=response();try{
  framework::Uuid item,object;bool run=false;
  try{if(map.contains("item_id"))item=network::osd_uuid(map.get("item_id"));if(map.contains("task_id"))object=network::osd_uuid(map.get("task_id"));if(map.contains("is_script_running"))run=network::osd_boolean(map.get("is_script_running"));}catch(...){}
  if(item.is_zero()||object.is_zero()){r.body=error("failed to recode request");return r;}
  if(!backend_.object_exists(object)){r.body=error("object not found");return r;}
  if(!backend_.can_edit_inventory(object,agent_)){r.body=error("No permissions to edit objec");return r;}
  if(!backend_.can_edit_script(item,object,agent_)){r.body=error("No permissions to edit script");return r;}
  auto path=backend_.new_path();auto upload=std::make_shared<TaskScriptUpload>(agent_,item,object,run,path,req.remote_ip,backend_);
  backend_.register_upload(upload);
  r.body="<llsd><map><key>uploader</key>"+str(origin_+path)+"<key>state</key><string>upload</string><key>upload_price</key><integer>0</integer></map></llsd>";
 }catch(...){if(backend_.log)backend_.log("[UpdateScriptTaskInventory]: backend exception");}return r;
}
UpdateScriptTaskRoute::UpdateScriptTaskRoute(std::string p,std::shared_ptr<UpdateScriptTaskCore> c,Deserialize decoder):path_(std::move(p)),core_(std::move(c)),deserialize_(decoder?std::move(decoder):FetchInventory2Route::deserialize_xml_json){if(!core_)throw std::invalid_argument("Null script task capability core");}
network::HttpResponse UpdateScriptTaskRoute::handle(const network::HttpRequest& req){
 ++requests_received;auto r=response();if(req.method!="POST"){r.status=404;return r;}
 network::Value map;try{map=deserialize_(req.body);if(map.view().kind!=network::Value::Kind::Map)throw std::invalid_argument("Expected map");}catch(...){r.status=400;return r;}
 try{r=core_->handle(req,map);}catch(...){r.status=500;}
 ++requests_handled;return r;
}
}

