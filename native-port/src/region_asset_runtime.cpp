// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_asset_runtime.hpp"
#include "opensim/services/get_assets_module.hpp"
#include "opensim/services/local_asset_connector.hpp"
#include "opensim/services/native_asset_storage.hpp"
#include "opensim/services/flotsam_memory_cache.hpp"
#include "opensim/services/flotsam_cache_module.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/services/user_account.hpp"
#include <bit>
#include <algorithm>
#include <exception>
#include <chrono>
#include <thread>
namespace opensim::services {
namespace {
bool white(const std::string& text){auto chars=framework::utf16_replacing(text);return std::all_of(chars.begin(),chars.end(),framework::unicode_whitespace);}
struct Backend final:AssetsModuleBackend,AssetConnectorBackend {
 std::function<void(std::string)> log;
 std::shared_ptr<AssetStorageState> storage_state=std::make_shared<AssetStorageState>();
 std::shared_ptr<NativeAssetStorageBackend> storage;
 std::shared_ptr<ConfiguredAssetService> service;
 framework::ObjectJobEngine callbacks;
 explicit Backend(std::function<void(std::string)> logger):log(std::move(logger)),
  callbacks([](std::any value){std::any_cast<std::function<void()>>(value)();},"RegionAssetCallbacks",1000,3,
   [this](const auto& name,const auto& error){job_error(name,error);}) {
  // Capture the logger by value: storage may outlive the runtime through caps.
  storage=std::make_shared<NativeAssetStorageBackend>([logger=log](const auto& level,const auto& text){if(logger)logger(level+": "+text);});
 }
 void emit(const std::string& text)noexcept{try{if(log)log(text);}catch(...) {}}
 void warn(const std::string& text)override{emit(text);}
 void error(const std::string& text)override{emit(text);}
 void info(const std::string& text)override{emit(text);}
 void debug(const std::string& text)override{emit(text);}
 void job_error(const std::string& name,const std::exception& error)override{emit(name+": "+error.what());}
 void post(std::function<void()> callback,const std::string&)override{callbacks.enqueue(std::move(callback));}
 std::shared_ptr<AssetService> load_service(const std::string& module,const server::IniConfig* config)override {
  if(module!="OpenSim.Services.AssetService.dll:AssetService")
   throw std::invalid_argument("Unsupported local AssetService module: "+module);
  AssetStorageFactory factory(storage,storage_state);service=factory.create(config);return service;
 }
};
struct CacheBackend final:FlotsamCacheModuleBackend {
 std::shared_ptr<NativeAssetStorageBackend> storage;
 explicit CacheBackend(std::shared_ptr<NativeAssetStorageBackend> value):storage(std::move(value)){}
 std::shared_ptr<DefaultAssetLoader> load_loader(const std::string& name)override{return storage->load_loader(name);}
 void cleanup_elapsed(std::shared_ptr<FlotsamCacheLifecycle>)override{throw std::runtime_error("Native protected file-cache cleanup is not implemented");}
};
struct Scene final:AssetsModuleScene,AssetConnectorScene,FlotsamCacheScene {
 std::shared_ptr<SceneCapabilityEvents> source;
 std::shared_ptr<AssetService> registered;
 std::shared_ptr<framework::IAssetCache> cache;
 std::optional<std::string> name;
 framework::Uuid id;
 std::atomic<bool> stopping=false;
 Scene(std::shared_ptr<SceneCapabilityEvents> events,std::optional<std::string> region,framework::Uuid region_id):source(std::move(events)),name(std::move(region)),id(region_id){}
 void register_service(std::shared_ptr<AssetService> service)override{registered=std::move(service);}
 AssetConnectorCache asset_cache()override{return {cache,true};}
 std::optional<std::string> region_name()override{return name;}
 SceneCapabilityEvents& events()override{return *source;}
 bool shutting_down()const override{return stopping;}
 std::shared_ptr<server::AssetRetrievalService> asset_service()override{return registered;}
 std::shared_ptr<AssetsUserManagement> user_management()override{return {};}
 std::shared_ptr<AssetsExternalCaps> external_caps()override{return {};}
 void register_cache(std::shared_ptr<framework::IAssetCache> value)override{cache=std::move(value);}
 void unregister_cache(std::shared_ptr<framework::IAssetCache> value)override{if(cache==value)cache.reset();}
 std::shared_ptr<AssetService> request_asset_service()override{return registered;}
 framework::Uuid cache_region_id()override{return id;}
};
}
struct RegionAssetRuntime::State {
 std::shared_ptr<Backend> backend;
 std::shared_ptr<Scene> scene;
 std::shared_ptr<LocalAssetConnector> connector;
 std::shared_ptr<GetAssetsModule> module;
 std::shared_ptr<FlotsamCacheModule> cache_module;
 std::shared_ptr<CacheBackend> cache_backend;
 std::shared_ptr<AssetsModuleState> module_state=std::make_shared<AssetsModuleState>();
 bool stopped=false;
 void stop(){
  if(stopped)return;stopped=true;
  std::exception_ptr first;
  auto attempt=[&](auto action){try{action();}catch(...){if(!first)first=std::current_exception();}};
  if(scene)scene->stopping=true;
  if(module&&scene&&int32_t(module_state->scenes)>0)attempt([&]{module->remove_region(scene);});
  // Handler waits for connector callbacks: callback pool and database stay live
  // until the asset request workers have joined.
  auto worker=std::dynamic_pointer_cast<framework::ObjectJobEngine>(module_state->worker.load());
  if(module)attempt([&]{module->close();});
  if(worker)while(worker->active_threads()!=0)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::shared_ptr<framework::ObjectJobEngine> file_worker;
  if(cache_module)file_worker=std::dynamic_pointer_cast<framework::ObjectJobEngine>(cache_module->shared_state()->worker.load());
  if(cache_module&&scene)attempt([&]{cache_module->remove_region(scene);});
  if(cache_module)attempt([&]{cache_module->close();});
  if(file_worker)while(file_worker->active_threads()!=0)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if(connector&&scene)attempt([&]{connector->remove_region(scene);});
  if(connector)attempt([&]{connector->close();});
  if(backend)attempt([&]{backend->callbacks.dispose();});
  if(backend)while(backend->callbacks.active_threads()!=0)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if(backend&&backend->service&&backend->service->database)attempt([&]{backend->service->database->dispose();});
  module_state->handler=nullptr;module_state->service=nullptr;
  if(scene)scene->registered.reset();
  cache_module.reset();cache_backend.reset();module.reset();connector.reset();scene.reset();backend.reset();
  if(first)std::rethrow_exception(first);
 }
 ~State(){try{stop();}catch(...) {}}
};
RegionAssetRuntime::RegionAssetRuntime(const server::IniConfig& config,std::shared_ptr<SceneCapabilityEvents> events,
 std::optional<std::string> region_name,std::function<void(std::string)> log,framework::Uuid region_id):state_(std::make_unique<State>()) {
 if(!events)throw std::invalid_argument("Null region asset capability events");
 std::shared_ptr<framework::IAssetCache> cache;
 FlotsamCacheModule::Options cache_options;
 if(config.get("Modules","AssetCaching")=="FlotsamAssetCache"){
  if(config.boolean("AssetCache","FileCacheEnabled",true))throw std::invalid_argument("Native Flotsam file cache is not implemented; no alternate file format or silent memory fallback is used. FileCacheEnabled=false selects the original memory-only profile.");
  FlotsamMemoryCache::Options settings;settings.memory=config.boolean("AssetCache","MemoryCacheEnabled",false);settings.negative=config.boolean("AssetCache","NegativeCacheEnabled",true);settings.negative_milliseconds=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(account_integer(config.get("AssetCache","NegativeCacheTimeout","120")))*1000u);
  if(config.has_section("AssetCache")){auto text=config.get("AssetCache","MemoryCacheTimeout","0.016");if(!network::try_parse_invariant_real(text,settings.memory_seconds))throw std::invalid_argument("Invalid AssetCache.MemoryCacheTimeout");settings.memory_seconds*=3600.0;}
  settings.log_level=account_integer(config.get("AssetCache","LogLevel","0"));
  auto files=std::make_shared<FlotsamMemoryCache::FileOperations>();files->debug=[log](const auto& text){if(log)log(text);};files->warn=files->debug;
  cache=std::make_shared<FlotsamMemoryCache>(settings,FlotsamMemoryCache::Clock{},true,files);
  cache_options.enabled=true;cache_options.file=false;
  if(config.has_section("AssetService")){cache_options.loader=config.get("AssetService","DefaultAssetLoader","");cache_options.loader_arguments=config.get("AssetService","AssetLoaderArgs","");if(white(cache_options.loader_arguments))cache_options.loader=std::string{};}
 }
 state_->backend=std::make_shared<Backend>(std::move(log));
 state_->scene=std::make_shared<Scene>(std::move(events),std::move(region_name),region_id);
 if(cache){state_->cache_backend=std::make_shared<CacheBackend>(state_->backend->storage);state_->cache_module=std::make_shared<FlotsamCacheModule>(std::move(cache_options),std::move(cache),state_->cache_backend);state_->cache_module->post_initialise();state_->cache_module->add_region(state_->scene);}
 state_->connector=std::make_shared<LocalAssetConnector>(state_->backend);
 state_->connector->initialise(&config);
 state_->connector->add_region(state_->scene);
 if(!state_->scene->registered)throw std::invalid_argument("Region assets require configured LocalAssetServicesConnector and local AssetService");
 state_->connector->region_loaded(state_->scene);
 if(state_->cache_module)state_->cache_module->region_loaded(state_->scene);
 state_->module=std::make_shared<GetAssetsModule>(state_->backend,state_->module_state);
 state_->module->initialise(&config);
 state_->module->add_region(state_->scene);
 state_->module->region_loaded(state_->scene);
}
std::shared_ptr<framework::AssetBase> RegionAssetRuntime::get(const framework::Uuid& id){if(!state_||state_->stopped||!state_->scene||!state_->scene->registered)throw std::runtime_error("Region asset service stopped");return state_->scene->registered->get_asset(id.str());}
std::optional<std::string> RegionAssetRuntime::store(std::shared_ptr<framework::AssetBase> asset){if(!state_||state_->stopped||!state_->scene||!state_->scene->registered)throw std::runtime_error("Region asset service stopped");return state_->scene->registered->store(std::move(asset));}
RegionAssetRuntime::~RegionAssetRuntime()=default;
std::shared_ptr<framework::IAssetCache> RegionAssetRuntime::asset_cache()const{return state_&&state_->scene?state_->scene->cache:std::shared_ptr<framework::IAssetCache>{};}
void RegionAssetRuntime::stop(){state_->stop();}
}
