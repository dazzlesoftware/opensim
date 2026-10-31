// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/baked_texture_caps.hpp"
#include "opensim/framework/ip_address.hpp"
namespace opensim::services {
namespace {
std::string xml(std::string_view value){std::string result;for(char c:value)switch(c){case '&':result+="&amp;";break;case '<':result+="&lt;";break;case '>':result+="&gt;";break;default:result+=c;}return result;}
bool same_ip(const std::string& a,const std::string& b){auto x=framework::IpAddress::parse(a),y=framework::IpAddress::parse(b);return x&&y&&x->v6()==y->v6()&&x->bytes()==y->bytes()&&(!x->v6()||x->scope_id()==y->scope_id());}
}
BakedTextureUpload::BakedTextureUpload(std::string path,framework::Uuid agent,std::string ip,BakedTextureCapsBackend backend):path_(std::move(path)),agent_(agent),remote_ip_(std::move(ip)),backend_(std::move(backend)){
 if(backend_.timer)backend_.timer(path_,30000);
}
void BakedTextureUpload::expire(){if(backend_.remove_upload)backend_.remove_upload(path_);if(backend_.dispose_timer)backend_.dispose_timer(path_);}
network::HttpResponse BakedTextureUpload::handle(const network::HttpRequest& request){
 network::HttpResponse response;response.content_type="text/html;charset=UTF-8";if(request.method!="POST"){response.status=404;return response;}if(request.body.empty()){response.status=400;return response;}if(request.body.size()>6000000){response.status=413;return response;}
 // SimpleBinaryHandler catches exceptions from the timer and registry operations.
 try{
  if(backend_.timer)backend_.timer(path_,0);if(backend_.remove_upload)backend_.remove_upload(path_);if(backend_.dispose_timer)backend_.dispose_timer(path_);
  if(!same_ip(request.remote_ip,remote_ip_)){response.status=401;return response;}
  // The source catches only asset creation/cache/serialization failures as400.
  try{
   auto id=backend_.new_id?backend_.new_id():framework::Uuid::random();auto asset=std::make_shared<framework::AssetBase>(id,"Baked Texture",std::int8_t(0),agent_.str());
   asset->data=std::make_shared<std::vector<std::uint8_t>>(request.body.begin(),request.body.end());asset->temporary(true);asset->local(true);
   if(!backend_.cache)throw std::runtime_error("Baked texture cache unavailable");backend_.cache->cache(asset);
   response.body="<llsd><map><key>new_asset</key><string>"+id.str()+"</string><key>new_inventory_item</key><uuid>00000000-0000-0000-0000-000000000000</uuid><key>state</key><string>complete</string><key>new_next_owner_mask</key><integer>0</integer><key>new_group_mask</key><integer>0</integer><key>new_everyone_mask</key><integer>0</integer><key>inventory_item_flags</key><integer>0</integer></map></llsd>";response.status=200;
  }catch(...){response.status=400;}
 }catch(...){response.status=500;}return response;
}
BakedTextureCaps::BakedTextureCaps(std::string path,framework::Uuid agent,std::string origin,BakedTextureCapsBackend backend):path_(std::move(path)),agent_(agent),origin_(std::move(origin)),backend_(std::move(backend)){}
network::HttpResponse BakedTextureCaps::handle(const network::HttpRequest& request){
 network::HttpResponse response;response.content_type="text/html;charset=UTF-8";if(request.method!="POST"){response.status=404;return response;}
 try{auto id=backend_.new_id?backend_.new_id():framework::Uuid::random();auto path="/"+id.str()+"-BK";auto upload=std::make_shared<BakedTextureUpload>(path,agent_,request.remote_ip,backend_);if(!backend_.register_upload)throw std::runtime_error("Upload listener unavailable");backend_.register_upload(upload);
  response.body="<llsd><map><key>uploader</key><string>"+xml(origin_+path)+"</string><key>state</key><string>upload</string><key>upload_price</key><integer>0</integer></map></llsd>";response.status=200;
 }catch(const std::exception& error){if(backend_.log)backend_.log(error.what());response.status=400;}catch(...){if(backend_.log)backend_.log("");response.status=400;}return response;
}
}
