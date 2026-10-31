// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/agent_launch.hpp"
#include "opensim/services/simulation_agent_updates.hpp"
#include "opensim/services/simulation_agent_lifecycle.hpp"
#include "opensim/server/config.hpp"
#include "opensim/services/simulation_objects.hpp"
#include <atomic>
#include <functional>
#include <mutex>
namespace opensim::services {
class LocalSimulationConnector;
class LocalSimulationScene;
class SimulationAgentArrival {
public:
    virtual ~SimulationAgentArrival()=default;
    virtual void agent_arrived_at_destination(const framework::Uuid& agent)=0;
};
class LocalSimulationScene:public framework::AgentTransferScene {
public:
    virtual framework::Uuid region_id()const=0;
    virtual std::optional<std::string> region_name()const=0;
    virtual std::uint32_t region_size_x()const=0;
    virtual std::uint32_t region_size_y()const=0;
    virtual std::uint64_t region_handle()const=0;
    virtual void register_simulation_service(LocalSimulationConnector* service)=0;
    virtual void unregister_simulation_service(LocalSimulationConnector* service)=0;
    virtual bool new_user_connection(const std::shared_ptr<framework::AgentCircuitData>& circuit,std::uint32_t flags,const GridRegion* source,std::string& reason)=0;
    virtual bool incoming_update_child_agent(const std::shared_ptr<framework::AgentData>& data)=0;
    virtual bool incoming_update_child_agent(const std::shared_ptr<framework::AgentPosition>& data)=0;
    virtual bool query_access(const framework::Uuid& agent,const std::optional<std::string>& home,bool teleport,const framework::Vector3& position,std::vector<framework::Uuid>* features,std::string& reason)=0;
    virtual std::shared_ptr<SimulationAgentArrival> entity_transfer_module()=0;
    virtual bool close_agent(const framework::Uuid& agent,bool force,const std::optional<std::string>& auth)=0;
    virtual bool incoming_create_object(const framework::Vector3& position,const std::shared_ptr<SimulationObject>& object)=0;
};
class LocalSimulationConnector:public ISimulationLogin,public ISimulationAgentUpdates,public ISimulationAgentLifecycle,public ISimulationObjects {
public:
    static constexpr std::string_view name="LocalSimulationConnectorModule";
    void initialise(const server::IniConfig* config);
    void initialise(const server::IniConfig& config){initialise(&config);}
    void initialise_service(const server::IniConfig*){}
    void post_initialise(){}
    void region_loaded(const std::shared_ptr<LocalSimulationScene>&){}
    void close(){}
    std::nullptr_t replaceable_interface()const{return nullptr;}
    bool enabled()const{return enabled_.load();}
    std::size_t scene_count()const{std::lock_guard lock(mutex_);return scenes_.size();}
    void init(const std::shared_ptr<LocalSimulationScene>& scene);
    void remove_scene(const std::shared_ptr<LocalSimulationScene>& scene);
    void add_region(const std::shared_ptr<LocalSimulationScene>& scene);
    void remove_region(const std::shared_ptr<LocalSimulationScene>& scene);
    std::shared_ptr<framework::AgentTransferScene> get_scene(const framework::Uuid& region)override;
    LocalSimulationConnector* get_inner_service(){return this;}
    bool is_local_region(const framework::Uuid& region)const;
    bool is_local_region(std::uint64_t handle);
    bool create_agent(const GridRegion* source,GridRegion& destination,const std::shared_ptr<framework::AgentCircuitData>& circuit,std::uint32_t flags,LoginTransferContext& context,std::string& reason)override;
    bool create_agent(const GridRegion* source,GridRegion* destination,const std::shared_ptr<framework::AgentCircuitData>& circuit,std::uint32_t flags,LoginTransferContext* context,std::string& reason);
    bool update_agent(GridRegion& destination,const std::shared_ptr<framework::AgentData>& data,LoginTransferContext& context)override;
    bool update_agent(GridRegion* destination,const std::shared_ptr<framework::AgentData>& data,LoginTransferContext* context);
    bool update_agent(GridRegion& destination,const std::shared_ptr<framework::AgentPosition>& data)override;
    bool update_agent(GridRegion* destination,const std::shared_ptr<framework::AgentPosition>& data);
    bool query_access(GridRegion& destination,const framework::Uuid& agent,const std::optional<std::string>& home,bool teleport,const framework::Vector3& position,std::vector<framework::Uuid>& features,LoginTransferContext& context,std::string& reason)override;
    bool query_access(GridRegion* destination,const framework::Uuid& agent,const std::optional<std::string>& home,bool teleport,const framework::Vector3& position,std::vector<framework::Uuid>* features,LoginTransferContext* context,std::string& reason);
    bool release_agent(const framework::Uuid& origin,const framework::Uuid& agent,const std::string& uri)override;
    bool close_agent(GridRegion& destination,const framework::Uuid& agent,const std::optional<std::string>& auth)override;
    bool close_agent(GridRegion* destination,const framework::Uuid& agent,const std::optional<std::string>& auth);
    bool create_object(GridRegion* destination,const framework::Vector3& position,std::shared_ptr<SimulationObject> object,bool local_call);
private:
    using Scene=std::shared_ptr<LocalSimulationScene>;
    Scene find(const framework::Uuid& region)const;
    bool visit(const std::function<bool(const Scene&)>& action);
    mutable std::recursive_mutex mutex_;
    framework::OrderedDictionary<std::string,Scene> scenes_;
    std::atomic<bool> enabled_=false;
};
}
