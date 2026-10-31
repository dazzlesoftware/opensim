// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_asset_runtime.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/server/http_route_registry.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace opensim;
using namespace opensim::services;
namespace {
int checks=0;
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
std::string text(const std::filesystem::path& path){auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
struct TemporaryDirectory {
 std::filesystem::path path=std::filesystem::temp_directory_path()/("opensim-region-assets-"+framework::Uuid::random().str());
 TemporaryDirectory(){std::filesystem::create_directory(path);}
 ~TemporaryDirectory(){std::error_code error;std::filesystem::remove_all(path,error);}
};
struct Backend final:SceneCapabilityBackend {
 std::shared_ptr<server::RegistryCapabilityListener> registry;
 explicit Backend(std::shared_ptr<server::RegistryCapabilityListener> listener):registry(std::move(listener)){}
 std::string scene_name()const override{return "Asset test scene";}
 std::optional<std::string> region_name()const override{return "Asset test region";}
 std::optional<std::string> external_host()const override{return "127.0.0.1";}
 std::shared_ptr<CapabilityRuntimeListener> listener()const override{return registry;}
 bool check_client(framework::Uuid,const network::HttpRequest&)override{return true;}
 void debug(const std::string&)override{}void warn(const std::string&)override{}
 void error(const std::string&)override{}void console(const std::string&)override{}
 void add_command(CapabilityCommand)override{}
};
server::IniConfig config(const TemporaryDirectory& directory,const std::string& filename,bool connector,const std::string& provider="OpenSim.Data.SQLite.dll") {
 auto path=directory.path/filename;std::ofstream file(path,std::ios::binary);
 file<<"[Modules]\nAssetServices="<<(connector?"LocalAssetServicesConnector":"")
  <<"\n[AssetService]\nLocalServiceModule=OpenSim.Services.AssetService.dll:AssetService\nStorageProvider="<<provider
  <<"\nConnectionString=URI=file:"<<text(directory.path/"assets.db")<<";version=3;Pooling=false"
  <<"\nAssetLoaderEnabled=false\n[ClientStack.LindenCaps]\nCap_GetTexture=localhost\nCap_GetMesh=localhost\nCap_GetMesh2=localhost\nCap_GetAsset=localhost\n";
 file.close();if(!file)throw std::runtime_error("Cannot write test configuration");return server::IniConfig::load(path);
}
void cache_profiles(){
 TemporaryDirectory directory;config(directory,"cache.ini",true);auto file=directory.path/"cache.ini";
 {std::ofstream out(file,std::ios::app);out<<"\n[Modules]\nAssetCaching=FlotsamAssetCache\n[AssetCache]\nFileCacheEnabled=false\nMemoryCacheEnabled=true\nMemoryCacheTimeout=0.25\n";}
 auto settings=server::IniConfig::load(file);auto local=framework::Uuid::random();
 for(int pass=0;pass<2;++pass){auto registry=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,19002,0,{}});SceneCapabilities scene(std::make_shared<Backend>(registry));RegionAssetRuntime assets(settings,scene.shared_events());check(bool(assets.asset_cache()),"selected real memory cache exposed");if(pass==0){auto a=std::make_shared<framework::AssetBase>(local,"local bake",std::int8_t(0),"creator");a->local(true);a->temporary(true);a->data=std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{0,255,1});check(assets.store(a)==local.str()&&assets.get(local)==a&&assets.asset_cache()->get_cached(local.str())==a,"real connector retains local asset in cache without ordinary persistence");}else check(!assets.get(local),"local cached asset is absent from actual SQLite after cache/runtime restart");assets.stop();scene.shutdown();}
 {std::ofstream out(file,std::ios::app);out<<"\n[AssetCache]\nFileCacheEnabled=true\n";}
 auto registry=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,19003,0,{}});SceneCapabilities scene(std::make_shared<Backend>(registry));auto before=scene.events().on_register_caps.snapshot().size();bool rejected=false;try{RegionAssetRuntime assets(server::IniConfig::load(file),scene.shared_events());}catch(const std::invalid_argument& e){rejected=std::string(e.what()).find("file cache is not implemented")!=std::string::npos;}check(rejected&&scene.events().on_register_caps.snapshot().size()==before,"unimplemented file cache fails explicitly without alternate persistence or subscription");scene.shutdown();
}
void run(){
 TemporaryDirectory directory;auto settings=config(directory,"assets.ini",true);
 // Each pass owns fresh connector/module state; the SQLite database survives.
 for(int pass=0;pass<2;++pass){
  auto registry=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,19000,0,{}});
  SceneCapabilities capabilities(std::make_shared<Backend>(registry));
  auto registrations=capabilities.events().on_register_caps.snapshot().size();
  RegionAssetRuntime assets(settings,capabilities.shared_events(),"Asset region");
  check(capabilities.events().on_register_caps.snapshot().size()==registrations+1,"asset module subscribes once");
  framework::Uuid stored_id("00000000-0000-0000-0000-00000000a501");std::vector<std::uint8_t> stored_bytes{'L','S','L',0,0xff};
  if(pass==0){auto asset=std::make_shared<framework::AssetBase>(stored_id,"Stored script",std::int8_t(10),std::string("creator"));asset->description("Actual script bytes");asset->data=std::make_shared<std::vector<std::uint8_t>>(stored_bytes);check(assets.store(asset)==stored_id.str(),"real local service stores script asset");}
  auto loaded=assets.get(stored_id);check(loaded&&loaded->type()==10&&loaded->data&&*loaded->data==stored_bytes,"stored script bytes survive actual database/runtime restart");

  auto circuit=std::make_shared<framework::AgentCircuitData>();
  circuit->agent_id=framework::Uuid("00000000-0000-0000-0000-000000000007");
  circuit->circuit_code=123;circuit->caps_path="asset-test";
  capabilities.capabilities().set_agent_caps_seeds(circuit);
  capabilities.capabilities().create_caps(circuit->agent_id,circuit->circuit_code);
  capabilities.capabilities().activate_caps(circuit->circuit_code);
  auto caps=std::dynamic_pointer_cast<Caps>(capabilities.capabilities().get_caps_for_user(circuit->circuit_code));
  check(bool(caps),"real circuit caps created");
  check(registry->routes.simple_keys().size()==1,"single seed route registered");
  check(registry->routes.poll_keys().size()==5,"four asset routes plus event queue registered");
  for(auto name:{"GetTexture","GetMesh","GetMesh2","ViewerAsset"}){
   std::shared_ptr<CapabilityPollHandler> poll;check(caps->try_get_poll(name,poll)&&bool(poll),"asset poll capability exists");
  }
  std::shared_ptr<server::CapabilitySimpleHandler> seed;
  check(registry->routes.try_simple("/CAPS/asset-test0000",seed),"source seed path exists");
  network::HttpRequest request;request.method="POST";request.content_type="application/llsd+xml";
  request.body="<llsd><array><string>GetTexture</string><string>GetMesh</string><string>GetMesh2</string><string>ViewerAsset</string></array></llsd>";
  auto response=seed->handle(request);check(response.status==200,"seed negotiation succeeds");
  for(auto name:{"GetTexture","GetMesh","GetMesh2","ViewerAsset"})check(response.body.find(name)!=std::string::npos,"seed advertises asset name");
  assets.stop();assets.stop();
  bool closed=false;try{assets.get(stored_id);}catch(const std::exception&){closed=true;}check(closed,"closed asset runtime rejects compiler retrieval");

  check(capabilities.events().on_register_caps.snapshot().size()==registrations,"asset stop unsubscribes idempotently");
  capabilities.shutdown();capabilities.shutdown();
  check(registry->routes.simple_keys().empty()&&registry->routes.poll_keys().empty(),"owners release all capability routes");
  check(std::filesystem::is_regular_file(directory.path/"assets.db")&&std::filesystem::file_size(directory.path/"assets.db")>0,"real SQLite database exists across runtime restart");
 }
 for(auto invalid:{config(directory,"disabled.ini",false),config(directory,"unsupported.ini",true,"Unsupported.Asset.Provider")}){
  auto registry=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{false,19001,0,{}});
  SceneCapabilities capabilities(std::make_shared<Backend>(registry));auto before=capabilities.events().on_register_caps.snapshot().size();
  bool failed=false;try{RegionAssetRuntime assets(invalid,capabilities.shared_events());}catch(const std::exception&){failed=true;}
  check(failed,"invalid asset service configuration rejects construction");
  check(capabilities.events().on_register_caps.snapshot().size()==before,"failed construction leaves no asset subscription");
  check(registry->routes.simple_keys().empty()&&registry->routes.poll_keys().empty(),"failed construction advertises no routes");
  capabilities.shutdown();
 }
}
}
int main(){try{run();cache_profiles();std::cout<<"PASS: "<<checks<<" region asset runtime checks\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
