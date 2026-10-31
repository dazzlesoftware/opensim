// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_store.hpp"
#include "opensim/scene/task_inventory_file.hpp"
namespace opensim::scene {
struct InventoryScriptTask {TaskInventoryFileItem item;framework::Uuid parent,parent_part;bool running=true;};
struct InventoryScriptPart {framework::Uuid id,owner;};
struct InventoryScriptRezBackend {
 std::function<framework::Uuid()> random_uuid=[] {return framework::Uuid::random();};
 std::function<std::uint32_t()> unix_time;
 std::function<std::optional<services::InventoryItem>(framework::Uuid,framework::Uuid)> inventory_item;
 // Empty means missing library/service/root. Callback returns actual FindItem.
 std::function<std::optional<services::InventoryItem>(framework::Uuid)> library_item;
 std::function<std::optional<InventoryScriptPart>(std::uint32_t)> scene_part;
 std::function<bool(framework::Uuid,framework::Uuid)> can_edit_inventory;
 // group_part receives the original scene part's group context identifier.
 std::function<std::optional<InventoryScriptPart>(framework::Uuid,std::uint32_t)> group_part;
 std::function<bool()> propagate_permissions;
 std::function<std::uint32_t(const InventoryScriptPart&)> root_flags;
 // Real SceneObjectPartInventory.AddInventoryItem: source name uniqueness,
 // parent/group assignment, try-add, serial/events/aggregate perms/persistence.
 std::function<void(const InventoryScriptPart&,std::shared_ptr<InventoryScriptTask>,bool)> insert_task;
 std::function<void(const InventoryScriptPart&)> invalidate_deep_permissions;
 // Must load the real transferred AssetID and instantiate through real engine.
 std::function<void(const InventoryScriptPart&,framework::Uuid,int,bool,const std::string&,int)> compile;
 std::function<void(framework::Uuid,const InventoryScriptPart&,framework::Uuid)> new_script;
 std::function<void(const InventoryScriptPart&)> resume;
 std::function<void(const InventoryScriptPart&,framework::Uuid)> properties;
 std::function<std::optional<InventoryScriptPart>(framework::Uuid,const services::InventoryItem&)> rez_new;
 std::function<void(std::string)> log;
 std::string default_engine;
};
class InventoryScriptRezController {
public:
 explicit InventoryScriptRezController(InventoryScriptRezBackend);
 // Scene.RezScript wrapper; transaction is deliberately unused by source.
 void rez(framework::Uuid agent,const services::InventoryItem&,framework::Uuid transaction,std::uint32_t local)const;
 std::optional<InventoryScriptPart> rez_existing(framework::Uuid agent,framework::Uuid item,std::uint32_t local)const;
 // Complete source SceneObjectGroup.AddInventoryItem transfer projection and
 // ordering. The scene caller deliberately ignores its false return.
 bool add_to_group(framework::Uuid agent,const InventoryScriptPart& group_context,std::uint32_t local,const services::InventoryItem&,framework::Uuid copy_id,bool with_modify_rights=true)const;
private:InventoryScriptRezBackend backend_;
};
}
