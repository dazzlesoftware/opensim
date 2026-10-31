// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/viewer_region.hpp"
#include "opensim/scene/default_animations.hpp"
#include "opensim/scene/legacy_inventory_requests.hpp"
#include "opensim/scene/task_inventory_file.hpp"
#include "opensim/scene/part_script_events.hpp"
#include "opensim/scene/inventory_new.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/task_inventory_item.hpp"
#include "opensim/services/agent_launch.hpp"
#include "opensim/services/presence_service.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/server/http_route_registry.hpp"
namespace opensim::scene {
// Host policies remain explicit while estate/parcel modules are being ported.
// Both login admission and viewer traffic use the same circuit/presence state.
struct RegionPolicies {
 std::function<bool(framework::Uuid,const framework::Vector3&,std::string&)> access;
 std::function<bool(const std::shared_ptr<framework::AgentCircuitData>&,std::string&)> authorize;
 std::function<bool(framework::Uuid,const framework::Vector3&,std::string&)> can_rez;
 std::function<bool(framework::Uuid,const SceneObject&)> can_edit;
 std::function<std::uint32_t(framework::Uuid,const SceneObject&)> client_flags;
 std::function<bool(framework::Uuid,const SceneObject&)> can_edit_inventory;
 std::function<bool(framework::Uuid)> viewer_ui_god;
 std::function<bool(framework::Uuid,const SceneObject&,const TaskInventoryFileItem&)> can_edit_script;
 std::function<void(std::shared_ptr<framework::AssetBase>)> store_asset;
 std::function<std::optional<std::string>(std::shared_ptr<framework::AssetBase>)> store_inventory_asset;
 std::function<bool(framework::Uuid,framework::Uuid,framework::Uuid)> can_edit_inventory_notecard;
 std::function<bool(framework::Uuid,framework::Uuid)> can_edit_user_inventory_script;
 std::function<std::shared_ptr<framework::AssetBase>(framework::Uuid)> load_asset;
 std::function<void(framework::Uuid,framework::Uuid,framework::Uuid,bool,framework::Uuid)> script_updated;

 // Source Constants.DefaultScriptText; hosts may supply the default asset's text.
 std::string default_script="default\n{\n    state_entry()\n    {\n        llSay(0, \"Script running\");\n    }\n}";
 std::function<framework::Uuid(framework::Uuid,const services::InventoryItem&,const std::string&)> create_script_asset;
 std::filesystem::path physics_library; // Empty selects no physics; explicit ubODE profile supports one avatar.
 std::shared_ptr<const DefaultAnimations> animations;
 std::optional<LegacyInventoryBackend> inventory;
 // Real avatar inventory storage remains alive until the region stops.
 services::InventoryService* inventory_service=nullptr;
 std::function<bool(framework::Uuid,framework::Uuid,framework::Uuid)> can_copy_task_inventory;
 std::function<bool(framework::Uuid)> inventory_is_god;
 std::function<void(const services::InventoryItem&,int)> inventory_upload_complete;
 bool bypass_inventory_permissions=false;
 std::function<bool(framework::Uuid,const SceneObject&,const services::InventoryItem&)> can_drop_inventory;
 std::function<bool(framework::Uuid,framework::Uuid)> inventory_group_member;
 std::function<std::uint64_t(framework::Uuid,framework::Uuid)> inventory_group_powers;
 std::function<void(framework::Uuid,framework::Uuid,framework::Uuid,std::shared_ptr<framework::TaskInventoryItem>)> task_inventory_transaction;
 std::function<void(framework::Uuid,const services::InventoryItem&)> fetch_remote_inventory_assets;
 std::function<void(framework::Uuid,framework::Uuid,std::shared_ptr<services::InventoryItem>)> user_inventory_transaction;
 std::function<std::optional<services::InventoryItem>(framework::Uuid)> library_inventory_item;
 std::function<std::optional<framework::Uuid>()> library_inventory_owner;
 std::function<bool(framework::Uuid,framework::Uuid)> can_copy_user_inventory;
 std::function<bool(std::int8_t,framework::Uuid)> can_create_user_inventory;
 std::function<bool(const InventoryNewRequest&)> inventory_creation_transaction;
 std::function<framework::Uuid(std::uint8_t)> environment_default_asset;
 std::function<InventoryNewLandmark(framework::Uuid)> inventory_landmark;
 bool propagate_permissions=true;
 std::function<bool(const framework::TaskInventoryItem&,const SceneObject&)> can_run_script;
 bool scripts_disabled=false;
 std::function<void(framework::Uuid)> remove_script_targets;
 std::function<std::optional<PartEmailEvents>()> script_email_events;
 std::function<void(framework::Uuid,std::uint64_t,std::uint32_t)> script_events_changed;

 std::function<void(framework::Uuid,framework::Uuid,framework::Uuid)> new_script;

 std::function<void(const ViewerAgent&)> entered,logged_out;
 std::function<void(const std::string&)> log;
};
class RegionRuntime final:public services::ISimulationLogin {
public:
 RegionRuntime(services::GridRegion region,services::IPresenceService& presence,RegionPolicies policies);
 ~RegionRuntime();
 network::UdpEndpoint start(std::shared_ptr<server::RegistryCapabilityListener> listener);
 void poll();void stop();
 bool can_edit_task_inventory(framework::Uuid object,framework::Uuid agent)const;
 bool can_edit_task_script(framework::Uuid item,framework::Uuid object,framework::Uuid agent)const;
 // HTTP callers queue the real transaction to the scene owner thread. Quiesce
 // pending script updates before stopping HTTP so no handler waits on a stopped
 // owner. The scene and asset dependencies must remain alive until it returns.
 std::vector<std::string> update_task_script(framework::Uuid agent,framework::Uuid item,framework::Uuid object,bool running,std::span<const std::uint8_t>);
 framework::Uuid update_inventory_asset(framework::Uuid agent,framework::Uuid item,framework::Uuid object,std::span<const std::uint8_t>);
 // Both queues are quiesced together before stopping HTTP.
 void quiesce_script_updates();
 std::int32_t active_script_count()const;

 services::GridRegion descriptor()const;
 World& world();ViewerRegion& viewers();
 services::SceneCapabilities* capabilities();
 bool query_access(services::GridRegion&,const framework::Uuid&,const std::optional<std::string>&,bool,const framework::Vector3&,std::vector<framework::Uuid>&,services::LoginTransferContext&,std::string&)override;
 bool create_agent(const services::GridRegion*,services::GridRegion&,const std::shared_ptr<framework::AgentCircuitData>&,std::uint32_t,services::LoginTransferContext&,std::string&)override;
private:
 struct State;std::shared_ptr<State> state_;
};
}
