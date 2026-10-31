// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/inventory_caps.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
namespace opensim::services {
InventoryBadRequests::InventoryBadRequests(std::function<std::int32_t()> clock,TimerObserver timer):clock_(std::move(clock)),timer_(std::move(timer)){if(!clock_)throw std::invalid_argument("Null inventory expiry clock");}
InventoryBadRequests::~InventoryBadRequests(){try{std::lock_guard lock(mutex_);disarm();}catch(...) {}}
void InventoryBadRequests::disarm(){if(timer_active_){if(timer_)timer_(TimerEvent::Disarm,0);timer_active_=false;}}
void InventoryBadRequests::add(framework::Uuid key,std::int32_t expire_ms){auto now=expire_ms>0?std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(clock_())+static_cast<std::uint32_t>(std::max(expire_ms,30000))):std::numeric_limits<std::int32_t>::min();std::lock_guard lock(mutex_);entries_[key]=now;if(!timer_active_){if(timer_)timer_(TimerEvent::Arm,30000);timer_active_=true;}}
bool InventoryBadRequests::contains(framework::Uuid key)const{std::lock_guard lock(mutex_);return entries_.contains(key);}
void InventoryBadRequests::purge(){std::lock_guard lock(mutex_);if(entries_.empty()){disarm();return;}auto now=clock_();for(auto it=entries_.begin();it!=entries_.end();)if(it->second>0&&it->second<now)it=entries_.erase(it);else ++it;if(entries_.empty())disarm();else if(timer_)timer_(TimerEvent::Rearm,30000);}
namespace {
void key(std::string& out,std::string_view name){out+="<key>";out+=name;out+="</key>";}
void number(std::string& out,std::string_view name,std::int32_t value){key(out,name);if(!value)out+="<integer />";else out+="<integer>"+std::to_string(value)+"</integer>";}
void uuid(std::string& out,std::string_view name,framework::Uuid value){key(out,name);out+=value.is_zero()?"<uuid />":"<uuid>"+value.str()+"</uuid>";}
std::string escape(std::string_view value){std::string out;for(unsigned char c:value)switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\'':out+="&apos;";break;default:out+=static_cast<char>(c);}return out;}
void string(std::string& out,std::string_view name,const std::string& value){key(out,name);out+=value.empty()?"<string />":"<string>"+escape(value)+"</string>";}
}
std::string inventory_item_llsd(const InventoryItem& item,std::uint32_t flags_mask){
 std::string out="<map>";uuid(out,"parent_id",item.parent);uuid(out,"asset_id",item.asset_id);uuid(out,"item_id",item.id);
 key(out,"permissions");out+="<map>";framework::Uuid creator;framework::try_parse_sdk_uuid(item.creator_id.substr(0,item.creator_id.find(';')),creator);
 // Actual SDK AddElem_group_id has the source constant "cgroup_id" for
 // nonzero groups; zero uses "group_id". Preserve this wire spelling.
 uuid(out,"creator_id",creator);uuid(out,"owner_id",item.owner);uuid(out,item.group_id.is_zero()?"group_id":"cgroup_id",item.group_id);
 number(out,"base_mask",item.current_permissions);number(out,"owner_mask",item.current_permissions);number(out,"group_mask",item.group_permissions);number(out,"everyone_mask",item.everyone_permissions);number(out,"next_owner_mask",item.next_permissions);
 key(out,"is_owner_group");out+=item.group_owned?"<boolean>1</boolean>":"<boolean>0</boolean>";out+="</map>";
 number(out,"type",item.asset_type);number(out,"inv_type",item.inventory_type);number(out,"flags",std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(item.flags)&flags_mask));
 key(out,"sale_info");out+="<map>";auto sale=static_cast<std::uint8_t>(item.sale_type);
 if(item.sale_price==0&&sale==0){number(out,"sale_price",0);number(out,"sale_type",0);}else{key(out,"sale_price");out+="<integer>"+std::to_string(item.sale_price)+"</integer>";key(out,"sale_type");out+="<integer>"+std::to_string(sale)+"</integer>";}out+="</map>";
 string(out,"name",item.name);string(out,"desc",framework::trim_utf8(item.description).empty()?std::string{}:item.description);number(out,"created_at",item.creation_date);out+="</map>";return out;
}
FetchInventory2Core::FetchInventory2Core(framework::Uuid agent,GetMultipleItems get):agent_(agent),get_(std::move(get)){if(!get_)throw std::invalid_argument("Null inventory item provider");}
network::HttpResponse FetchInventory2Core::handle(const network::Value& request_map,InventoryBadRequests* bad_requests)const{
 network::HttpResponse response;
 if(!bad_requests){response.status=404;return response;}
 const auto& map=request_map.view();if(map.kind!=network::Value::Kind::Map)throw std::invalid_argument("Inventory request requires map");
 const auto& requested=map.get("items").view();if(requested.kind!=network::Value::Kind::Array)throw std::invalid_argument("Inventory items require array");
 std::vector<framework::Uuid> ids(requested.array.size());std::size_t count=0;
 for(const auto& entry:requested.array){const auto& item=entry.view();if(item.kind!=network::Value::Kind::Map)throw std::invalid_argument("Inventory item requires map");auto id=network::osd_uuid(item.get("item_id"));if(!bad_requests->contains(id))ids[count++]=id;}
 std::optional<Items> items;try{items=get_(agent_,ids);}catch(...){}
 response.body="<llsd><map>";uuid(response.body,"agent_id",agent_);key(response.body,"items");
 if(!items||items->empty())response.body+="<array />";else{response.body+="<array>";for(const auto& item:*items)if(item)response.body+=inventory_item_llsd(*item,0xff);response.body+="</array>";}
 response.body+="</map></llsd>";return response;
}
FetchInventory2Route::FetchInventory2Route(std::string path,std::shared_ptr<FetchInventory2Core> core,
 std::shared_ptr<InventoryBadRequests> bad_requests,Deserialize deserialize):path_(std::move(path)),core_(std::move(core)),bad_requests_(std::move(bad_requests)),deserialize_(deserialize?std::move(deserialize):deserialize_xml_json){if(!core_)throw std::invalid_argument("Null inventory capability core");}
network::Value FetchInventory2Route::deserialize_xml_json(std::string_view body){
 // Match the Stream overload's sniff, not the case-insensitive byte[] overload.
 if(body.starts_with("<llsd>")||body.starts_with("<?xml")||body.starts_with("<? llsd/xml"))return network::parse_llsd_xml(body);
 if(body.starts_with("<? llsd/notatio")||body.starts_with("<? llsd/binary"))throw std::invalid_argument("Native inventory route requires injected LLSD notation/binary decoder");
 // JSON StreamReader consumes its UTF-8 BOM after the format sniff.
 if(body.starts_with("\xef\xbb\xbf"))body.remove_prefix(3);
 return network::parse_osd_json(body);
}
network::HttpResponse FetchInventory2Route::handle(const network::HttpRequest& request){
 ++requests_received;network::HttpResponse response;response.content_type="text/html;charset=UTF-8";
 if(request.method!="POST"){response.status=404;return response;}
 network::Value args;try{args=deserialize_(request.body);if(args.view().kind!=network::Value::Kind::Map)throw std::invalid_argument("Inventory HTTP body requires map");}catch(...){response.status=400;return response;}
 try{response=core_->handle(args,bad_requests_.get());response.content_type="text/html;charset=UTF-8";}catch(...){response.status=500;}
 ++requests_handled;return response;
}
}
