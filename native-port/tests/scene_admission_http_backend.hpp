// Controlled service/actor/caps adapters for native admission HTTP integration.
#pragma once
#include "opensim/services/scene_admission.hpp"
#include "opensim/services/caps.hpp"
#include <thread>
struct HttpAdmissionBackend;
struct HttpCapsListener:opensim::services::CapabilityRuntimeListener {
    bool use_ssl()const override{return false;}
    std::uint32_t port()const override{return 0;}
    std::uint32_t ssl_port()const override{return 0;}
    std::optional<std::string> ssl_common_name()const override{return {};}
    void add_stream(const std::shared_ptr<opensim::server::CapabilityStreamHandler>&)override{}
    void remove_stream(const std::string&,const std::optional<std::string>&)override{}
    void add_simple(const std::shared_ptr<opensim::server::CapabilitySimpleHandler>&)override{}
    void remove_simple(const std::optional<std::string>&)override{}
    void add_poll(const std::shared_ptr<opensim::services::CapabilityPollHandler>&)override{}
    void remove_poll(const std::optional<std::string>&)override{}
};
struct HttpCapabilityObject:opensim::services::Caps {
    HttpAdmissionBackend& backend;
    bool active=false;
    HttpCapabilityObject(HttpAdmissionBackend& b,opensim::framework::Uuid a,std::optional<std::string> p);
    void activate()override;
    void dispose()override;
    void deregister_handlers()override;
};
struct HttpCapabilityScene:opensim::services::CapabilityModuleScene {
    HttpAdmissionBackend& backend;
    explicit HttpCapabilityScene(HttpAdmissionBackend& b):backend(b){}
    std::optional<std::string> region_name()const override;
    void register_module(opensim::services::CapabilitiesModule*)override{}
    void unregister_module(opensim::services::CapabilitiesModule*)override{}
    std::shared_ptr<opensim::services::CapabilityObject> make_caps(const std::optional<std::string>& path,const opensim::framework::Uuid& agent)override;
    void register_caps(const opensim::framework::Uuid&,const std::shared_ptr<opensim::services::CapabilityObject>&)override;
    void deregister_caps(const opensim::framework::Uuid&,const std::shared_ptr<opensim::services::CapabilityObject>&)override;
    void add_command(opensim::services::CapabilityCommand)override{}
    bool selected_for_console()const override{return true;}
    bool console_has_selected_scene()const override{return false;}
    std::optional<std::string> presence_name(const opensim::framework::Uuid&)override{return "Test User";}
    void console_output(const std::string&)override{}
    void info(const std::string&)override{}
};
struct HttpPresenceIdentity:opensim::services::PresenceStateIdentity {
    std::optional<std::string> presence_name()const override{return "Test User";}
    std::optional<std::string> scene_name()const override{return "Scene1";}
};
struct HttpAdmissionPresence:opensim::services::AdmissionPresence {
    std::shared_ptr<HttpPresenceIdentity> identity=std::make_shared<HttpPresenceIdentity>();
    opensim::services::PresenceStateMachine machine{identity};
    std::vector<std::string>& events;
    bool not_close=false;
    std::uint32_t flags=0;
    int adjustments=0;
    explicit HttpAdmissionPresence(std::vector<std::string>& e):events(e){machine.set_state(opensim::services::PresenceState::pre_remove);}
    opensim::framework::Uuid agent_id()const override{return id(8);}
    bool deleted()const override{return false;}
    bool child()const override{return true;}
    opensim::services::AdmissionPresenceState state()const override{return machine.get_state();}
    void state(opensim::services::AdmissionPresenceState value)override{machine.set_state(value);}
    void do_not_close_after_teleport(bool value)override{not_close=value;}
    bool has_client()const override{return true;}
    void teleport_flags(std::uint32_t value)override{flags=value;}
    void adjust_known_seeds()override{events.push_back("adjust");++adjustments;}
};
struct HttpAdmissionCaps:opensim::services::AdmissionCaps {
    HttpAdmissionBackend& backend;
    explicit HttpAdmissionCaps(HttpAdmissionBackend& value):backend(value){}
    void set_agent_caps_seeds(const std::shared_ptr<opensim::framework::AgentCircuitData>& circuit)override;
    void create_caps(const opensim::framework::Uuid&,std::uint32_t)override;
    void activate_caps(std::uint32_t)override;
};
struct HttpAdmissionBackend:opensim::services::SceneAdmissionBackend {
    HttpAccessBackend& access_backend;
    opensim::services::SceneAccess& access;
    opensim::server::IniConfig config;
    std::recursive_mutex remove_mutex,new_mutex;
    std::vector<std::string> events;
    std::shared_ptr<HttpCapsListener> capability_listener=std::make_shared<HttpCapsListener>();
    std::shared_ptr<HttpAdmissionCaps> module;
    std::shared_ptr<opensim::services::CapabilitiesModule> capabilities;
    std::shared_ptr<HttpCapabilityScene> capability_scene;
    std::shared_ptr<HttpAdmissionPresence> actor;
    std::shared_ptr<opensim::framework::AgentCircuitData> incoming;
    int activated=0;
    int caps_created=0,caps_registered=0,caps_deregistered=0,caps_disposed=0,handlers_deregistered=0;
    HttpAdmissionBackend(HttpAccessBackend& b,opensim::services::SceneAccess& a,const opensim::server::IniConfig& c):access_backend(b),access(a),config(c){if(config.boolean("Admission","Caps",true)){capabilities=std::make_shared<opensim::services::CapabilitiesModule>();capability_scene=std::make_shared<HttpCapabilityScene>(*this);capabilities->add_region(capability_scene);module=std::make_shared<HttpAdmissionCaps>(*this);}if(config.boolean("Admission","ExistingChild",false))actor=std::make_shared<HttpAdmissionPresence>(events);}
    opensim::services::AccessRegion& region()override{return access_backend.info;}
    std::recursive_mutex& remove_client_mutex()override{return remove_mutex;}
    std::recursive_mutex& new_user_mutex()override{return new_mutex;}
    void log_incoming(const std::shared_ptr<opensim::framework::AgentCircuitData>& a,std::uint32_t,const opensim::services::GridRegion*,const std::string&)override{incoming=a;events.clear();events.push_back("incoming");}
    bool logins_enabled()const override{return config.boolean("Admission","LoginsEnabled",true);}
    std::vector<std::optional<std::string>> allowed_viewers()const override{return {};}
    std::vector<std::optional<std::string>> banned_viewers()const override{return {"banned","σviewer","𐐨viewer"};}
    std::shared_ptr<opensim::services::AdmissionPresence> presence(const opensim::framework::Uuid& agent)override{events.push_back("scene-presence");return actor&&agent==actor->agent_id()?actor:nullptr;}
    bool in_transit(const opensim::framework::Uuid&)override{events.push_back("transit");return config.boolean("Admission","InTransit",false);}
    void sleep(int n)override{std::this_thread::sleep_for(std::chrono::milliseconds(n));}
    bool telehub_allow_landmarks()const override{return false;}
    bool check_land_access(const opensim::framework::Uuid& a,bool n,bool h,bool c,const opensim::framework::Vector3& p,std::string& reason)override{events.push_back("land");return access.check_land_position_access(a,n,h,c,p,reason);}
    void remove_circuit(const std::shared_ptr<opensim::framework::AgentCircuitData>& a)override{events.push_back("remove");access_backend.circuits.remove_circuit(a);}
    void close_agent(const opensim::framework::Uuid& a,bool)override{access_backend.circuits.remove_circuit(a);}
    void remove_cached_account(const opensim::framework::Uuid&)override{events.push_back("account-cache");}
    void add_circuit(const std::shared_ptr<opensim::framework::AgentCircuitData>& a)override{events.push_back("add");access_backend.circuits.add_new_circuit(a);}
    bool has_presence_service()const override{return !config.boolean("Admission","NoPresenceService",false);}
    std::optional<opensim::services::AdmissionGridPresence> grid_presence(const opensim::framework::Uuid&)override{events.push_back("grid-presence");if(config.boolean("Admission","MissingPresence",false))return {};return opensim::services::AdmissionGridPresence{incoming->agent_id.str()};}
    bool see_into_region()const override{return config.boolean("Admission","SeeIntoRegion",true);}
    bool authorize_user(const std::shared_ptr<opensim::framework::AgentCircuitData>& a,bool bypass,std::string& reason)override{events.push_back("authorize");return access.authorize_user(a,bypass,reason);}
    std::shared_ptr<opensim::services::AdmissionCaps> caps()override{return module;}
    void cache_user_name(const std::shared_ptr<opensim::framework::AgentCircuitData>&)override{events.push_back("name-cache");}
};
inline void HttpAdmissionCaps::set_agent_caps_seeds(const std::shared_ptr<opensim::framework::AgentCircuitData>& a){backend.events.push_back("seeds");backend.capabilities->set_agent_caps_seeds(a);}
inline void HttpAdmissionCaps::create_caps(const opensim::framework::Uuid& agent,std::uint32_t code){backend.events.push_back("create-caps");backend.capabilities->create_caps(agent,code);}
inline void HttpAdmissionCaps::activate_caps(std::uint32_t code){backend.events.push_back("activate");backend.capabilities->activate_caps(code);}
inline std::optional<std::string> HttpCapabilityScene::region_name()const{return backend.access_backend.info.name;}
inline std::shared_ptr<opensim::services::CapabilityObject> HttpCapabilityScene::make_caps(const std::optional<std::string>& path,const opensim::framework::Uuid& agent){if(backend.config.boolean("Admission","FailCaps",false))throw std::runtime_error("controlled caps failure");++backend.caps_created;return std::make_shared<HttpCapabilityObject>(backend,agent,path);}
inline void HttpCapabilityScene::register_caps(const opensim::framework::Uuid&,const std::shared_ptr<opensim::services::CapabilityObject>&){++backend.caps_registered;if(backend.config.boolean("Admission","FailRegister",false))throw std::runtime_error("controlled register failure");}
inline void HttpCapabilityScene::deregister_caps(const opensim::framework::Uuid&,const std::shared_ptr<opensim::services::CapabilityObject>&){++backend.caps_deregistered;}
inline HttpCapabilityObject::HttpCapabilityObject(HttpAdmissionBackend& b,opensim::framework::Uuid a,std::optional<std::string> p):opensim::services::Caps(b.capability_listener,"127.0.0.1",0,std::move(p),a,b.access_backend.info.name,[listener=b.capability_listener]{return listener;}),backend(b){}
inline void HttpCapabilityObject::activate(){opensim::services::Caps::activate();active=true;++backend.activated;}
inline void HttpCapabilityObject::deregister_handlers(){++backend.handlers_deregistered;opensim::services::Caps::deregister_handlers();}
inline void HttpCapabilityObject::dispose(){++backend.caps_disposed;active=false;opensim::services::Caps::dispose();}
