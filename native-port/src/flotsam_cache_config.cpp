// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_cache_config.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace opensim::services {
namespace {
bool white(const std::optional<std::string> &value) {
    if (!value)
        return true;
    auto text = framework::utf16_replacing(*value);
    return std::all_of(text.begin(), text.end(), framework::unicode_whitespace);
}
std::int32_t narrow(double value) {
    return std::isfinite(value) && value >= -2147483648.0 && value < 2147483648.0
               ? static_cast<std::int32_t>(value)
               : INT32_MIN;
}
std::int32_t multiply(std::int32_t value, std::uint32_t factor) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) * factor);
}
}
FlotsamCacheConfig::FlotsamCacheConfig(std::shared_ptr<FlotsamInstanceConfig> instance,
                                       std::shared_ptr<FlotsamStaticConfig> shared,
                                       std::shared_ptr<FlotsamConfigBackend> backend)
    : instance_(std::move(instance)), shared_(std::move(shared)), backend_(std::move(backend)) {
    if (!instance_ || !shared_ || !backend_ || !backend_->full_path || !backend_->hours_to_ticks ||
        !backend_->total_hours || !backend_->make_key_cache || !backend_->make_memory_cache ||
        !backend_->info || !backend_->debug || !backend_->register_command)
        throw std::invalid_argument("Incomplete Flotsam initializer dependencies");
}
void FlotsamCacheConfig::initialise(std::shared_ptr<FlotsamConfigSource> source) {
    if (!source)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    auto modules = source->section("Modules");
    if (!modules)
        return;
    if (modules->text("AssetCaching", std::string{}) != std::optional<std::string>(name))
        return;
    auto &state = *instance_;
    auto &common = *shared_;
    auto &b = *backend_;
    state.negative_entries = b.make_key_cache(2000);
    state.enabled = true;
    b.info(std::string("[FLOTSAM ASSET CACHE]: ") + name + " enabled");
    auto assets = source->section("AssetCache");
    if (!assets)
        b.debug(
            "[FLOTSAM ASSET CACHE]: AssetCache section missing from config (not copied config-include/FlotsamCache.ini.example?  Using defaults.");
    else {
        state.file = assets->boolean("FileCacheEnabled", state.file);
        state.directory = assets->text("CacheDirectory", state.directory);
        state.directory = b.full_path(state.directory);
        state.memory = assets->boolean("MemoryCacheEnabled", state.memory);
        state.memory_seconds = assets->real("MemoryCacheTimeout", state.memory_seconds);
        state.memory_seconds *= 3600.0;
        state.negative = assets->boolean("NegativeCacheEnabled", state.negative);
        state.negative_milliseconds = assets->integer("NegativeCacheTimeout", state.negative_milliseconds);
        common.update_access = assets->boolean("UpdateFileTimeOnCacheHit", common.update_access);
        common.update_access &= state.file;
        state.log_level = assets->integer("LogLevel", state.log_level);
        state.hit_rate_display = static_cast<std::uint64_t>(
            assets->long_integer("HitRateDisplay", std::bit_cast<std::int64_t>(state.hit_rate_display)));
        state.expiration_ticks = b.hours_to_ticks(assets->real("FileCacheTimeout", 48.0));
        state.cleanup_ticks =
            b.hours_to_ticks(assets->real("FileCleanupTimer", b.total_hours(state.cleanup_ticks)));
        common.tiers = assets->integer("CacheDirectoryTiers", common.tiers);
        common.tier_length = assets->integer("CacheDirectoryTierLength", common.tier_length);
        common.warn_at = assets->integer("CacheWarnAt", common.warn_at);
    }
    if (common.update_access)
        common.access_entries = b.make_key_cache(300000);
    if (state.memory)
        state.memory_entries = b.make_memory_cache(multiply(narrow(state.memory_seconds), 500));
    b.info("[FLOTSAM ASSET CACHE]: Cache Directory " + state.directory.value_or(""));
    common.tiers = std::clamp(common.tiers, 1, 3);
    common.tier_length = std::clamp(common.tier_length, 1, 4);
    state.negative_milliseconds = multiply(state.negative_milliseconds, 1000);
    auto service = source->section("AssetService");
    if (service) {
        state.loader = service->text("DefaultAssetLoader", std::string{});
        state.loader_arguments = service->text("AssetLoaderArgs", std::string{});
        if (white(state.loader_arguments))
            state.loader = std::string{};
    }
    auto add = [&](const char *command, const char *usage, const char *description) {
        b.register_command({"Assets", true, command, usage, description});
    };
    add("fcache status", "fcache status", "Display cache status");
    add("fcache clear", "fcache clear [file] [memory]",
        "Remove all assets in the cache.  If file or memory is specified then only this cache is cleared.");
    add("fcache clearnegatives", "fcache clearnegatives",
        "Remove cache of assets previously not found in services.");
    add("fcache assets", "fcache assets", "Attempt a deep scan and cache of all assets in all scenes");
    add("fcache expire", "fcache expire <datetime(mm/dd/YYYY)>",
        "Purge cached assets older than the specified date/time");
    if (!white(state.loader)) {
        add("fcache cachedefaultassets", "fcache cachedefaultassets",
            "loads local default assets to cache. This may override grid ones. use with care");
        add("fcache deletedefaultassets", "fcache deletedefaultassets",
            "deletes default local assets from cache so they can be refreshed from grid. use with care");
    }
}
}
