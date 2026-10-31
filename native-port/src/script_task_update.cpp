// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/script_task_update.hpp"
#include <stdexcept>
namespace opensim::scene {
ScriptTaskUpdateController::ScriptTaskUpdateController(ScriptTaskUpdateBackend backend):backend_(std::move(backend)){
 if(!backend_.can_edit_script||!backend_.find_part||!backend_.store||!backend_.updated||!backend_.random_uuid)throw std::invalid_argument("Incomplete task script transaction backend");
}
ScriptTaskErrors ScriptTaskUpdateController::update(const ScriptTaskUpdateClient& client,framework::Uuid requested_item,framework::Uuid requested_part,bool running,framework::AssetBytes data)const{
 if(!backend_.can_edit_script(requested_item,requested_part,client.agent)){
  client.alert("Insufficient permissions to edit script",false);
  return std::vector<std::string>{};
 }
 auto part=backend_.find_part(requested_part);
 if(!part)return std::vector<std::string>{};
 auto item=part->get_item(part->local_id,requested_item);
 if(!item){if(backend_.log)backend_.log("[PRIM INVENTORY]: Task item missing for caps script update: "+requested_item.str());return std::vector<std::string>{};}
 item->set_running(running);
 auto asset=std::make_shared<framework::AssetBase>(backend_.random_uuid(),item->name,std::int8_t{10},client.agent.str());
 asset->description(item->description);asset->data=data?std::move(data):std::make_shared<std::vector<std::uint8_t>>(1);
 backend_.store(asset);
 part->remove_instance(item->id,false);
 item->set_asset(asset->full_id());
 part->update_inventory(*item);
 part->invalidate_permissions();
 part->send_properties(client);
 auto errors=part->compile(item->id,0,false,backend_.default_engine,1);
 backend_.updated(client.agent,requested_item,requested_part,running,item->asset());
 part->resume_scripts();
 return errors;
}
ScriptTaskErrors ScriptTaskUpdateController::update(framework::Uuid avatar,framework::Uuid item,framework::Uuid part,bool running,framework::AssetBytes data)const{
 if(!backend_.find_client)throw std::invalid_argument("Missing scene presence provider");
 auto client=backend_.find_client(avatar);
 if(client)return update(*client,item,part,running,std::move(data));
 if(backend_.log)backend_.log("[PRIM INVENTORY]: Avatar cannot be found to update its prim item asset: "+avatar.str());
 return std::vector<std::string>{};
}
}
