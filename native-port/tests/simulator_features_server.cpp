// Native capability module composition with controlled world/grid/authorization providers.
#include "opensim/services/simulator_features.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/server/poll_http_service.hpp"
#include "opensim/network/osd_json.hpp"
#include <fstream>
#include <iostream>
using namespace opensim;using namespace services;using namespace server;using namespace network;
struct Backend:SceneCapabilityBackend,SimulatorFeaturesBackend {
    std::shared_ptr<RegistryCapabilityListener> registry;std::atomic<int> errors=0,mode=0,calls=0,followers=0;std::atomic<uint32_t> width=256;
    explicit Backend(std::shared_ptr<RegistryCapabilityListener> r):registry(std::move(r)){}
    std::string scene_name()const override{return "Scene";}std::optional<std::string> region_name()const override{return "Region";}std::optional<std::string> external_host()const override{return "127.0.0.1";}std::shared_ptr<CapabilityRuntimeListener> listener()const override{return registry;}bool check_client(framework::Uuid,const HttpRequest&)override{return true;}uint32_t region_size_x()const override{return width;}
    framework::Uuid random_uuid()override{return framework::Uuid::random();}
    void debug(const std::string&)override{}void warn(const std::string&)override{}void error(const std::string&)override{++errors;}void console(const std::string&)override{}void add_command(CapabilityCommand)override{}
};
struct GridInfo:SimulatorGridInfo {void set(const std::string&,const std::optional<std::string>&)override{}void aliases(std::vector<std::string>)override{}};
struct Scene:SimulatorFeaturesScene {
    std::shared_ptr<SceneCapabilityEvents> source;SimulatorFeaturesModule* registered=nullptr;
    explicit Scene(std::shared_ptr<SceneCapabilityEvents> events):source(std::move(events)){}
    SceneCapabilityEvents& events()override{return *source;}void register_module(SimulatorFeaturesModule* module)override{registered=module;}
    std::shared_ptr<SimulatorGridExtras> extra_features()override{auto fields=std::make_shared<SimulatorGridExtras>();(*fields)["Result"]=std::string("Success");(*fields)["Custom"]=std::string("grid-value");(*fields)["GridName"]=std::string("Grid");(*fields)["ExportSupported"]=std::string("false");return fields;}
    std::shared_ptr<SimulatorGridInfo> grid_info()override{return std::make_shared<GridInfo>();}
};
FeatureData value(Value data){return SimulatorFeatureValue::from_value(data);}
int main(int argc,char**argv){try{
    auto config=IniConfig::load("features.ini");std::shared_ptr<RegistryCapabilityListener> registry;std::shared_ptr<Backend> backend;std::unique_ptr<SceneCapabilities> scene_caps;std::shared_ptr<SimulatorFeaturesModule> module;std::shared_ptr<Scene> scene;std::shared_ptr<framework::AgentCircuitData> circuit;std::shared_ptr<CapabilitySimpleHandler> selected;PollHttpService polls(2);HttpServerOptions options;options.port=0;options.workers=2;if(argc==3){options.certificate=argv[1];options.private_key=argv[2];}
    AsyncHttpServer http(options,[&](const HttpRequest& request,std::shared_ptr<HttpResponseChannel> channel){polls.dispatch(*registry,request,channel,[&](const HttpRequest& r){HttpResponse response;
        if(r.path=="/baseline"){FeatureData data;module->try_feature("AnimatedObjects",data);auto bytes=simulator_feature_xml(data);response.body.assign(bytes->begin(),bytes->end());}
        else if(r.path=="/get-features"){auto bytes=simulator_feature_xml(module->get_features());response.body.assign(bytes->begin(),bytes->end());}
        else if(r.path=="/mode")backend->mode=std::stoi(r.body);
        else if(r.path=="/size")backend->width=static_cast<uint32_t>(std::stoul(r.body));
        else if(r.path=="/reset"){auto caps=std::dynamic_pointer_cast<Caps>(scene_caps->capabilities().get_caps_for_user(123));caps->flags=0;}
        else if(r.path=="/detach")module->remove_region(scene);
        else if(r.path=="/attach")module->add_region(scene);
        else if(r.path=="/register"){circuit->caps_path=r.body;scene_caps->capabilities().set_agent_caps_seeds(circuit);scene_caps->capabilities().create_caps(circuit->agent_id,123);scene_caps->capabilities().activate_caps(123);}
        else if(r.path=="/capture"){auto caps=std::dynamic_pointer_cast<Caps>(scene_caps->capabilities().get_caps_for_user(123));auto paths=caps->handlers().local_paths();registry->routes.try_simple(paths.at("SimulatorFeatures"),selected);}
        else if(r.path=="/dispose")scene_caps->capabilities().remove_caps(circuit->agent_id,123);
        else if(r.path=="/selected"){auto req=r;req.method="GET";return selected->handle(req);}
        else if(r.path=="/reload"){std::ofstream file("ScriptSyntax.xml",std::ios::binary);file<<"fedcba98-7654-3210-fedc-ba9876543210\nchanged";file.close();module->initialise(&config);}
        else if(r.path=="/state"){auto v=Value::object({});v.set("calls",Value(backend->calls.load()));v.set("followers",Value(backend->followers.load()));v.set("errors",Value(backend->errors.load()));v.set("simple",Value(int32_t(registry->routes.simple_keys().size())));v.set("polls",Value(int32_t(registry->routes.poll_keys().size())));response.body=osd_json(v,true);}
        else if(r.path=="/ready"){response.status=503;response.body="Native world runtime incomplete";}
        else response.status=404;return response;
    });});polls.start();http.start([&](int port){registry=std::make_shared<RegistryCapabilityListener>(CapabilityListenerInfo{argc==3,static_cast<uint32_t>(port),static_cast<uint32_t>(port),std::string("localhost")});backend=std::make_shared<Backend>(registry);scene_caps=std::make_unique<SceneCapabilities>(backend);scene=std::make_shared<Scene>(scene_caps->shared_events());module=std::make_shared<SimulatorFeaturesModule>(std::static_pointer_cast<SimulatorFeaturesBackend>(backend));module->initialise(&config);module->add_region(scene);module->region_loaded(scene);module->add_extra("destination-guide-url",value(Value("https://guide/?a=[USERID]&b=[USERID]")));
        module->on_request.add({{backend.get(),"First"},[backend](auto agent,FeatureData& copy){++backend->calls;auto mode=backend->mode.load();if(mode==2){copy=SimulatorFeatureValue::object();copy->map["Replacement"]=value(Value(true));return;}if(mode==3||mode==4){copy=nullptr;return;}copy->map["Callback"]=value(Value(agent.str()));auto animated=copy->map.at("AnimatedObjects");animated->map["AnimatedObjectMaxTris"]=value(Value(int32_t(1)));if(mode==1){copy->map["Partial"]=value(Value(true));throw std::runtime_error("controlled feature callback");}}});
        module->on_request.add({{backend.get(),"Follower"},[backend](auto,FeatureData& copy){++backend->followers;if(backend->mode==4){copy=SimulatorFeatureValue::object();copy->map["Recovered"]=value(Value(true));}else if(copy)copy->map["Follower"]=value(Value(true));}});
        circuit=std::make_shared<framework::AgentCircuitData>();circuit->agent_id=framework::Uuid("00000000-0000-0000-0000-000000000007");circuit->circuit_code=123;circuit->caps_path="features";scene_caps->capabilities().set_agent_caps_seeds(circuit);scene_caps->capabilities().create_caps(circuit->agent_id,123);scene_caps->capabilities().activate_caps(123);std::cout<<"LISTENING "<<port<<std::endl;});std::string line;std::getline(std::cin,line);polls.stop();http.stop();module->remove_region(scene);scene_caps->shutdown();selected.reset();scene_caps.reset();module.reset();scene.reset();if(!registry->routes.simple_keys().empty()||!registry->routes.poll_keys().empty())throw std::runtime_error("Feature owner retained routes");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
