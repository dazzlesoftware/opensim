// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/library_inventory_caps.hpp"
#include "opensim/network/osd_value.hpp"
#include <stdexcept>
namespace opensim::services {
FetchLibrary2Core::FetchLibrary2Core(framework::Uuid agent,std::optional<LibraryItemBackend> library):agent_(agent),library_(std::move(library)){if(library_&&(!library_->root_owner||!library_->get_items))throw std::invalid_argument("Library inventory service requires actual root and item provider");}
network::HttpResponse FetchLibrary2Core::handle(const network::Value& input,InventoryBadRequests* bad)const{
 network::HttpResponse response;response.content_type="text/html;charset=UTF-8";
 if(!bad||!library_||agent_.is_zero()){response.status=404;return response;}
 const auto& map=input.view();if(map.kind!=network::Value::Kind::Map)throw std::invalid_argument("Library request requires map");const auto& items=map.get("items").view();if(items.kind!=network::Value::Kind::Array)throw std::invalid_argument("Library items require array");std::vector<framework::Uuid> ids(items.array.size());std::size_t i=0;
 for(const auto& entry:items.array){const auto& item=entry.view();if(item.kind!=network::Value::Kind::Map)throw std::invalid_argument("Library item requires map");auto id=network::osd_uuid(item.get("item_id"));if(!bad->contains(id))ids[i++]=id;}
 auto result=library_->get_items(ids);
 response.body="<llsd><map><key>agent_id</key><uuid>"+agent_.str()+"</uuid><key>items</key>";
 if(!result||result->empty())response.body+="<array />";else{response.body+="<array>";for(const auto& item:*result)if(item)response.body+=inventory_item_llsd(*item);response.body+="</array>";}
 response.body+="</map></llsd>";return response;
}
FetchLibrary2Route::FetchLibrary2Route(std::string path,std::shared_ptr<FetchLibrary2Core> core,std::shared_ptr<InventoryBadRequests> bad,Deserialize deserialize):path_(std::move(path)),core_(std::move(core)),bad_(std::move(bad)),deserialize_(deserialize?std::move(deserialize):FetchInventory2Route::deserialize_xml_json){if(!core_)throw std::invalid_argument("Null library capability core");}
network::HttpResponse FetchLibrary2Route::handle(const network::HttpRequest& request){
 ++requests_received;network::HttpResponse response;response.content_type="text/html;charset=UTF-8";if(request.method!="POST"){response.status=404;return response;}
 network::Value args;try{args=deserialize_(request.body);if(args.view().kind!=network::Value::Kind::Map)throw std::invalid_argument("Library HTTP body requires map");}catch(...){response.status=400;return response;}
 try{response=core_->handle(args,bad_.get());response.content_type="text/html;charset=UTF-8";}catch(...){response.status=500;}++requests_handled;return response;
}
}
