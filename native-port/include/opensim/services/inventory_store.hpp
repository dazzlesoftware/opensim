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
#pragma once
#include "opensim/framework/uuid.hpp"
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace opensim::services {
struct InventoryFolder {
    std::string name;
    int type=0,version=0;
    framework::Uuid id,owner,parent;
};
struct InventoryItem {
    framework::Uuid asset_id;
    int asset_type=0;
    std::string name,description;
    int next_permissions=0,current_permissions=0,inventory_type=0;
    std::string creator_id;
    int base_permissions=0,everyone_permissions=0,sale_price=0,sale_type=0,creation_date=0;
    framework::Uuid group_id;
    int group_owned=0,flags=0;
    framework::Uuid id,owner,parent;
    int group_permissions=0;
    bool operator==(const InventoryItem&)const=default;
};
class InventoryFolderStore {
public:
    using Filter=std::vector<std::pair<std::string,std::string>>;
    explicit InventoryFolderStore(const std::string& path,bool initialize=false);
    ~InventoryFolderStore();
    InventoryFolderStore(const InventoryFolderStore&)=delete;
    InventoryFolderStore& operator=(const InventoryFolderStore&)=delete;
    std::vector<InventoryFolder> folders(const Filter& filter);
    bool store(InventoryFolder& folder);
    bool move(const framework::Uuid& id,const framework::Uuid& parent);
    bool erase(const Filter& filter);
    std::vector<InventoryItem> items(const Filter& filter);
    bool store(InventoryItem& item);
    bool move_item(const framework::Uuid& id,const framework::Uuid& parent);
    bool erase_items(const Filter& filter);
    std::vector<InventoryItem> active_gestures(const framework::Uuid& owner);
    int asset_permissions(const framework::Uuid& owner,const framework::Uuid& asset);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Folder portion of XInventoryService. Item/library/provider paths remain separate.
class InventoryFolderService {
public:
    explicit InventoryFolderService(InventoryFolderStore& store):store_(store){}
    bool create_user_inventory(const framework::Uuid& owner);
    std::optional<InventoryFolder> root(const framework::Uuid& owner);
    std::optional<InventoryFolder> for_type(const framework::Uuid& owner,int type);
    std::optional<std::vector<InventoryFolder>> skeleton(const framework::Uuid& owner);
private:
    InventoryFolder create(const framework::Uuid& owner,const framework::Uuid& parent,int type,const std::string& name);
    InventoryFolderStore& store_;
};
struct InventoryCollection {
    framework::Uuid owner,folder;
    int version=0;
    std::vector<InventoryFolder> folders;
    std::vector<InventoryItem> items;
};
class InventoryService:public InventoryFolderService {
public:
    explicit InventoryService(InventoryFolderStore& store,bool allow_delete=true):InventoryFolderService(store),store_(store),allow_delete_(allow_delete){}
    std::optional<InventoryFolder> folder(const framework::Uuid& principal,const framework::Uuid& id);
    std::optional<InventoryItem> item(const framework::Uuid& principal,const framework::Uuid& id);
    InventoryCollection content(const framework::Uuid& principal,const framework::Uuid& id);
    std::vector<InventoryCollection> multiple_content(const framework::Uuid& principal,const std::vector<framework::Uuid>& ids);
    std::vector<std::optional<InventoryItem>> multiple_items(const framework::Uuid& principal,const std::vector<framework::Uuid>& ids);
    std::vector<InventoryItem> folder_items(const framework::Uuid& principal,const framework::Uuid& id);
    bool add_folder(InventoryFolder folder);
    bool update_folder(InventoryFolder folder);
    bool move_folder(const InventoryFolder& folder);
    bool add_item(const InventoryItem& item);
    bool update_item(InventoryItem& item);
    bool move_items(const framework::Uuid& principal,const std::vector<InventoryItem>& items);
    bool delete_items(const framework::Uuid& principal,const std::vector<framework::Uuid>& ids);
    bool delete_folders(const framework::Uuid& principal,const std::vector<framework::Uuid>& ids,bool only_if_trash=true);
    bool purge_folder(const framework::Uuid& id,bool only_if_trash=true);
    std::vector<InventoryItem> active_gestures(const framework::Uuid& principal);
    int asset_permissions(const framework::Uuid& principal,const framework::Uuid& asset);
    bool has_inventory_for_user(const framework::Uuid&)const{return false;}
private:
    bool parent_is_trash_or_lost(const framework::Uuid& id);
    InventoryFolderStore& store_;
    bool allow_delete_;
};
}
