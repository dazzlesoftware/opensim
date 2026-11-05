// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_cache.hpp"
#include "opensim/services/asset_service.hpp"
#include "opensim/services/flotsam_expiring_entries.hpp"
#include <atomic>
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
class FlotsamMemoryCache final : public framework::IAssetCache, public AssetService {
public:
    using Asset = AssetService::Asset;
    struct Options {
        bool memory = false, negative = true;
        double memory_seconds = .016;
        int negative_milliseconds = 120000;
        bool file = false, update_access = false;
        int log_level = 0;
    };
    struct FileStatistics {
        std::uint64_t hits = 0, in_progress = 0;
    };
    struct FileOperations {
        std::function<FileStatistics()> statistics;
        std::function<void(const std::string&, const Asset&, bool)> cache;
        std::function<Asset(const std::string&)> read;
        std::function<bool(const std::string&)> check;
        std::function<void(const std::string&)> touch, expire;
        std::function<void()> clear;
        std::function<void(const std::string&)> debug, warn;
    };
    using Clock = std::function<double()>;
    struct Containers {
        std::atomic<std::shared_ptr<FlotsamKeyEntries>> negative;
        std::atomic<std::shared_ptr<FlotsamAssetEntries>> memory;
        std::function<std::shared_ptr<FlotsamKeyEntries>(std::int32_t)> make_key;
        std::function<std::shared_ptr<FlotsamAssetEntries>(std::int32_t)> make_memory;
        std::function<void(std::shared_ptr<FlotsamKeyEntries>)> publish_key;
        std::function<void(std::shared_ptr<FlotsamAssetEntries>)> publish_memory;
        std::function<Options()> live_options;
        std::function<void()> pump_retired;
    };
    explicit FlotsamMemoryCache(Options, Clock clock = {}, bool background = true,
                                std::shared_ptr<FileOperations> files = {});
    // Adopts exact initializer-created objects without reading a clock or allocating
    // replacement containers. Factories are used only by the data Clear operation.
    FlotsamMemoryCache(Options, std::shared_ptr<Containers>, std::shared_ptr<FileOperations> files = {});
    std::shared_ptr<Containers> containers() const {
        return containers_;
    }
    ~FlotsamMemoryCache();
    void cache(const std::shared_ptr<framework::AssetBase>&, bool replace = false) override;
    void cache_negative(const std::string&) override;
    bool get(const std::string&, std::shared_ptr<framework::AssetBase>&) override;
    std::shared_ptr<framework::AssetBase> get_cached(const std::string&) override;
    bool get_from_memory(const std::string&, std::shared_ptr<framework::AssetBase>&) override;
    bool check(const std::string&) override;
    void expire(const std::string&) override;
    void clear() override;
    Asset get_asset(const std::string&) override;
    Asset get_foreign(const std::string&, const std::optional<std::string>&, bool) override;
    std::shared_ptr<framework::AssetMetadata> get_metadata(const std::string&) override;
    framework::AssetBytes get_data(const std::string&) override;
    bool get_async(const std::string&, std::any, Retrieved) override;
    void get(const std::string&, const std::optional<std::string>&, bool, Complete) override;
    std::vector<bool> assets_exist(const std::vector<std::string>&) override;
    std::optional<std::string> store(Asset) override;
    bool update_content(const std::string&, framework::AssetBytes) override;
    bool erase(const std::string&) override;
    // Runs callbacks due at the supplied clock's current time; reads never purge.
    void pump();
    struct Statistics {
        std::uint64_t requests = 0, memory_hits = 0, weak_hits = 0;
    };
    Statistics statistics() const;
    struct Status {
        Statistics hits;
        FileStatistics files;
        std::int32_t weak_entries = 0, weak_alive = 0, memory_entries = 0, negative_entries = 0;
    };
    Status status() const;
    std::vector<std::string> hit_report() const;
    static std::vector<std::string> hit_report(const Status&, bool memory);
    void clear_memory();
    std::int32_t clear_negatives();
    Options settings() const {
        return containers_->live_options ? containers_->live_options() : options_;
    }

private:
    friend class FlotsamCacheCleaner;
    mutable std::mutex mutex_;
    Options options_;
    Statistics stats_;
    std::shared_ptr<FileOperations> files_;
    std::shared_ptr<Containers> containers_;
    std::unordered_map<std::string, std::weak_ptr<framework::AssetBase>> weak_;
    std::shared_ptr<FlotsamKeyEntries> negative_entries() const;
    std::shared_ptr<FlotsamAssetEntries> memory_entries() const;
    void update_memory(const std::string&, const Asset&);
    void lookup(const std::string&, Asset&, bool file);
    void update_weak(const std::string&, const Asset&);
    Asset weak_get(const std::string&);
    Asset memory_get(const std::string&);
    bool begin_get(const std::string&, Asset&);
};
}
