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
#include "opensim/services/inventory_store.hpp"
#include <filesystem>
#include <algorithm>
#include <iostream>
using namespace opensim::services;
using opensim::framework::Uuid;
namespace {
int failures=0;
void check(bool value,const char* name){if(!value){++failures;std::cerr<<"FAIL: "<<name<<'\n';}}
void tests(const std::string& path){
    const auto owner=Uuid::random();Uuid root_id;InventoryItem saved;
    {
        InventoryFolderStore store(path,true);InventoryFolderService service(store);
        check(!service.root(owner)&&!service.skeleton(owner),"new account inventory is absent");
        check(service.create_user_inventory(owner),"inventory creation reports new root");auto root=service.root(owner);root_id=root->id;
        check(root->name=="My Inventory"&&root->type==8&&root->version==19,"root data and direct child version increments");
        check(service.skeleton(owner)->size()==21,"all default folders and calling-card descendants created");
        check(!service.create_user_inventory(owner)&&service.skeleton(owner)->size()==22,"repair returns false and preserves source duplicate Textures behavior");
        check(service.for_type(owner,0)&&service.for_type(owner,57)&&!service.for_type(owner,49),"folder type lookups preserve source default set");
        check(!service.skeleton(Uuid::random()),"inventory owner separation");
        auto folder=InventoryFolder{std::string(70,'x'),-1,1,Uuid::random(),owner,root_id};store.store(folder);
        check(folder.name.size()==64&&store.folders({{"folderID",folder.id.str()}}).front().name==folder.name,"folder store truncates and mutates long names");
        auto unicode=InventoryFolder{std::string(63,'a')+"\xf0\x9f\x98\x80",-1,1,Uuid::random(),owner,root_id};store.store(unicode);
        check(unicode.name==std::string(63,'a')+"\xef\xbf\xbd","UTF-16 substring boundary replaces lone surrogate");
        unicode.type=40000;unicode.version=65537;store.store(unicode);
        const auto skeleton=*service.skeleton(owner);const auto converted=std::find_if(skeleton.begin(),skeleton.end(),[&](const auto& value){return value.id==unicode.id;});
        check(converted!=skeleton.end()&&converted->type==-25536&&converted->version==1,"service narrows provider folder type and version like managed casts");
        auto destination=*service.for_type(owner,6);const auto before=store.folders({{"folderID",destination.id.str()}}).front().version;
        check(store.move(folder.id,destination.id),"folder move");check(store.folders({{"folderID",destination.id.str()}}).front().version==before+1,"move increments destination version");
        check(store.move(folder.id,destination.id)&&store.folders({{"folderID",destination.id.str()}}).front().version==before+3,"same-parent move increments source and destination");
        check(!store.move(Uuid::random(),root_id),"missing move returns false");
        check(store.erase({{"folderID",folder.id.str()}})&&store.folders({{"folderID",folder.id.str()}}).empty(),"folder delete persists");
        bool bad=false;try{store.folders({{"folderID OR 1=1","x"}});}catch(const std::invalid_argument&){bad=true;}check(bad,"query identifiers validated");
        auto version=[&](const Uuid& id){return store.folders({{"folderID",id.str()}}).front().version;};
        InventoryItem item;item.asset_id=Uuid::random();item.asset_type=21;item.name=std::string(70,'n');item.description=std::string(127,'d')+"\xf0\x9f\x98\x80";
        item.next_permissions=0x123;item.current_permissions=(-2147483647-1);item.inventory_type=21;item.creator_id="creator;https://example.invalid/";item.base_permissions=-1;item.everyone_permissions=7;
        item.sale_price=123;item.sale_type=2;item.creation_date=123456789;item.group_id=Uuid::random();item.group_owned=1;item.flags=1;item.id=Uuid::random();item.owner=owner;item.parent=destination.id;item.group_permissions=0x456;
        const auto original_version=version(destination.id);store.store(item);
        check(item.name.size()==64&&item.description==std::string(127,'d')+"\xef\xbf\xbd","item strings use source name/description UTF-16 limits");
        check(store.items({{"inventoryID",item.id.str()}})==std::vector<InventoryItem>{item},"all twenty inventory item fields roundtrip");
        check(version(destination.id)==original_version+1,"storing item increments parent version");
        auto second=item;second.id=Uuid::random();second.current_permissions=0x4000;second.flags=3;store.store(second);
        check(store.asset_permissions(owner,item.asset_id)==((-2147483647-1)|0x4000),"asset permissions combine all owned copies with signed masks");
        check(store.active_gestures(owner)==std::vector<InventoryItem>{item},"active gestures require exact flags one");
        check(store.asset_permissions(Uuid::random(),item.asset_id)==0&&store.active_gestures(Uuid::random()).empty(),"item owner isolation");
        const auto old_version=version(destination.id),new_version=version(root_id);item.parent=root_id;store.store(item);
        check(version(destination.id)==old_version&&version(root_id)==new_version+1,"REPLACE increments new parent only as in provider");
        check(store.move_item(item.id,destination.id)&&version(root_id)==new_version+2&&version(destination.id)==old_version+1,"item move increments both parents");
        check(store.move_item(item.id,destination.id)&&version(destination.id)==old_version+3,"same-parent item move increments twice");
        const auto delete_version=version(destination.id);check(store.erase_items({{"avatarID",owner.str()},{"assetID",item.asset_id.str()}})&&version(destination.id)==delete_version+1,"bulk item delete increments each distinct parent once");
        check(!store.erase_items({{"inventoryID",item.id.str()}})&&!store.move_item(Uuid::random(),root_id),"missing item delete and move return false");
        saved=item;saved.id=Uuid::random();saved.parent=root_id;saved.name="Persisted inventory";store.store(saved);
    }
    {InventoryFolderStore store(path);InventoryFolderService service(store);check(service.root(owner)->id==root_id&&service.skeleton(owner)->size()==23,"inventory survives database reopen");check(store.items({{"inventoryID",saved.id.str()}})==std::vector<InventoryItem>{saved},"inventory item survives database reopen");}
}
void service_tests(){
    InventoryFolderStore store(":memory:",true);InventoryService service(store),restricted(store,false);const auto owner=Uuid::random(),other=Uuid::random();service.create_user_inventory(owner);const auto root=*service.root(owner);
    auto plain=InventoryFolder{"Ordinary",-1,5,Uuid::random(),owner,root.id};check(service.add_folder(plain)&&!service.add_folder(plain),"service rejects duplicate folder IDs");
    auto duplicate=*service.for_type(owner,0);duplicate.id=Uuid::random();check(!service.add_folder(duplicate),"service rejects duplicate root system-folder types");
    auto system=*service.for_type(owner,6);const auto system_name=system.name;system.name="Changed";system.version=0;check(!service.update_folder(system),"system folder rejects stale version");system.version=5;check(service.update_folder(system)&&service.folder(owner,system.id)->name==system_name,"system update changes version only");
    plain.name="Renamed";plain.version=1;check(service.update_folder(plain)&&service.folder(owner,plain.id)->name=="Renamed"&&service.folder(owner,plain.id)->version==5,"ordinary folder updates preserve monotonic version");
    InventoryItem item;item.id=Uuid::random();item.owner=owner;item.parent=plain.id;item.asset_id=Uuid::random();item.asset_type=6;item.inventory_type=6;item.creator_id=owner.str()+";https://grid.invalid";item.name=std::string(70,'z');
    check(service.add_item(item)&&item.name.size()==70,"service stores a converted copy without truncating caller name");auto stored=*service.item(other,item.id);check(stored.name.size()==64&&stored.creator_id==item.creator_id+";Unknown User","service normalizes creator identification on conversion");
    item.owner=other;item.parent=root.id;item.asset_type=21;item.inventory_type=21;item.creator_id="different";item.name="Updated";const auto new_asset=Uuid::random();item.asset_id=new_asset;
    check(service.update_item(item)&&item.owner==owner&&item.parent==plain.id&&item.asset_type==6&&item.inventory_type==6&&item.creator_id==stored.creator_id,"update restores caller item invariants");
    check(service.item(owner,item.id)->asset_id==new_asset&&service.item(owner,item.id)->name=="Updated","update permits mutable asset ID and name");
    auto content=service.content(other,plain.id);check(content.owner==owner&&content.folder==plain.id&&content.items.size()==1,"folder content resolves owner by folder ID");
    const auto missing=Uuid::random();const auto multiple=service.multiple_items(owner,{item.id,missing,item.id});check(multiple.size()==3&&multiple[0]&&!multiple[1]&&multiple[2],"multiple item fetch preserves missing slots and input order");
    check(service.multiple_content(owner,{plain.id,missing}).size()==2&&service.content(other,missing).owner==other,"multiple folder results and missing-folder owner fallback");
    auto link=item;link.id=Uuid::random();link.asset_type=24;service.add_item(link);check(!restricted.update_item(link),"AllowDelete false blocks link update");
    check(restricted.delete_items(other,{item.id,link.id})&&service.item(owner,item.id)&&!service.item(owner,link.id),"AllowDelete false deletes links but retains ordinary items");
    auto trash=*service.for_type(owner,14);auto child=InventoryFolder{"Child",-1,1,Uuid::random(),owner,trash.id};service.add_folder(child);auto grandchild=InventoryFolder{"Grandchild",-1,1,Uuid::random(),owner,child.id};service.add_folder(grandchild);
    item.parent=grandchild.id;check(service.move_items(other,{item}),"service item move ignores principal as baseline");
    check(!restricted.purge_folder(trash.id)&&!service.purge_folder(plain.id),"purge respects AllowDelete and trash ancestry");
    check(service.purge_folder(trash.id)&&!service.folder(owner,child.id)&&!service.folder(owner,grandchild.id)&&!service.item(owner,item.id),"recursive trash purge removes descendants and items");
    check(service.folder(owner,trash.id).has_value(),"purge preserves target folder");
    auto lost=*service.for_type(owner,16);child.id=Uuid::random();child.parent=lost.id;service.add_folder(child);check(!service.purge_folder(child.id),"baseline Lost And Found ancestor lookup quirk retained");
    check(service.delete_folders(owner,{plain.id})&&service.folder(owner,plain.id),"ordinary folder deletion skipped outside trash");check(service.delete_folders(owner,{child.id},false)&&!service.folder(owner,child.id),"explicit unrestricted folder deletion");
    check(!service.has_inventory_for_user(owner),"unused HasInventoryForUser preserves constant false behavior");
}
}
int main(){const auto path=(std::filesystem::temp_directory_path()/("opensim-inventory-"+Uuid::random().str()+".db")).string();try{tests(path);service_tests();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';std::filesystem::remove(path);return 1;}std::filesystem::remove(path);return failures?1:0;}
