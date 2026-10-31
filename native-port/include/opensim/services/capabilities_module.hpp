// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/scene_admission.hpp"
#include <functional>
#include "opensim/server/config.hpp"
namespace opensim::services {
class CapabilitiesModule;
using CapabilityPaths=framework::OrderedDictionary<std::string,std::optional<std::string>>;
class CapabilityObject {
public:
    virtual ~CapabilityObject()=default;
    virtual std::optional<std::string> caps_path()const=0;
    virtual framework::Uuid agent_id()const=0;
    virtual void deregister_handlers()=0;
    virtual void dispose()=0;
    virtual void activate()=0;
    virtual std::optional<std::string> base_url()const=0;
    virtual CapabilityPaths local_paths()const=0;
    virtual CapabilityPaths poll_paths()const=0;
    virtual CapabilityPaths external_paths()const=0;
};
struct CapabilityCommand {
    std::string name,help,summary;
    std::optional<std::string> details;
    std::function<void(const std::vector<std::string>&)> handler;
    std::string group="Comms";
    bool shared=false;
};
class CapabilityModuleScene {
public:
    virtual ~CapabilityModuleScene()=default;
    virtual std::optional<std::string> region_name()const=0;
    virtual void register_module(CapabilitiesModule* module)=0;
    virtual void unregister_module(CapabilitiesModule* module)=0;
    virtual std::shared_ptr<CapabilityObject> make_caps(const std::optional<std::string>& path,const framework::Uuid& agent)=0;
    virtual void register_caps(const framework::Uuid& agent,const std::shared_ptr<CapabilityObject>& caps)=0;
    virtual void deregister_caps(const framework::Uuid& agent,const std::shared_ptr<CapabilityObject>& caps)=0;
    virtual void add_command(CapabilityCommand command)=0;
    virtual bool selected_for_console()const=0;
    virtual bool console_has_selected_scene()const=0;
    virtual std::optional<std::string> presence_name(const framework::Uuid& agent)=0;
    virtual void console_output(const std::string& text)=0;
    virtual void info(const std::string& text)=0;
};
class CapabilitiesModule:public AdmissionCaps {
public:
    using Seeds=framework::AgentCircuitData::ChildrenSeeds;
    static constexpr std::string_view name="Capabilities Module";
    void initialise(const server::IniConfig*){}
    void add_region(std::shared_ptr<CapabilityModuleScene> scene);
    void set_console_backend(std::shared_ptr<CapabilityModuleScene> console){std::lock_guard lock(scene_mutex_);console_=std::move(console);}
    void remove_region(const std::shared_ptr<CapabilityModuleScene>&);
    void region_loaded(const std::shared_ptr<CapabilityModuleScene>&){}
    void post_initialise(){}
    void close(){}
    std::nullptr_t replaceable_interface()const{return nullptr;}
    void set_agent_caps_seeds(const std::shared_ptr<framework::AgentCircuitData>& agent)override;
    void create_caps(const framework::Uuid& agent,std::uint32_t circuit)override;
    void activate_caps(std::uint32_t circuit)override;
    void remove_caps(const framework::Uuid& agent,std::uint32_t circuit);
    std::shared_ptr<CapabilityObject> get_caps_for_user(std::uint32_t circuit);
    std::optional<std::string> get_caps_path(const framework::Uuid& agent);
    std::shared_ptr<Seeds> get_children_seeds(const framework::Uuid& agent);
    void set_children_seed(const framework::Uuid& agent,std::shared_ptr<Seeds> seeds);
    void drop_child_seed(const framework::Uuid& agent,std::uint64_t handle);
    std::optional<std::string> get_child_seed(const framework::Uuid& agent,std::uint64_t handle);
    void dump_children_seeds(const framework::Uuid& agent);
    void show_caps_list(const std::vector<std::string>&);
    void show_caps_stats_by_cap(const std::vector<std::string>& arguments);
    void show_caps_stats_by_user(const std::vector<std::string>&){}
    std::size_t caps_count(){std::lock_guard lock(objects_mutex_);return objects_.size();}
private:
    std::shared_ptr<CapabilityModuleScene> require_scene()const;
    std::shared_ptr<CapabilityModuleScene> console_backend()const;
    bool console_selected()const;
    mutable std::recursive_mutex scene_mutex_;
    std::recursive_mutex paths_mutex_,children_mutex_,objects_mutex_;
    std::shared_ptr<CapabilityModuleScene> scene_,console_;
    framework::OrderedDictionary<std::string,std::optional<std::string>> paths_;
    framework::OrderedDictionary<std::string,std::shared_ptr<Seeds>> children_;
    framework::OrderedDictionary<std::uint32_t,std::shared_ptr<CapabilityObject>> objects_;
};
}
