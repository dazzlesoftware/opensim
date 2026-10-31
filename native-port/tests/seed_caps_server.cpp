// Real socket/activation/capability composition with controlled scene checks.
#include "opensim/server/http_route_registry.hpp"
#include "opensim/network/osd_json.hpp"
#include <atomic>
#include <iostream>
using namespace opensim;
struct Handler:server::CapabilityStreamHandler {std::string route,verb,label;Handler(std::string p,std::string m,std::string n):route(std::move(p)),verb(std::move(m)),label(std::move(n)){}std::optional<std::string> path()const override{return route;}std::string method()const override{return verb;}network::HttpResponse handle(const network::HttpRequest& request)override{network::HttpResponse response;response.body=label+":"+request.method+":"+request.body;return response;}};
#include "opensim/server/seed_caps.hpp"
#include "opensim/services/scene_client_verification.hpp"
struct Backend;
struct Client:services::VerifiedSceneClient {
    framework::Uuid session_id()const override{return framework::Uuid("00000000-0000-0000-0000-000000000002");}
};
struct Presence:services::VerifiedScenePresence {
    framework::Uuid agent;
    explicit Presence(framework::Uuid id):agent(id){}
    std::shared_ptr<services::VerifiedSceneClient> controlling_client()const override{return std::make_shared<Client>();}
    framework::Uuid agent_id()const override{return agent;}
};
struct Verifier:services::UserAgentVerificationModule {
    Backend& backend;explicit Verifier(Backend& b):backend(b){}
    bool verify_client(const std::shared_ptr<framework::AgentCircuitData>& circuit,const std::string& address)override;
};
struct Backend:server::SeedCapsBackend,services::SceneClientVerificationBackend {
    services::Caps& object;
    mutable framework::AgentCircuitManager circuits;
    services::NullPresenceStore store;
    services::PresenceService sessions;
    services::SceneClientVerification policy;
    std::shared_ptr<Presence> actor;
    std::shared_ptr<Verifier> verifier;
    std::atomic<bool> allow=true,module_enabled=true,present=true,service_available=true,close_fails=false;
    std::atomic<std::uint32_t> width=256,flags=0x40000000;
    std::atomic<int> pending=0,checks=0,remote_port=0,verifications=0,closed=0,cleanup_errors=0;
    explicit Backend(services::Caps& value):object(value),sessions(store,false,std::make_shared<services::PresenceCache>([]{return 0.;})),policy(*this),actor(std::make_shared<Presence>(object.agent_id())),verifier(std::make_shared<Verifier>(*this)){admit();}
    void admit(){auto circuit=std::make_shared<framework::AgentCircuitData>();circuit->agent_id=object.agent_id();circuit->session_id=Client{}.session_id();circuit->circuit_code=123;circuit->teleport_flags=flags;circuits.add_new_circuit(circuit);sessions.login(circuit->agent_id.str(),circuit->session_id,framework::Uuid("00000000-0000-0000-0000-000000000003"));present=true;}
    services::Caps& caps()override{return object;}
    std::optional<std::string> region_name()const override{return "Region";}
    framework::AgentCircuitManager* authenticate_handler()const override{return &circuits;}
    std::shared_ptr<services::UserAgentVerificationModule> user_verification_module()override{return module_enabled?verifier:nullptr;}
    std::shared_ptr<services::VerifiedScenePresence> scene_presence(const framework::Uuid& agent)override{return present&&agent==actor->agent?actor:nullptr;}
    services::IPresenceService* presence_service()override{return service_available?&sessions:nullptr;}
    void close_agent(const framework::Uuid&,bool)override{if(close_fails)throw std::runtime_error("controlled close failure");present=false;++closed;}
    void cleanup_exception(const std::exception&)override{++cleanup_errors;}
    bool check_client(const framework::Uuid& agent,const network::HttpRequest& r)override{++checks;remote_port=r.remote_port;auto address=framework::IpAddress::parse(r.remote_ip);if(!address)throw std::invalid_argument("Invalid peer address");framework::IpEndpoint endpoint{std::make_shared<framework::IpAddress>(*address),static_cast<std::uint16_t>(r.remote_port)};return policy.check_client(agent,&endpoint);}
    std::uint32_t region_size_x()const override{return width;}
    std::uint32_t region_size_y()const override{return 256;}
    void debug(const std::string&)override{}
    void warn(const std::string&)override{}
};
bool Verifier::verify_client(const std::shared_ptr<framework::AgentCircuitData>& circuit,const std::string& address){++backend.verifications;return backend.allow&&circuit->agent_id==backend.object.agent_id()&&(address=="127.0.0.1"||address=="::1");}
struct SeedRoute:server::SeedCapsHandler {
    Backend& backend;
    explicit SeedRoute(Backend& b):SeedCapsHandler(b),backend(b){}
    network::HttpResponse handle(const network::HttpRequest& r)override{++backend.pending;try{auto response=SeedCapsHandler::handle(r);--backend.pending;return response;}catch(...){--backend.pending;throw;}}
};
struct Runtime {
    std::shared_ptr<server::RegistryCapabilityListener> listener;
    services::Caps object;
    Backend backend;
    explicit Runtime(int port):listener(std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,static_cast<std::uint32_t>(port),0,{}})),object(listener,"127.0.0.1",static_cast<std::uint32_t>(port),"seed",framework::Uuid("00000000-0000-0000-0000-000000000001"),"Region",[server=listener]{return server;}),backend(object){
object.register_simple("SEED",std::make_shared<SeedRoute>(backend));object.register_handler("A",std::make_shared<Handler>("/cap/action","POST","action"));for(const auto& name:{"ObjectAnimation","EnvironmentSettings","ExtEnvironment","ModifyMaterialParams"})object.register_external(name,"https://external/"+std::string(name));object.register_external("External","https://external/cap?a=1&b=2");auto poll=std::make_shared<services::CapabilityPollHandler>();poll->url="/poll/controlled";object.register_poll("Poll",poll);
    }
    network::HttpResponse request(const network::HttpRequest& r){network::HttpResponse response;if(r.path=="/ready"){response.status=503;return response;}if(r.path=="/state"){auto value=network::Value::object({});value.set("flags",network::Value::long_integer(object.flags));value.set("pending",network::Value(backend.pending.load()));value.set("checks",network::Value(backend.checks.load()));value.set("remote_port",network::Value(backend.remote_port.load()));value.set("circuits",network::Value(static_cast<std::int32_t>(backend.circuits.get_agent_circuits().size())));value.set("online",network::Value(backend.sessions.agent(Client{}.session_id()).has_value()));value.set("present",network::Value(backend.present.load()));value.set("verifications",network::Value(backend.verifications.load()));value.set("closed",network::Value(backend.closed.load()));value.set("cleanup_errors",network::Value(backend.cleanup_errors.load()));response.content_type="application/json";response.body=network::osd_json(value,true);return response;}if(r.path=="/control/activate"){object.activate();return response;}if(r.path=="/control/dispose"){object.dispose();return response;}if(r.path=="/control/deny"||r.path=="/control/allow"){backend.allow=r.path=="/control/allow";return response;}if(r.path=="/control/admit"){backend.admit();return response;}if(r.path=="/control/remove-circuit"){backend.circuits.remove_circuit(object.agent_id());return response;}if(r.path=="/control/module"||r.path=="/control/no-module"){backend.module_enabled=r.path=="/control/module";return response;}if(r.path=="/control/service"||r.path=="/control/no-service"){backend.service_available=r.path=="/control/service";return response;}if(r.path=="/control/close-fail"||r.path=="/control/close-ok"){backend.close_fails=r.path=="/control/close-fail";return response;}if(r.path=="/control/flags"){backend.flags=static_cast<std::uint32_t>(std::stoul(r.body));if(auto circuit=backend.circuits.get_agent_circuit_data(object.agent_id()))circuit->teleport_flags=backend.flags;return response;}if(r.path=="/control/reset"){object.flags=0;return response;}if(r.path=="/control/size"){backend.width=static_cast<std::uint32_t>(std::stoul(r.body));return response;}return listener->dispatch(r);}
};
int main(){try{std::unique_ptr<Runtime> runtime;network::HttpServerOptions options;options.port=0;network::HttpServer http(options,[&](const network::HttpRequest& request){return runtime->request(request);});http.start([&](int port){runtime=std::make_unique<Runtime>(port);std::cout<<"LISTENING "<<port<<std::endl;});std::string line;std::getline(std::cin,line);http.stop();runtime->object.dispose();return 0;}catch(const std::exception& exception){std::cerr<<exception.what();return 1;}}
