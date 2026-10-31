#include "opensim/services/scene_capabilities.hpp"
#include "opensim/server/http_route_registry.hpp"
#include <iostream>
using namespace opensim;using namespace services;
namespace {
int count=0;
void check(bool value,const char* text){++count;if(!value)throw std::runtime_error(text);}
struct Backend:SceneCapabilityBackend {
    std::shared_ptr<server::RegistryCapabilityListener> registry,main;int commands=0,errors=0;
    explicit Backend(std::shared_ptr<server::RegistryCapabilityListener> r):registry(std::move(r)),main(registry){}
    std::string scene_name()const override{return "Scene";}
    std::optional<std::string> region_name()const override{return "Region";}
    std::optional<std::string> external_host()const override{return "127.0.0.1";}
    std::shared_ptr<CapabilityRuntimeListener> listener()const override{return registry;}
    std::shared_ptr<CapabilityRuntimeListener> main_server()const override{return main;}
    bool check_client(framework::Uuid,const network::HttpRequest&)override{return true;}
    void debug(const std::string&)override{}void warn(const std::string&)override{}void error(const std::string&)override{++errors;}void console(const std::string&)override{}void add_command(CapabilityCommand)override{++commands;}
};
void run(){
    auto registry=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,9000,0,{}});auto backend=std::make_shared<Backend>(registry);std::weak_ptr<Backend> weak_backend=backend;std::weak_ptr<Caps> weak_caps;std::weak_ptr<EventQueueModule> weak_queue;
    std::shared_ptr<EventQueueModule> queue;std::shared_ptr<CapabilityPollHandler> poll;std::shared_ptr<server::CapabilitySimpleHandler> seed;std::shared_ptr<Caps> saved;
    framework::Uuid agent("00000000-0000-0000-0000-000000000002");
    {SceneCapabilities runtime(backend);queue=runtime.event_queue();weak_queue=queue;check(backend->commands==5,"module console commands");check(runtime.events().on_register_caps.snapshot().size()==2&&runtime.events().on_client_closed.snapshot().size()==1,"source registration event wiring");
    auto circuit=std::make_shared<framework::AgentCircuitData>();circuit->agent_id=agent;circuit->circuit_code=123;circuit->caps_path="seed";runtime.capabilities().set_agent_caps_seeds(circuit);runtime.capabilities().create_caps(agent,123);saved=std::dynamic_pointer_cast<Caps>(runtime.capabilities().get_caps_for_user(123));weak_caps=saved;check(bool(saved),"real native Caps constructed");check(registry->routes.simple_keys().size()==1&&registry->routes.poll_keys().size()==1,"seed and poll registered");check(registry->routes.try_simple("/CAPS/seed0000",seed),"selected seed route");check(saved->try_get_poll("EventQueueGet",poll),"selected poll handler");
    runtime.capabilities().activate_caps(123);network::HttpRequest request;request.method="POST";request.content_type="application/llsd+xml";request.body="<llsd><array><string>EventQueueGet</string></array></llsd>";auto response=seed->handle(request);check(response.status==200&&response.body.find("http://127.0.0.1:9000/CE/")!=std::string::npos,"real seed advertises queue");
    queue->windlight_refresh(1,agent);check(poll->has_events({},agent),"producer queued before owner shutdown");runtime.shutdown();runtime.shutdown();check(registry->routes.simple_keys().empty()&&registry->routes.poll_keys().empty(),"shutdown removes registrations idempotently");check(runtime.events().on_register_caps.snapshot().empty()&&runtime.events().on_client_closed.snapshot().empty(),"shutdown removes module event delegates");
    auto events=poll->get_events({},agent);check(std::any_cast<int32_t>(events->get("int_response_code"))==200,"selected poll retains source queue after region removal");bool threw=false;try{seed->handle(request);}catch(const std::exception&){threw=true;}check(threw,"selected seed observes disposed Caps activation");}
    check(!weak_caps.expired()&&!weak_queue.expired(),"selected handlers retain actors after owner destruction");backend.reset();check(!weak_backend.expired(),"selected handlers retain backend lifetime");check(!queue->enqueue(nullptr,framework::Uuid{}),"retained queue safely reports missing scene");seed.reset();saved.reset();check(weak_caps.expired(),"disposed seed/Caps ownership cycle released");poll.reset();queue.reset();check(weak_queue.expired()&&weak_backend.expired(),"retained callbacks release queue and backend");
    bool bad=false;try{SceneCapabilities invalid(nullptr);}catch(const std::invalid_argument&){bad=true;}check(bad,"null owner dependency rejected");
    auto other=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,9001,0,{}});auto separate=std::make_shared<Backend>(registry);separate->main=other;
    {SceneCapabilities runtime(separate);auto circuit=std::make_shared<framework::AgentCircuitData>();circuit->agent_id=agent;circuit->caps_path="main";runtime.capabilities().set_agent_caps_seeds(circuit);runtime.capabilities().create_caps(agent,124);auto caps=std::dynamic_pointer_cast<Caps>(runtime.capabilities().get_caps_for_user(124));check(caps->http_listener()==other&&other->routes.simple_keys().size()==1&&other->routes.poll_keys().size()==1&&registry->routes.simple_keys().empty(),"source CapabilitiesModule constructs on MainServer listener");}
    check(other->routes.simple_keys().empty()&&other->routes.poll_keys().empty(),"separate MainServer owner routes released");
}
}
int main(){try{run();std::cout<<"PASS: "<<count<<" scene capability lifetime checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
