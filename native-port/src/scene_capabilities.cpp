// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/server/seed_caps.hpp"
namespace opensim::services {
namespace {
template<class T>T& required(const std::shared_ptr<T>& value){if(!value)throw std::invalid_argument("Null scene capability dependency");return *value;}
class SeedBinding:public server::SeedCapsBackend {
public:
    SeedBinding(std::shared_ptr<Caps> caps,std::shared_ptr<SceneCapabilityBackend> backend):caps_(std::move(caps)),backend_(std::move(backend)){}
    Caps& caps()override{return *caps_;}
    std::optional<std::string> region_name()const override{return backend_->region_name();}
    bool check_client(const framework::Uuid& agent,const network::HttpRequest& request)override{return backend_->check_client(agent,request);}
    std::uint32_t region_size_x()const override{return backend_->region_size_x();}
    std::uint32_t region_size_y()const override{return backend_->region_size_y();}
    void debug(const std::string& text)override{backend_->debug(text);}
    void warn(const std::string& text)override{backend_->warn(text);}
private:
    std::shared_ptr<Caps> caps_;std::shared_ptr<SceneCapabilityBackend> backend_;
};
class SeedRoute:public server::CapabilitySimpleHandler {
public:
    SeedRoute(std::shared_ptr<Caps> caps,std::shared_ptr<SceneCapabilityBackend> backend):caps_(std::move(caps)),backend_(std::move(backend)),path_("/CAPS/"+caps_->caps_path().value_or("")+"0000"){}
    std::optional<std::string> path()const override{return path_;}
    network::HttpResponse handle(const network::HttpRequest& request)override{SeedBinding binding(caps_,backend_);server::SeedCapsHandler handler(binding);return handler.handle(request);}
private:
    // Source delegates keep BunchOfCaps and its Caps alive while selected or in
    // flight. Explicit owner shutdown removes registrations and breaks cycles.
    std::shared_ptr<Caps> caps_;std::shared_ptr<SceneCapabilityBackend> backend_;std::string path_;
};
}
struct SceneCapabilities::State {
    std::shared_ptr<SceneCapabilityBackend> backend;
    SceneCapabilityEvents events;
    std::recursive_mutex mutex;
    std::vector<std::weak_ptr<Caps>> objects;
    CapabilitiesModule* caps_module=nullptr;EventQueueModule* queue_module=nullptr;
    explicit State(std::shared_ptr<SceneCapabilityBackend> b):backend(std::move(b)),events([value=backend](const auto& prefix,const auto& e){value->event_error(prefix,e);}){required(backend);}
};
class SceneCapabilities::CapsScene:public CapabilityModuleScene {
    std::shared_ptr<State> state_;
public:
    explicit CapsScene(std::shared_ptr<State> state):state_(std::move(state)){}
    std::optional<std::string> region_name()const override{return state_->backend->region_name();}
    void register_module(CapabilitiesModule* module)override{std::lock_guard lock(state_->mutex);state_->caps_module=module;}
    void unregister_module(CapabilitiesModule* module)override{std::lock_guard lock(state_->mutex);if(state_->caps_module==module)state_->caps_module=nullptr;}
    std::shared_ptr<CapabilityObject> make_caps(const std::optional<std::string>& path,const framework::Uuid& agent)override{auto backend=state_->backend;auto main=backend->main_server();auto caps=std::make_shared<Caps>(main,backend->external_host(),main?main->port():0,path,agent,backend->region_name(),[backend]{return backend->main_server();},nullptr,[backend](const auto& text){backend->error(text);});std::lock_guard lock(state_->mutex);std::erase_if(state_->objects,[](const auto& object){return object.expired();});state_->objects.push_back(caps);return caps;}
    void register_caps(const framework::Uuid& agent,const std::shared_ptr<CapabilityObject>& object)override{auto caps=std::dynamic_pointer_cast<Caps>(object);if(object&&!caps)throw std::invalid_argument("Scene capability type");state_->events.trigger_register(agent,std::move(caps));}
    void deregister_caps(const framework::Uuid& agent,const std::shared_ptr<CapabilityObject>& object)override{auto caps=std::dynamic_pointer_cast<Caps>(object);if(object&&!caps)throw std::invalid_argument("Scene capability type");state_->events.trigger_deregister(agent,std::move(caps));}
    void add_command(CapabilityCommand command)override{state_->backend->add_command(std::move(command));}
    bool selected_for_console()const override{return state_->backend->selected_for_console();}
    bool console_has_selected_scene()const override{return state_->backend->console_has_selected_scene();}
    std::optional<std::string> presence_name(const framework::Uuid& agent)override{return state_->backend->presence_name(agent);}
    void console_output(const std::string& text)override{state_->backend->console(text);}
    void info(const std::string& text)override{state_->backend->info(text);}
};
class SceneCapabilities::QueueScene:public EventQueueScene {
    std::shared_ptr<State> state_;
public:
    explicit QueueScene(std::shared_ptr<State> state):state_(std::move(state)){}
    std::string name()const override{return state_->backend->scene_name();}
    std::string region_name()const override{return state_->backend->region_name().value_or("");}
    void register_module(EventQueueModule* module)override{std::lock_guard lock(state_->mutex);state_->queue_module=module;}
    void unregister_module(EventQueueModule* module)override{std::lock_guard lock(state_->mutex);if(state_->queue_module==module)state_->queue_module=nullptr;}
    void subscribe_closed(EventQueueModule* owner,Closed callback)override{state_->events.on_client_closed.add({{owner,"EventQueue.ClientClosed"},std::move(callback)});}
    void unsubscribe_closed(EventQueueModule* owner)override{state_->events.on_client_closed.remove(SceneDelegateIdentity{owner,"EventQueue.ClientClosed"});}
    void subscribe_register(EventQueueModule* owner,Register callback)override{state_->events.on_register_caps.add({{owner,"EventQueue.OnRegisterCaps"},[callback=std::move(callback)](auto agent,auto caps){callback(agent,event_queue_caps(std::move(caps)));}});}
    void unsubscribe_register(EventQueueModule* owner)override{state_->events.on_register_caps.remove(SceneDelegateIdentity{owner,"EventQueue.OnRegisterCaps"});}
};
SceneCapabilities::SceneCapabilities(std::shared_ptr<SceneCapabilityBackend> backend):state_(std::make_shared<State>(std::move(backend))),caps_scene_(std::make_shared<CapsScene>(state_)),queue_scene_(std::make_shared<QueueScene>(state_)),capabilities_(std::make_unique<CapabilitiesModule>()),queue_(std::make_shared<EventQueueModule>(std::static_pointer_cast<EventQueueBackend>(state_->backend))){
    capabilities_->add_region(caps_scene_);
    state_->events.on_register_caps.add({{this,"Seed.Register"},[backend=state_->backend](auto,auto caps){required(caps).register_simple("SEED",std::make_shared<SeedRoute>(caps,backend));}});
    queue_->add_region(queue_scene_);
}
SceneCapabilities::~SceneCapabilities(){try{shutdown();}catch(const std::exception& e){try{state_->backend->error(e.what());}catch(...){}}}
SceneCapabilityEvents& SceneCapabilities::events(){return state_->events;}
std::shared_ptr<SceneCapabilityEvents> SceneCapabilities::shared_events()const{return std::shared_ptr<SceneCapabilityEvents>(state_,&state_->events);}
void SceneCapabilities::client_closed(framework::Uuid agent){state_->events.trigger_closed(agent,queue_scene_);}
void SceneCapabilities::shutdown(){if(stopped_.exchange(true))return;std::vector<std::shared_ptr<Caps>> objects;{std::lock_guard lock(state_->mutex);for(auto& weak:state_->objects)if(auto caps=weak.lock())objects.push_back(std::move(caps));state_->objects.clear();}std::exception_ptr first;for(auto& caps:objects)try{caps->dispose();}catch(...){if(!first)first=std::current_exception();}queue_->remove_region(queue_scene_);capabilities_->remove_region(caps_scene_);state_->events.on_register_caps.remove(SceneDelegateIdentity{this,"Seed.Register"});if(first)std::rethrow_exception(first);}
}
