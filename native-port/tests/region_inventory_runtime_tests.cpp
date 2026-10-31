// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_inventory_runtime.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/server/http_route_registry.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace opensim;using namespace services;
namespace {
int checks=0;void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
struct Backend final:SceneCapabilityBackend {
 std::shared_ptr<server::RegistryCapabilityListener> registry;
 explicit Backend(std::shared_ptr<server::RegistryCapabilityListener> r):registry(std::move(r)){}
 std::string scene_name()const override{return "Inventory scene";}
 std::optional<std::string> region_name()const override{return "Inventory region";}
 std::optional<std::string> external_host()const override{return "127.0.0.1";}
 std::shared_ptr<CapabilityRuntimeListener> listener()const override{return registry;}
 bool check_client(framework::Uuid,const network::HttpRequest&)override{return true;}
 void debug(const std::string&)override{}void warn(const std::string&)override{}void error(const std::string&)override{}void console(const std::string&)override{}void add_command(CapabilityCommand)override{}
};
struct ConfigFile {
 std::filesystem::path path=std::filesystem::temp_directory_path()/("opensim-inventory-"+framework::Uuid::random().str()+".ini");
 explicit ConfigFile(const std::string& url,std::optional<std::string> library_url={}){std::ofstream file(path);file<<"[ClientStack.LindenCaps]\nCap_FetchInventory2="<<url<<"\n";if(library_url)file<<"Cap_FetchLib2="<<*library_url<<"\n";}
 ~ConfigFile(){std::error_code error;std::filesystem::remove(path,error);}
};
std::shared_ptr<Caps> create(SceneCapabilities& scene,framework::Uuid agent,int code){auto circuit=std::make_shared<framework::AgentCircuitData>();circuit->agent_id=agent;circuit->circuit_code=code;circuit->caps_path="inventory"+std::to_string(code);scene.capabilities().set_agent_caps_seeds(circuit);scene.capabilities().create_caps(agent,code);scene.capabilities().activate_caps(code);return std::dynamic_pointer_cast<Caps>(scene.capabilities().get_caps_for_user(code));}
void run(){
 auto registry=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,19002,0,{}});SceneCapabilities scene(std::make_shared<Backend>(registry));auto before=scene.events().on_register_caps.snapshot().size();
 ConfigFile file("localhost");auto config=server::IniConfig::load(file.path);framework::Uuid first("00000000-0000-0000-0000-000000000001"),second("00000000-0000-0000-0000-000000000002"),item_id("00000000-0000-0000-0000-000000000003");framework::Uuid selected;int calls=0;
 auto provider=[&](auto agent,const auto& ids)->std::optional<FetchInventory2Core::Items>{selected=agent;++calls;check(ids.size()==1&&ids[0]==item_id,"native route decodes requested item ID");InventoryItem item;item.id=item_id;item.owner=agent;item.name="native item";return FetchInventory2Core::Items{item};};
 RegionInventoryRuntime runtime(config,scene.shared_events(),provider);runtime.poll();
 check(scene.events().on_register_caps.snapshot().size()==before+1,"inventory host subscribes once");
 auto a=create(scene,first,123),b=create(scene,second,124);check(bool(a)&&bool(b),"real per-agent Caps constructed");check(registry->routes.simple_keys().size()==6,"two seeds and four inventory item/library routes");
 std::shared_ptr<server::CapabilitySimpleHandler> route_a,route_b;
 auto paths_a=a->local_paths(),paths_b=b->local_paths();
 check(paths_a.contains("FetchInventory2")&&paths_b.contains("FetchInventory2"),"per-agent item capability advertised");
 check(paths_a.contains("FetchLib2"),"source default advertises library route even without service");
 check(paths_a.at("FetchInventory2")!=paths_b.at("FetchInventory2"),"distinct random paths per agent");
 check(registry->routes.try_simple(paths_a.at("FetchInventory2"),route_a)&&registry->routes.try_simple(paths_b.at("FetchInventory2"),route_b),"real registry owns inventory routes");
 network::HttpRequest request;request.method="POST";request.content_type="application/llsd+xml";request.body="<llsd><map><key>items</key><array><map><key>item_id</key><uuid>"+item_id.str()+"</uuid></map></array></map></llsd>";
 auto response=route_a->handle(request);check(response.status==200&&response.body.find("native item")!=response.body.npos&&selected==first,"first route returns item for captured owner");
 response=route_b->handle(request);check(response.status==200&&selected==second&&calls==2,"second route retains distinct captured owner");
 std::shared_ptr<server::CapabilitySimpleHandler> seed;check(registry->routes.try_simple("/CAPS/inventory1230000",seed),"seed route available");
 auto seed_request=request;seed_request.body="<llsd><array><string>FetchInventory2</string><string>FetchLib2</string></array></llsd>";
 response=seed->handle(seed_request);check(response.status==200&&response.body.find("FetchInventory2")!=response.body.npos&&response.body.find("FetchLib2")!=response.body.npos,"seed exposes source item and library routes");
 std::shared_ptr<server::CapabilitySimpleHandler> absent_library;check(registry->routes.try_simple(paths_a.at("FetchLib2"),absent_library)&&absent_library->handle(request).status==404,"source missing library service route404");
 auto snapshot=scene.events().on_register_caps.snapshot();runtime.stop();runtime.stop();runtime.poll();
 check(scene.events().on_register_caps.snapshot().size()==before,"stop unsubscribes idempotently");
 auto c=create(scene,framework::Uuid::random(),125);check(!c->local_paths().contains("FetchInventory2"),"stop prevents new registrations");
 snapshot.back().invoke(first,c);check(!c->local_paths().contains("FetchInventory2"),"stale event snapshot respects stopped state");
 check(route_a->handle(request).status==200&&selected==first,"caps-owned route remains valid after host stop");
 scene.shutdown();scene.shutdown();check(registry->routes.simple_keys().empty()&&registry->routes.poll_keys().empty(),"caps owner shutdown removes all routes");
 {
  ConfigFile disabled("");RegionInventoryRuntime empty(server::IniConfig::load(disabled.path),scene.shared_events(),{});empty.poll();empty.stop();check(scene.events().on_register_caps.snapshot().empty(),"disabled module needs no provider or subscription");
 }
 bool failed=false;try{RegionInventoryRuntime invalid(config,scene.shared_events(),{});}catch(const std::invalid_argument&){failed=true;}
 check(failed&&scene.events().on_register_caps.snapshot().empty(),"enabled module missing provider fails without subscription");
 {
  auto external_registry=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,19003,0,{}});SceneCapabilities external_scene(std::make_shared<Backend>(external_registry));ConfigFile external("https://inventory.invalid/");RegionInventoryRuntime external_runtime(server::IniConfig::load(external.path),external_scene.shared_events(),provider);auto caps=create(external_scene,first,126);check(!caps->local_paths().contains("FetchInventory2")&&!caps->external_paths().contains("FetchInventory2"),"source RegisterCaps skips non-localhost inventory value");check(caps->local_paths().contains("FetchLib2"),"library default localhost registers independently of external inventory URL");external_runtime.stop();external_scene.shutdown();
 }
 {
  auto other_registry=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,19004,0,{}});SceneCapabilities other(std::make_shared<Backend>(other_registry));SceneCapabilityEvents::CapsEvent::Handler saved;
  {RegionInventoryRuntime temporary(config,other.shared_events(),provider);saved=other.events().on_register_caps.snapshot().back();}
  auto caps=create(other,first,127);saved.invoke(first,caps);check(!caps->local_paths().contains("FetchInventory2"),"weak delegate does not resurrect destroyed runtime");other.shutdown();
 }
 {
  auto r=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,19007,0,{}});SceneCapabilities library_scene(std::make_shared<Backend>(r));LibraryItemBackend library;library.root_owner=second;int library_calls=0;library.get_items=[&](const auto& ids)->std::optional<FetchInventory2Core::Items>{++library_calls;check(ids.size()==1&&ids.front()==item_id,"library provider receives actual requested IDs");InventoryItem item;item.id=item_id;item.owner=second;item.name="library item";item.flags=0x1234;return FetchInventory2Core::Items{item};};RegionInventoryRuntime host(config,library_scene.shared_events(),provider,library);auto one=create(library_scene,first,128),two=create(library_scene,second,129);std::shared_ptr<server::CapabilitySimpleHandler> first_route,second_route;check(r->routes.try_simple(one->local_paths().at("FetchLib2"),first_route)&&r->routes.try_simple(two->local_paths().at("FetchLib2"),second_route),"provided library registers per-agent routes");auto first_response=first_route->handle(request),second_response=second_route->handle(request);check(first_response.status==200&&first_response.body.find("<key>agent_id</key><uuid>"+first.str())!=std::string::npos&&second_response.body.find("<key>agent_id</key><uuid>"+second.str())!=std::string::npos,"library routes capture distinct requesting agents");check(first_response.body.find("<key>flags</key><integer>4660</integer>")!=std::string::npos&&library_calls==2,"library route preserves full flags");auto stale=library_scene.events().on_register_caps.snapshot().back();host.stop();auto after=create(library_scene,first,130);stale.invoke(first,after);check(!after->local_paths().contains("FetchLib2"),"stopped snapshot cannot register library routes");check(first_route->handle(request).status==200,"caps retains library provider after host stop");library_scene.shutdown();check(r->routes.simple_keys().empty()&&r->routes.poll_keys().empty(),"library capability routes cleaned by caps owner");
 }
 for(const auto& url:{std::string{},std::string("https://library.invalid/")}){
  auto r=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,19008,0,{}});SceneCapabilities scoped(std::make_shared<Backend>(r));ConfigFile selected_config("localhost",url);RegionInventoryRuntime host(server::IniConfig::load(selected_config.path),scoped.shared_events(),provider);auto caps=create(scoped,first,131);check(!caps->local_paths().contains("FetchLib2")&&!caps->external_paths().contains("FetchLib2"),"explicit nonlocal library URL is skipped as source RegisterCaps");host.stop();scoped.shutdown();
 }
}
}
int main(){try{run();std::cout<<"PASS: "<<checks<<" region inventory runtime checks\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
