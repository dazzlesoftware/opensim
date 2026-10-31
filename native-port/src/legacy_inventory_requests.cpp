// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/legacy_inventory_requests.hpp"
#include <set>
namespace opensim::scene {
LegacyInventoryRequests::LegacyInventoryRequests(LegacyInventoryBackend backend):backend_(std::move(backend)){if(!backend_.item||!backend_.content||!backend_.folder)throw std::invalid_argument("Null legacy inventory provider");}
bool LegacyInventoryRequests::process_items(framework::Uuid agent,std::span<const LegacyItemRequest> requests,const std::function<bool()>& active,const InventoryPacketSink& sink)const{
 if(!active)throw std::invalid_argument("Null legacy client activity callback");if(!active())return false;
 std::vector<std::optional<services::InventoryItem>> items;for(const auto& request:requests){auto item=backend_.item(request.owner,request.item);if(item)items.push_back(std::move(item));}inventory_item_details(agent,items,sink);return true;
}
void LegacyInventoryRequests::send_folder(framework::Uuid agent,const LegacyFolderRequest& request,const services::InventoryCollection& content,int version,int descendants,const InventoryPacketSink& sink)const{
 std::vector<std::optional<services::InventoryItem>> items;std::vector<std::optional<services::InventoryFolder>> folders;for(const auto& item:content.items)items.push_back(item);for(const auto& folder:content.folders)folders.push_back(folder);inventory_folder_details(agent,content.owner,request.folder,items,folders,version,descendants,request.fetch_folders,request.fetch_items,sink);
}
LegacyFolderDisposition LegacyInventoryRequests::receive_folder(framework::Uuid agent,const LegacyFolderRequest& request,const InventoryPacketSink& sink)const{
 if(request.folder.is_zero())return LegacyFolderDisposition::Ignored;
 if(backend_.library_folder){auto library=backend_.library_folder(request.folder);if(library){send_folder(agent,request,*library,library->version,static_cast<int>(library->folders.size()+library->items.size()),sink);return LegacyFolderDisposition::Answered;}}
 return LegacyFolderDisposition::Queued;
}
bool LegacyInventoryRequests::process_folder(framework::Uuid agent,const LegacyFolderRequest& request,const std::function<bool()>& active,const InventoryPacketSink& sink)const{
 if(!active)throw std::invalid_argument("Null legacy client activity callback");if(!active())return false;
 if(backend_.library_folder){auto library=backend_.library_folder(request.folder);if(library){send_folder(agent,request,*library,library->version,static_cast<int>(library->folders.size()+library->items.size()),sink);return true;}}
 auto contents=backend_.content(agent,request.folder);auto containing=backend_.folder(agent,request.folder);if(!containing)return true;
 int descendants=static_cast<int>(contents.folders.size()+contents.items.size());
 if(request.fetch_items&&!contents.items.empty()){std::set<framework::Uuid> seen;std::vector<services::InventoryItem> links;for(const auto& item:contents.items){if(item.asset_type!=24||seen.contains(item.asset_id))continue;auto target=backend_.item(item.owner,item.asset_id);if(target&&target->asset_type!=24&&target->asset_type!=25){seen.insert(target->id);links.push_back(std::move(*target));}}if(!links.empty()){links.insert(links.end(),contents.items.begin(),contents.items.end());contents.items=std::move(links);}}
 contents.owner=agent;send_folder(agent,request,contents,containing->version,descendants,sink);return true;
}
}
