// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_cache.hpp"
#include "opensim/services/asset_service.hpp"
#include <cstdint>
#include <optional>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_map>
namespace opensim::services {
// Composes the original weak/memory/negative paths with optional file operations.
// Module lifecycle, scene scanning, commands and production profile selection
// remain the responsibility of the module host.
class FlotsamMemoryCache final:public framework::IAssetCache,public AssetService {
public:
 using Asset=AssetService::Asset;
 struct Options {bool memory=false,negative=true;double memory_seconds=.016;int negative_milliseconds=120000;bool file=false,update_access=false;int log_level=0;};
 struct FileOperations {
  std::function<void(const std::string&,const Asset&,bool)> cache;
  std::function<Asset(const std::string&)> read;
  std::function<bool(const std::string&)> check;
  std::function<void(const std::string&)> touch,expire;
  std::function<void()> clear;
  std::function<void(const std::string&)> debug,warn;
 };
 using Clock=std::function<double()>;
 explicit FlotsamMemoryCache(Options,Clock clock={},bool background=true,std::shared_ptr<FileOperations> files={});
 ~FlotsamMemoryCache();
 void cache(const std::shared_ptr<framework::AssetBase>&,bool replace=false)override;
 void cache_negative(const std::string&)override;
 bool get(const std::string&,std::shared_ptr<framework::AssetBase>&)override;
 std::shared_ptr<framework::AssetBase> get_cached(const std::string&)override;
 bool get_from_memory(const std::string&,std::shared_ptr<framework::AssetBase>&)override;
 bool check(const std::string&)override;
 void expire(const std::string&)override;void clear()override;
 Asset get_asset(const std::string&)override;
 Asset get_foreign(const std::string&,const std::optional<std::string>&,bool)override;
 std::shared_ptr<framework::AssetMetadata> get_metadata(const std::string&)override;
 framework::AssetBytes get_data(const std::string&)override;
 bool get_async(const std::string&,std::any,Retrieved)override;
 void get(const std::string&,const std::optional<std::string>&,bool,Complete)override;
 std::vector<bool> assets_exist(const std::vector<std::string>&)override;
 std::optional<std::string> store(Asset)override;
 bool update_content(const std::string&,framework::AssetBytes)override;
 bool erase(const std::string&)override;
 // Runs callbacks due at the supplied clock's current time; reads never purge.
 void pump();
 struct Statistics {std::uint64_t requests=0,memory_hits=0,weak_hits=0;};
 Statistics statistics()const;
 Options settings()const{return options_;}
private:
 friend class FlotsamCacheCleaner;
 struct Entry {Asset asset;std::int32_t deadline;};
 mutable std::mutex mutex_;std::condition_variable changed_;std::thread worker_;bool stopped_=false;
 Options options_;Clock clock_;Statistics stats_;std::shared_ptr<FileOperations> files_;
 std::int32_t memory_check_;double memory_start_,negative_start_;
 std::optional<double> memory_due_,negative_due_;
 std::unordered_map<std::string,std::weak_ptr<framework::AssetBase>> weak_;
 std::unordered_map<std::string,Entry> memory_;
 std::unordered_map<std::string,std::int32_t> negative_;
 void update_memory(const std::string&,const Asset&);
 void lookup(const std::string&,Asset&,bool file);
 void update_weak(const std::string&,const Asset&);
 Asset weak_get(const std::string&);Asset memory_get(const std::string&);
 bool begin_get(const std::string&,Asset&);
 void purge_locked();void run();
};
}
