/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/services/library_service.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::services {
using framework::Uuid;
std::shared_ptr<InventoryFolderTree> InventoryFolderTree::create_child(const Uuid& requested_id,const std::string& requested_name,std::uint16_t requested_type){
    std::lock_guard lock(mutex_);if(contains_child(requested_id))return {};
    auto result=std::make_shared<InventoryFolderTree>();result->id=requested_id;result->name=requested_name;result->type=static_cast<std::int16_t>(requested_type);result->parent=this->id;result->owner=owner;children_.push_back(result);return result;
}
void InventoryFolderTree::add_child(const std::shared_ptr<InventoryFolderTree>& folder){
    if(!folder)throw std::invalid_argument("Missing child folder");
    const auto contains_self=[&](auto&& recurse,const std::shared_ptr<InventoryFolderTree>& node)->bool{if(node.get()==this)return true;for(const auto& child:node->children())if(recurse(recurse,child))return true;return false;};
    if(contains_self(contains_self,folder))throw std::invalid_argument("Inventory tree cycle");
    std::lock_guard lock(mutex_);folder->parent=id;auto found=std::find_if(children_.begin(),children_.end(),[&](const auto& child){return child->id==folder->id;});if(found==children_.end())children_.push_back(folder);else *found=folder;
}
bool InventoryFolderTree::contains_child(const Uuid& requested_id)const{return static_cast<bool>(child(requested_id));}
std::shared_ptr<InventoryFolderTree> InventoryFolderTree::child(const Uuid& requested_id)const{std::lock_guard lock(mutex_);for(const auto& folder:children_)if(folder->id==requested_id)return folder;return {};}
std::shared_ptr<InventoryFolderTree> InventoryFolderTree::remove_child(const Uuid& requested_id){std::lock_guard lock(mutex_);for(auto it=children_.begin();it!=children_.end();++it)if((*it)->id==requested_id){auto result=*it;children_.erase(it);return result;}return {};}
void InventoryFolderTree::add_item(const std::shared_ptr<InventoryItem>& item){if(!item)throw std::invalid_argument("Missing inventory item");std::lock_guard lock(mutex_);for(auto& value:items_)if(value->id==item->id){value=item;return;}items_.push_back(item);}
void InventoryFolderTree::purge(){std::lock_guard lock(mutex_);for(const auto& folder:children_)folder->purge();children_.clear();items_.clear();}
std::shared_ptr<InventoryItem> InventoryFolderTree::find_item(const Uuid& requested_id)const{std::lock_guard lock(mutex_);for(const auto& value:items_)if(value->id==requested_id)return value;for(const auto& folder:children_)if(auto result=folder->find_item(requested_id))return result;return {};}
std::shared_ptr<InventoryItem> InventoryFolderTree::find_asset(const Uuid& requested_id)const{std::lock_guard lock(mutex_);for(const auto& value:items_)if(value->asset_id==requested_id)return value;for(const auto& folder:children_)if(auto result=folder->find_asset(requested_id))return result;return {};}
bool InventoryFolderTree::delete_item(const Uuid& requested_id){std::lock_guard lock(mutex_);for(auto it=items_.begin();it!=items_.end();++it)if((*it)->id==requested_id){items_.erase(it);return true;}for(const auto& folder:children_)if(folder->delete_item(requested_id))return true;return false;}
std::shared_ptr<InventoryFolderTree> InventoryFolderTree::find_folder(const Uuid& requested_id){if(this->id==requested_id)return shared_from_this();std::lock_guard lock(mutex_);for(const auto& folder:children_)if(auto result=folder->find_folder(requested_id))return result;return {};}
std::shared_ptr<InventoryFolderTree> InventoryFolderTree::find_type(int requested_type)const{std::lock_guard lock(mutex_);for(const auto& folder:children_)if(folder->type==requested_type)return folder;return {};}
namespace {
bool whitespace(std::uint32_t value){return (value>=9&&value<=13)||value==0x20||value==0x85||value==0xa0||value==0x1680||(value>=0x2000&&value<=0x200a)||value==0x2028||value==0x2029||value==0x202f||value==0x205f||value==0x3000;}
std::string trim(const std::string& text){
    std::size_t first=std::string::npos,last=0;
    for(std::size_t i=0;i<text.size();){const auto start=i;auto value=static_cast<std::uint32_t>(static_cast<unsigned char>(text[i++]));if(value>=0xc0){int count=value<0xe0?1:value<0xf0?2:3;value&=(1u<<(6-count))-1;for(int n=0;n<count&&i<text.size();++n)value=(value<<6)|(static_cast<unsigned char>(text[i++])&63u);}if(!whitespace(value)){if(first==std::string::npos)first=start;last=i;}}
    return first==std::string::npos?std::string{}:text.substr(first,last-first);
}
}
std::shared_ptr<InventoryFolderTree> InventoryFolderTree::find_folder_path(std::string path){path=trim(path);if(path.empty()||path=="/")return shared_from_this();const auto split=path.find('/');const auto first=path.substr(0,split);std::lock_guard lock(mutex_);for(const auto& folder:children_)if(folder->name==first)return split==std::string::npos?folder:folder->find_folder_path(path.substr(split+1));return {};}
std::shared_ptr<InventoryItem> InventoryFolderTree::find_item_path(const std::string& path)const{const auto split=path.find('/');std::lock_guard lock(mutex_);if(split==std::string::npos){for(const auto& value:items_)if(value->name==path)return value;}else for(const auto& folder:children_)if(folder->name==path.substr(0,split))return folder->find_item_path(path.substr(split+1));return {};}
std::vector<std::shared_ptr<InventoryItem>> InventoryFolderTree::items()const{std::lock_guard lock(mutex_);return items_;}
std::vector<std::shared_ptr<InventoryFolderTree>> InventoryFolderTree::children()const{std::lock_guard lock(mutex_);return children_;}
int InventoryFolderTree::total_count()const{std::lock_guard lock(mutex_);std::uint32_t count=static_cast<std::uint32_t>(items_.size());for(const auto& folder:children_)count+=static_cast<std::uint32_t>(folder->total_count());return static_cast<std::int32_t>(count);}
}
