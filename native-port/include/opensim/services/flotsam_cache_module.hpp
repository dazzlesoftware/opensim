// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/asset_storage_factory.hpp"
#include "opensim/framework/asset_cache.hpp"
#include "opensim/framework/object_job_engine.hpp"
#include "opensim/services/flotsam_file_store.hpp"
#include <atomic>
#include <mutex>
#include <unordered_set>
namespace opensim::services {
class FlotsamCleanupTimer {
public:
 virtual ~FlotsamCleanupTimer()=default;
 virtual void subscribe(std::function<void()>)=0;
 virtual void start()=0;
 virtual void stop()=0;
 virtual void close()=0;
};
// A source System.Timers.Timer with AutoReset=false, as used by this module.
std::shared_ptr<FlotsamCleanupTimer> native_flotsam_cleanup_timer(double milliseconds);
class FlotsamCacheScene {
public:
 virtual ~FlotsamCacheScene()=default;
 virtual void register_cache(std::shared_ptr<framework::IAssetCache>)=0;
 virtual void unregister_cache(std::shared_ptr<framework::IAssetCache>)=0;
 virtual std::shared_ptr<AssetService> request_asset_service()=0;
 virtual framework::Uuid cache_region_id()=0;
};
struct FlotsamCacheSharedState {
 std::mutex defaults_mutex;
 std::atomic<std::shared_ptr<framework::ObjectJobQueue>> worker;
 std::atomic<std::shared_ptr<std::unordered_set<std::string>>> default_assets{std::make_shared<std::unordered_set<std::string>>()};
 std::shared_ptr<FlotsamFileState> files=std::make_shared<FlotsamFileState>();
};
struct FlotsamCacheLifecycle {
 std::recursive_mutex timer_mutex;
 std::vector<std::shared_ptr<FlotsamCacheScene>> scenes;
 std::shared_ptr<AssetService> service;
 std::shared_ptr<FlotsamCleanupTimer> timer;
 std::atomic<bool> timer_running=false,cleanup_running=false;
};
class FlotsamCacheModuleBackend {
public:
 virtual ~FlotsamCacheModuleBackend()=default;
 virtual std::shared_ptr<FlotsamCleanupTimer> make_timer(double interval);
 virtual std::shared_ptr<framework::ObjectJobQueue> make_worker();
 virtual std::shared_ptr<DefaultAssetLoader> load_loader(const std::string&)=0;
 // Protected file scanning/cleanup is a separate required implementation.
 virtual void cleanup_elapsed(std::shared_ptr<FlotsamCacheLifecycle>)=0;
};
class FlotsamCacheModule {
public:
 struct Options {
  bool enabled=false,file=true;
  std::int64_t expiration_ticks=48ll*3600*10000000,cleanup_ticks=3600ll*10000000;
  std::optional<std::string> loader;
  std::string loader_arguments;
 };
 FlotsamCacheModule(Options,std::shared_ptr<framework::IAssetCache>,std::shared_ptr<FlotsamCacheModuleBackend>,std::shared_ptr<FlotsamCacheSharedState> shared={});
 static constexpr std::string_view name="FlotsamAssetCache";
 std::nullptr_t replaceable_interface()const{return nullptr;}
 void post_initialise(){}
 void add_region(std::shared_ptr<FlotsamCacheScene>);
 void remove_region(std::shared_ptr<FlotsamCacheScene>);
 void region_loaded(std::shared_ptr<FlotsamCacheScene>);
 void close();
 std::shared_ptr<FlotsamCacheLifecycle> lifecycle()const{return life_;}
 std::shared_ptr<FlotsamCacheSharedState> shared_state()const{return shared_;}
private:
 Options options_;std::shared_ptr<framework::IAssetCache> cache_;std::shared_ptr<FlotsamCacheModuleBackend> backend_;
 std::shared_ptr<FlotsamCacheLifecycle> life_=std::make_shared<FlotsamCacheLifecycle>();std::shared_ptr<FlotsamCacheSharedState> shared_;
 void stop_workers();
};
// Resolves the source shared worker on each availability/enqueue operation.
// Backends hold the state weakly; queued jobs cannot own their executor.
std::shared_ptr<FlotsamFileBackend> native_flotsam_shared_file_backend(std::shared_ptr<FlotsamCacheSharedState>,std::function<void(const std::string&)> warn={});
}
