// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/capabilities_module.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
namespace opensim::services {
namespace {std::string padded(const std::string& text,std::size_t width){auto length=framework::utf16_replacing(text).size();return text+std::string(length<width?width-length:0,' ');}std::string row(const std::string& key,const std::optional<std::string>& value){return "   "+padded(key,38)+" "+padded(value.value_or(""),60)+"\n";}CapabilitiesModule::Seeds& require_seeds(const std::shared_ptr<CapabilitiesModule::Seeds>& value){if(!value)throw std::invalid_argument("Null child seeds");return *value;}}
std::shared_ptr<CapabilityModuleScene> CapabilitiesModule::require_scene()const{std::lock_guard lock(scene_mutex_);if(!scene_)throw std::invalid_argument("Null capability scene");return scene_;}
std::shared_ptr<CapabilityModuleScene> CapabilitiesModule::console_backend()const{std::lock_guard lock(scene_mutex_);auto backend=console_?console_:scene_;if(!backend)throw std::invalid_argument("Null capability console");return backend;}
bool CapabilitiesModule::console_selected()const{std::shared_ptr<CapabilityModuleScene> scene;{std::lock_guard lock(scene_mutex_);scene=scene_;}if(scene)return scene->selected_for_console();return !console_backend()->console_has_selected_scene();}
void CapabilitiesModule::add_region(std::shared_ptr<CapabilityModuleScene> scene){{std::lock_guard lock(scene_mutex_);scene_=std::move(scene);if(scene_)console_=scene_;}require_scene()->register_module(this);
    require_scene()->add_command({"show caps list","show caps list","Shows list of registered capabilities for users.",std::nullopt,[this](const auto& args){show_caps_list(args);}});
    require_scene()->add_command({"show caps stats by user","show caps stats by user [<first-name> <last-name>]","Shows statistics on capabilities use by user.","If a user name is given, then prints a detailed breakdown of caps use ordered by number of requests received.",[this](const auto& args){show_caps_stats_by_user(args);}});
    require_scene()->add_command({"show caps stats by cap","show caps stats by cap [<cap-name>]","Shows statistics on capabilities use by capability.","If a capability name is given, then prints a detailed breakdown of use by each user.",[this](const auto& args){show_caps_stats_by_cap(args);}});
}
void CapabilitiesModule::remove_region(const std::shared_ptr<CapabilityModuleScene>&){require_scene()->unregister_module(this);}
void CapabilitiesModule::set_agent_caps_seeds(const std::shared_ptr<framework::AgentCircuitData>& input){auto agent=input;if(!agent)throw std::invalid_argument("Null agent circuit");{std::lock_guard lock(paths_mutex_);paths_[agent->agent_id.str()]=agent->caps_path;}{std::lock_guard lock(children_mutex_);children_[agent->agent_id.str()]=agent->children_cap_seeds?agent->children_cap_seeds:std::make_shared<Seeds>();}}
std::optional<std::string> CapabilitiesModule::get_caps_path(const framework::Uuid& agent){std::lock_guard lock(paths_mutex_);auto found=paths_.find(agent.str());return found==paths_.end()?std::nullopt:found->second;}
void CapabilitiesModule::create_caps(const framework::Uuid& agent,std::uint32_t circuit){auto path=get_caps_path(agent);std::shared_ptr<CapabilityObject> caps;
    {std::lock_guard lock(objects_mutex_);auto old=objects_.find(circuit);if(old!=objects_.end()){auto previous=old->second;if(path==previous->caps_path())return;previous->deregister_handlers();objects_.erase(circuit);}
        caps=require_scene()->make_caps(path,agent);if(!caps)throw std::invalid_argument("Null capability object");(void)caps->caps_path();objects_[circuit]=caps;
    }require_scene()->register_caps(agent,caps);
}
void CapabilitiesModule::remove_caps(const framework::Uuid& agent,std::uint32_t circuit){(void)require_scene()->region_name();{std::lock_guard lock(children_mutex_);children_.erase(agent.str());}
    std::lock_guard lock(objects_mutex_);auto found=objects_.find(circuit);if(found!=objects_.end()){auto caps=found->second;require_scene()->deregister_caps(agent,caps);objects_.erase(circuit);caps->dispose();}
    else for(const auto& entry:objects_){const auto code=entry.first;auto caps=entry.second;if(agent==caps->agent_id()){require_scene()->deregister_caps(agent,caps);objects_.erase(code);caps->dispose();return;}}
}
std::shared_ptr<CapabilityObject> CapabilitiesModule::get_caps_for_user(std::uint32_t circuit){std::lock_guard lock(objects_mutex_);auto found=objects_.find(circuit);return found==objects_.end()?nullptr:found->second;}
void CapabilitiesModule::activate_caps(std::uint32_t circuit){std::lock_guard lock(objects_mutex_);auto found=objects_.find(circuit);if(found!=objects_.end()){auto caps=found->second;caps->activate();}}
std::shared_ptr<CapabilitiesModule::Seeds> CapabilitiesModule::get_children_seeds(const framework::Uuid& agent){std::lock_guard lock(children_mutex_);auto found=children_.find(agent.str());return found==children_.end()?std::make_shared<Seeds>():found->second;}
void CapabilitiesModule::set_children_seed(const framework::Uuid& agent,std::shared_ptr<Seeds> seeds){std::lock_guard lock(children_mutex_);children_[agent.str()]=std::move(seeds);}
void CapabilitiesModule::drop_child_seed(const framework::Uuid& agent,std::uint64_t handle){std::lock_guard lock(children_mutex_);auto found=children_.find(agent.str());if(found!=children_.end())require_seeds(found->second).erase(handle);}
std::optional<std::string> CapabilitiesModule::get_child_seed(const framework::Uuid& agent,std::uint64_t handle){std::lock_guard lock(children_mutex_);auto found=children_.find(agent.str());if(found!=children_.end()){auto& seeds=require_seeds(found->second);auto value=seeds.find(handle);if(value!=seeds.end())return value->second;}return {};}
void CapabilitiesModule::dump_children_seeds(const framework::Uuid& agent){require_scene()->info("================ ChildrenSeed "+require_scene()->region_name().value_or("")+" ================");std::lock_guard lock(children_mutex_);auto seeds=children_.at(agent.str());for(const auto& value:require_seeds(seeds)){auto handle=value.first;auto seed=value.second;auto x=static_cast<std::uint32_t>((handle>>40)&0xffffff),y=static_cast<std::uint32_t>((handle>>8)&0xffffff);require_scene()->info(" >> "+std::to_string(x)+", "+std::to_string(y)+": "+seed.value_or(""));}}
void CapabilitiesModule::show_caps_list(const std::vector<std::string>&){if(!console_selected())return;std::string output="Region "+require_scene()->region_name().value_or("")+":\n";{std::lock_guard lock(objects_mutex_);
    for(const auto& entry:objects_){auto code=entry.first;auto caps=entry.second;auto agent=caps->agent_id();auto name=require_scene()->presence_name(agent).value_or("");output+="** Circuit "+std::to_string(code)+"; "+caps->agent_id().str()+" "+name+":\n";output+="**    Base URL "+caps->base_url().value_or("")+"\n";for(const auto& pair:caps->local_paths())output+=row(pair.first,pair.second);for(const auto& pair:caps->poll_paths())output+=row(pair.first,pair.second);for(const auto& pair:caps->external_paths())output+=row(pair.first,pair.second);
    }}console_backend()->console_output(output);
}
void CapabilitiesModule::show_caps_stats_by_cap(const std::vector<std::string>& args){if(!console_selected())return;if(args.size()!=5&&args.size()!=6){console_backend()->console_output("Usage: show caps stats by cap [<cap-name>]");return;}console_backend()->console_output("Region "+require_scene()->region_name().value_or("")+":\n");}
}
