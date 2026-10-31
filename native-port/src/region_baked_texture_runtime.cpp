// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_baked_texture_runtime.hpp"
#include "opensim/framework/text.hpp"
#include <condition_variable>
#include <thread>
#include <map>
#include <chrono>
namespace opensim::services {
struct RegionBakedTextureRuntime::State:std::enable_shared_from_this<State>{
 using Clock=std::chrono::steady_clock;
 struct Pending {std::shared_ptr<BakedTextureUpload> upload;std::weak_ptr<CapabilityRuntimeListener> listener;std::optional<Clock::time_point> deadline;};
 std::shared_ptr<SceneCapabilityEvents> events;std::shared_ptr<framework::IAssetCache> cache;std::string url;std::function<void(const std::string&)> log;
 mutable std::mutex mutex;std::condition_variable changed;std::map<std::string,Pending> pending;
 bool stopped=false,subscribed=false;std::thread timer_thread;
 SceneDelegateIdentity identity()const{return {this,"UploadBakedTexture.RegisterCaps"};}
 bool accepting()const{std::lock_guard lock(mutex);return !stopped;}
 void arm(const std::string& path,int milliseconds,std::weak_ptr<CapabilityRuntimeListener> listener){std::lock_guard lock(mutex);if(stopped)return;if(milliseconds==0){auto it=pending.find(path);if(it!=pending.end())it->second.deadline.reset();}else{auto& entry=pending[path];entry.listener=std::move(listener);entry.deadline=Clock::now()+std::chrono::milliseconds(milliseconds);}changed.notify_all();}
 void remove(const std::string& path){std::shared_ptr<CapabilityRuntimeListener> listener;std::shared_ptr<BakedTextureUpload> retained;{std::lock_guard lock(mutex);auto it=pending.find(path);if(it==pending.end())return;listener=it->second.listener.lock();retained=std::move(it->second.upload);pending.erase(it);changed.notify_all();}if(listener)listener->remove_simple(path);}
 void add(std::shared_ptr<BakedTextureUpload> upload,std::weak_ptr<CapabilityRuntimeListener> listener){
  if(!upload||!upload->path())throw std::invalid_argument("Null baked texture upload route");auto path=*upload->path();auto target=listener.lock();if(!target)throw std::runtime_error("Upload listener expired");bool expired=false;
  {std::lock_guard lock(mutex);if(stopped)throw std::runtime_error("Baked texture runtime stopped");auto& entry=pending[path];entry.listener=listener;entry.upload=upload;expired=entry.deadline&&*entry.deadline<=Clock::now();target->add_simple(upload);changed.notify_all();}
  if(expired)upload->expire();
 }
 void register_caps(framework::Uuid agent,const std::shared_ptr<Caps>& caps){
  if(!accepting())return;if(!caps)throw std::invalid_argument("Null baked texture Caps");
  if(url!="localhost"){if(!framework::trim_utf8(url).empty())caps->register_external("UploadBakedTexture",url);return;}
  std::weak_ptr<State> weak=shared_from_this();std::weak_ptr<CapabilityRuntimeListener> listener=caps->http_listener();BakedTextureCapsBackend backend;backend.cache=cache;
  backend.register_upload=[weak,listener](auto upload){auto state=weak.lock();if(!state)throw std::runtime_error("Baked texture runtime expired");state->add(std::move(upload),listener);};
  backend.remove_upload=[weak](const auto& path){if(auto state=weak.lock())state->remove(path);};
  backend.timer=[weak,listener](const auto& path,int delay){if(auto state=weak.lock())state->arm(path,delay,listener);};
  // Removal retires this route's timer entry; source Dispose is then idempotent.
  backend.dispose_timer=[](const auto&){};backend.log=log;
  auto origin=std::string(caps->ssl_caps()?"https://":"http://")+caps->host_name().value_or("")+":"+std::to_string(caps->port());
  caps->register_simple("UploadBakedTexture",std::make_shared<BakedTextureCaps>("/"+framework::Uuid::random().str(),agent,origin,std::move(backend)));
 }
 void timer_loop(){
  std::unique_lock lock(mutex);while(!stopped){std::optional<Clock::time_point> next;for(const auto& [path,entry]:pending)if(entry.upload&&entry.deadline&&(!next||*entry.deadline<*next))next=entry.deadline;
   if(!next){changed.wait(lock);continue;}if(Clock::now()<*next){changed.wait_until(lock,*next);continue;}
   std::vector<std::shared_ptr<BakedTextureUpload>> expired;auto now=Clock::now();for(auto& [path,entry]:pending)if(entry.upload&&entry.deadline&&*entry.deadline<=now){entry.deadline.reset();expired.push_back(entry.upload);}
   lock.unlock();for(const auto& upload:expired)try{upload->expire();}catch(...){}lock.lock();
  }
 }
 void stop(){std::vector<std::shared_ptr<BakedTextureUpload>> uploads;{std::lock_guard lock(mutex);if(stopped)return;stopped=true;for(const auto& [path,entry]:pending)if(entry.upload)uploads.push_back(entry.upload);changed.notify_all();}
  if(subscribed){events->on_register_caps.remove(identity());subscribed=false;}if(timer_thread.joinable())timer_thread.join();for(const auto& upload:uploads)upload->expire();
  std::lock_guard lock(mutex);pending.clear();
 }
};
RegionBakedTextureRuntime::RegionBakedTextureRuntime(std::shared_ptr<SceneCapabilityEvents> events,std::shared_ptr<framework::IAssetCache> cache,std::string url,std::function<void(const std::string&)> log):state_(std::make_shared<State>()){
 if(!events)throw std::invalid_argument("Null baked texture scene events");state_->events=std::move(events);state_->cache=std::move(cache);state_->url=std::move(url);state_->log=std::move(log);if(!state_->cache)return;
 std::weak_ptr<State> weak=state_;state_->events->on_register_caps.add({state_->identity(),[weak](auto agent,auto caps){if(auto state=weak.lock())state->register_caps(agent,caps);}});state_->subscribed=true;
 try{auto state=state_;state_->timer_thread=std::thread([state]{state->timer_loop();});}catch(...){state_->events->on_register_caps.remove(state_->identity());state_->subscribed=false;throw;}
}
RegionBakedTextureRuntime::~RegionBakedTextureRuntime(){try{stop();}catch(...) {}}
void RegionBakedTextureRuntime::stop(){state_->stop();}
std::size_t RegionBakedTextureRuntime::pending_uploads()const{std::lock_guard lock(state_->mutex);std::size_t count=0;for(const auto& [path,entry]:state_->pending)if(entry.upload)++count;return count;}
}
