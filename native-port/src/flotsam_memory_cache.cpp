// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_memory_cache.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
namespace opensim::services {
namespace {
std::int32_t narrow(double n){return std::isfinite(n)&&n>=-2147483648.0&&n<2147483648.0?static_cast<std::int32_t>(n):INT32_MIN;}
std::int32_t wrap(std::int64_t n){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(n));}
std::int32_t deadline(double elapsed,std::int32_t duration,std::int32_t minimum){return duration>0?wrap(std::int64_t(narrow(elapsed))+std::max(duration,minimum)):INT32_MIN;}
}
FlotsamMemoryCache::FlotsamMemoryCache(Options options,Clock clock,bool background,std::shared_ptr<FileOperations> files):options_(options),clock_(clock?std::move(clock):Clock([]{return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();})),files_(std::move(files)),memory_check_(std::max(500,wrap(std::int64_t(narrow(options.memory_seconds))*500))),memory_start_(clock_()),negative_start_(clock_()){
 if((options_.file&&(!files_||!files_->cache||!files_->read||!files_->check||!files_->expire||!files_->clear))||(options_.update_access&&(!files_||!files_->touch)))throw std::invalid_argument("Incomplete Flotsam cache file operations");
 if(background)worker_=std::thread([this]{run();});
}
FlotsamMemoryCache::~FlotsamMemoryCache(){{std::lock_guard lock(mutex_);stopped_=true;changed_.notify_all();}if(worker_.joinable())worker_.join();}
void FlotsamMemoryCache::update_memory(const std::string& id,const Asset& asset){std::lock_guard lock(mutex_);auto now=clock_();memory_[id]={asset,deadline(now-memory_start_,narrow(options_.memory_seconds*1000),memory_check_)};if(!memory_due_)memory_due_=clock_()+memory_check_;changed_.notify_all();}
void FlotsamMemoryCache::update_weak(const std::string& id,const Asset& asset){std::lock_guard lock(mutex_);weak_[id]=asset;}
void FlotsamMemoryCache::cache(const Asset& asset,bool replace){if(!asset)return;update_weak(asset->id(),asset);if(options_.memory)update_memory(asset->id(),asset);if(options_.file)files_->cache(asset->id(),asset,replace);if(options_.negative){std::lock_guard lock(mutex_);negative_.erase(asset->id());if(negative_.empty())negative_due_.reset();changed_.notify_all();}}
void FlotsamMemoryCache::cache_negative(const std::string& id){std::lock_guard lock(mutex_);if(options_.negative){auto now=clock_();negative_[id]=deadline(now-negative_start_,options_.negative_milliseconds,2000);if(!negative_due_)negative_due_=clock_()+2000;changed_.notify_all();}}
FlotsamMemoryCache::Asset FlotsamMemoryCache::weak_get(const std::string& id){std::lock_guard lock(mutex_);auto it=weak_.find(id);if(it!=weak_.end())if(auto asset=it->second.lock()){++stats_.weak_hits;return asset;}return {};}
FlotsamMemoryCache::Asset FlotsamMemoryCache::memory_get(const std::string& id){std::lock_guard lock(mutex_);auto it=memory_.find(id);if(it!=memory_.end()){++stats_.memory_hits;return it->second.asset;}return {};}
void FlotsamMemoryCache::lookup(const std::string& id,Asset& asset,bool file){asset=weak_get(id);if(asset){if(options_.update_access)files_->touch(id);if(options_.memory)update_memory(id,asset);return;}if(options_.memory){asset=memory_get(id);if(asset){update_weak(id,asset);if(options_.update_access)files_->touch(id);return;}}if(file&&options_.file){asset=files_->read(id);if(asset){update_weak(id,asset);if(options_.memory)update_memory(id,asset);}}}
bool FlotsamMemoryCache::begin_get(const std::string& id,Asset& asset){asset.reset();std::lock_guard lock(mutex_);++stats_.requests;return id!=framework::Uuid{}.str()&&!negative_.contains(id);}
bool FlotsamMemoryCache::get(const std::string& id,Asset& asset){if(!begin_get(id,asset))return false;lookup(id,asset,true);return true;}
bool FlotsamMemoryCache::get_from_memory(const std::string& id,Asset& asset){if(!begin_get(id,asset))return false;lookup(id,asset,false);return true;}
FlotsamMemoryCache::Asset FlotsamMemoryCache::get_cached(const std::string& id){{std::lock_guard lock(mutex_);++stats_.requests;}Asset asset;lookup(id,asset,true);return asset;}
bool FlotsamMemoryCache::check(const std::string& id){if(weak_get(id))return true;if(options_.memory){std::lock_guard lock(mutex_);if(memory_.contains(id))return true;}return options_.file&&files_->check(id);}
void FlotsamMemoryCache::expire(const std::string& id){if(options_.log_level>=2&&files_&&files_->debug)files_->debug("[FLOTSAM ASSET CACHE]: Expiring Asset "+id);try{{std::lock_guard lock(mutex_);weak_.erase(id);if(options_.memory){memory_.erase(id);if(memory_.empty())memory_due_.reset();}if(options_.negative){negative_.erase(id);if(negative_.empty())negative_due_.reset();}changed_.notify_all();}if(options_.file)files_->expire(id);}catch(const std::exception& e){if(options_.log_level>=2&&files_&&files_->warn)files_->warn("[FLOTSAM ASSET CACHE]: Failed to expire cached file "+id+": "+e.what());}}
void FlotsamMemoryCache::clear(){if(options_.log_level>=2&&files_&&files_->debug)files_->debug("[FLOTSAM ASSET CACHE]: Clearing caches.");if(options_.file)files_->clear();std::lock_guard lock(mutex_);if(options_.memory){memory_.clear();memory_due_.reset();memory_start_=clock_();}if(options_.negative){negative_.clear();negative_due_.reset();negative_start_=clock_();}weak_.clear();changed_.notify_all();}
FlotsamMemoryCache::Asset FlotsamMemoryCache::get_asset(const std::string& id){Asset asset;get(id,asset);return asset;}
FlotsamMemoryCache::Asset FlotsamMemoryCache::get_foreign(const std::string&,const std::optional<std::string>&,bool){return {};}
std::shared_ptr<framework::AssetMetadata> FlotsamMemoryCache::get_metadata(const std::string& id){auto asset=get_asset(id);return asset?asset->metadata():nullptr;}
framework::AssetBytes FlotsamMemoryCache::get_data(const std::string& id){auto asset=get_asset(id);return asset?asset->data:nullptr;}
bool FlotsamMemoryCache::get_async(const std::string& id,std::any sender,Retrieved callback){Asset asset;if(!get(id,asset))return false;callback(id,std::move(sender),asset);return true;}
void FlotsamMemoryCache::get(const std::string&,const std::optional<std::string>&,bool,Complete){}
std::vector<bool> FlotsamMemoryCache::assets_exist(const std::vector<std::string>& ids){std::vector<bool> result;result.reserve(ids.size());for(auto& id:ids)result.push_back(check(id));return result;}
std::optional<std::string> FlotsamMemoryCache::store(Asset asset){if(!asset)throw std::invalid_argument("Null cache asset");if(asset->full_id()==framework::Uuid{})asset->full_id(framework::Uuid::random());cache(asset);return asset->id();}
bool FlotsamMemoryCache::update_content(const std::string& id,framework::AssetBytes data){Asset asset;if(!get(id,asset))return false;if(!asset)throw std::invalid_argument("Null cache asset");asset->data=std::move(data);cache(asset,true);return true;}
bool FlotsamMemoryCache::erase(const std::string& id){expire(id);return true;}
void FlotsamMemoryCache::purge_locked(){auto now=clock_();if(memory_due_&&*memory_due_<=now){auto relative=narrow(now-memory_start_);for(auto it=memory_.begin();it!=memory_.end();)if(it->second.deadline>0&&it->second.deadline<relative)it=memory_.erase(it);else ++it;if(memory_.empty())memory_due_.reset();else memory_due_=clock_()+memory_check_;}if(negative_due_&&*negative_due_<=now){auto relative=narrow(now-negative_start_);for(auto it=negative_.begin();it!=negative_.end();)if(it->second>0&&it->second<relative)it=negative_.erase(it);else ++it;if(negative_.empty())negative_due_.reset();else negative_due_=clock_()+2000;}}
void FlotsamMemoryCache::pump(){std::lock_guard lock(mutex_);purge_locked();}
FlotsamMemoryCache::Statistics FlotsamMemoryCache::statistics()const{std::lock_guard lock(mutex_);return stats_;}
void FlotsamMemoryCache::run(){std::unique_lock lock(mutex_);while(!stopped_){purge_locked();auto next=memory_due_;if(negative_due_&&(!next||*negative_due_<*next))next=negative_due_;if(!next){changed_.wait(lock);continue;}changed_.wait_for(lock,std::chrono::duration<double,std::milli>(std::max(1.0,*next-clock_())));}}
}
