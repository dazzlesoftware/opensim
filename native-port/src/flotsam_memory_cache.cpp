// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_memory_cache.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <charconv>
#include <array>
namespace opensim::services {
namespace {
std::int32_t narrow(double n) {
    return std::isfinite(n) && n >= -2147483648.0 && n < 2147483648.0 ? static_cast<std::int32_t>(n)
                                                                      : INT32_MIN;
}
std::int32_t wrap(std::int64_t n) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(n));
}
}
namespace {
FlotsamMemoryCache::Clock shared_clock(FlotsamMemoryCache::Clock clock) {
    auto provider =
        std::make_shared<FlotsamMemoryCache::Clock>(clock ? std::move(clock) : FlotsamMemoryCache::Clock([] {
            return std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }));
    return [provider] { return (*provider)(); };
}
std::shared_ptr<FlotsamMemoryCache::Containers> default_containers(const FlotsamMemoryCache::Options& options,
                                                                   FlotsamMemoryCache::Clock clock,
                                                                   bool background) {
    auto state = std::make_shared<FlotsamMemoryCache::Containers>();
    auto time = shared_clock(std::move(clock));
    state->make_key = [time, background](auto period) {
        return std::make_shared<FlotsamKeyEntries>(period, time, background);
    };
    state->make_memory = [time, background](auto period) {
        return std::make_shared<FlotsamAssetEntries>(period, time, background);
    };
    // Source field initialization/Initialise creates negative before optional memory.
    state->negative = state->make_key(2000);
    if (options.memory)
        state->memory = state->make_memory(wrap(std::int64_t(narrow(options.memory_seconds)) * 500));
    return state;
}
}
FlotsamMemoryCache::FlotsamMemoryCache(Options options, Clock clock, bool background,
                                       std::shared_ptr<FileOperations> files)
    : FlotsamMemoryCache(options, default_containers(options, std::move(clock), background),
                         std::move(files)) {
}
FlotsamMemoryCache::FlotsamMemoryCache(Options options, std::shared_ptr<Containers> entries,
                                       std::shared_ptr<FileOperations> files)
    : options_(options), files_(std::move(files)), containers_(std::move(entries)) {
    if (!containers_ || !containers_->negative.load() || !containers_->make_key || !containers_->make_memory)
        throw std::invalid_argument("Incomplete Flotsam cache containers");
    if ((options.file && (!files_ || !files_->cache || !files_->read || !files_->check || !files_->expire ||
                          !files_->clear)) ||
        (options.update_access && (!files_ || !files_->touch)))
        throw std::invalid_argument("Incomplete Flotsam cache file operations");
    if (options.memory && !containers_->memory.load())
        throw std::invalid_argument("Missing initialized memory container");
}
FlotsamMemoryCache::~FlotsamMemoryCache() = default;
std::shared_ptr<FlotsamKeyEntries> FlotsamMemoryCache::negative_entries() const {
    auto entries = containers_->negative.load();
    if (!entries)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return entries;
}
std::shared_ptr<FlotsamAssetEntries> FlotsamMemoryCache::memory_entries() const {
    auto entries = containers_->memory.load();
    if (!entries)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    return entries;
}
void FlotsamMemoryCache::update_memory(const std::string& id, const Asset& asset) {
    memory_entries()->add(id, asset, narrow(settings().memory_seconds * 1000));
}
void FlotsamMemoryCache::update_weak(const std::string& id, const Asset& asset) {
    std::lock_guard lock(mutex_);
    weak_[id] = asset;
}
void FlotsamMemoryCache::cache(const Asset& asset, bool replace) {
    if (!asset)
        return;
    update_weak(asset->id(), asset);
    if (settings().memory)
        update_memory(asset->id(), asset);
    if (settings().file)
        files_->cache(asset->id(), asset, replace);
    if (settings().negative)
        negative_entries()->remove(asset->id());
}
void FlotsamMemoryCache::cache_negative(const std::string& id) {
    if (settings().negative)
        negative_entries()->add(id, settings().negative_milliseconds);
}
FlotsamMemoryCache::Asset FlotsamMemoryCache::weak_get(const std::string& id) {
    std::lock_guard lock(mutex_);
    auto it = weak_.find(id);
    if (it != weak_.end())
        if (auto asset = it->second.lock()) {
            ++stats_.weak_hits;
            return asset;
        }
    return {};
}
FlotsamMemoryCache::Asset FlotsamMemoryCache::memory_get(const std::string& id) {
    Asset asset;
    if (memory_entries()->try_get(id, asset)) {
        std::lock_guard lock(mutex_);
        ++stats_.memory_hits;
    }
    return asset;
}
void FlotsamMemoryCache::lookup(const std::string& id, Asset& asset, bool file) {
    asset = weak_get(id);
    if (asset) {
        if (settings().update_access)
            files_->touch(id);
        if (settings().memory)
            update_memory(id, asset);
        return;
    }
    if (settings().memory) {
        asset = memory_get(id);
        if (asset) {
            update_weak(id, asset);
            if (settings().update_access)
                files_->touch(id);
            return;
        }
    }
    if (file && settings().file) {
        asset = files_->read(id);
        if (asset) {
            update_weak(id, asset);
            if (settings().memory)
                update_memory(id, asset);
        }
    }
}
bool FlotsamMemoryCache::begin_get(const std::string& id, Asset& asset) {
    asset.reset();
    {
        std::lock_guard lock(mutex_);
        ++stats_.requests;
    }
    return id != framework::Uuid{}.str() && !negative_entries()->contains(id);
}
bool FlotsamMemoryCache::get(const std::string& id, Asset& asset) {
    if (!begin_get(id, asset))
        return false;
    lookup(id, asset, true);
    return true;
}
bool FlotsamMemoryCache::get_from_memory(const std::string& id, Asset& asset) {
    if (!begin_get(id, asset))
        return false;
    lookup(id, asset, false);
    return true;
}
FlotsamMemoryCache::Asset FlotsamMemoryCache::get_cached(const std::string& id) {
    {
        std::lock_guard lock(mutex_);
        ++stats_.requests;
    }
    Asset asset;
    lookup(id, asset, true);
    return asset;
}
bool FlotsamMemoryCache::check(const std::string& id) {
    if (weak_get(id))
        return true;
    if (settings().memory) {
        Asset ignored;
        if (memory_entries()->try_get(id, ignored))
            return true;
    }
    return settings().file && files_->check(id);
}
void FlotsamMemoryCache::expire(const std::string& id) {
    if (settings().log_level >= 2 && files_ && files_->debug)
        files_->debug("[FLOTSAM ASSET CACHE]: Expiring Asset " + id);
    try {
        {
            std::lock_guard lock(mutex_);
            weak_.erase(id);
        }
        if (settings().memory)
            memory_entries()->remove(id);
        if (settings().negative)
            negative_entries()->remove(id);
        if (settings().file)
            files_->expire(id);
    } catch (const std::exception& e) {
        if (settings().log_level >= 2 && files_ && files_->warn)
            files_->warn("[FLOTSAM ASSET CACHE]: Failed to expire cached file " + id + ": " + e.what());
    }
}
void FlotsamMemoryCache::clear() {
    if (settings().log_level >= 2 && files_ && files_->debug)
        files_->debug("[FLOTSAM ASSET CACHE]: Clearing caches.");
    if (settings().file)
        files_->clear();
    if (settings().memory) {
        auto old = memory_entries();
        old->dispose();
        auto next = containers_->make_memory(wrap(std::int64_t(narrow(settings().memory_seconds)) * 500));
        containers_->memory = next;
        if (containers_->publish_memory)
            containers_->publish_memory(next);
    }
    if (settings().negative) {
        auto old = negative_entries();
        old->dispose();
        auto next = containers_->make_key(2000);
        containers_->negative = next;
        if (containers_->publish_key)
            containers_->publish_key(next);
    }
    std::lock_guard lock(mutex_);
    weak_.clear();
}
FlotsamMemoryCache::Asset FlotsamMemoryCache::get_asset(const std::string& id) {
    Asset asset;
    get(id, asset);
    return asset;
}
FlotsamMemoryCache::Asset FlotsamMemoryCache::get_foreign(const std::string&,
                                                          const std::optional<std::string>&, bool) {
    return {};
}
std::shared_ptr<framework::AssetMetadata> FlotsamMemoryCache::get_metadata(const std::string& id) {
    auto asset = get_asset(id);
    return asset ? asset->metadata() : nullptr;
}
framework::AssetBytes FlotsamMemoryCache::get_data(const std::string& id) {
    auto asset = get_asset(id);
    return asset ? asset->data : nullptr;
}
bool FlotsamMemoryCache::get_async(const std::string& id, std::any sender, Retrieved callback) {
    Asset asset;
    if (!get(id, asset))
        return false;
    callback(id, std::move(sender), asset);
    return true;
}
void FlotsamMemoryCache::get(const std::string&, const std::optional<std::string>&, bool, Complete) {
}
std::vector<bool> FlotsamMemoryCache::assets_exist(const std::vector<std::string>& ids) {
    std::vector<bool> result;
    result.reserve(ids.size());
    for (auto& id : ids)
        result.push_back(check(id));
    return result;
}
std::optional<std::string> FlotsamMemoryCache::store(Asset asset) {
    if (!asset)
        throw std::invalid_argument("Null cache asset");
    if (asset->full_id() == framework::Uuid{})
        asset->full_id(framework::Uuid::random());
    cache(asset);
    return asset->id();
}
bool FlotsamMemoryCache::update_content(const std::string& id, framework::AssetBytes data) {
    Asset asset;
    if (!get(id, asset))
        return false;
    if (!asset)
        throw std::invalid_argument("Null cache asset");
    asset->data = std::move(data);
    cache(asset, true);
    return true;
}
bool FlotsamMemoryCache::erase(const std::string& id) {
    expire(id);
    return true;
}
void FlotsamMemoryCache::pump() {
    if (containers_->pump_retired)
        containers_->pump_retired();
    if (auto memory = containers_->memory.load())
        memory->pump();
    if (auto negative = containers_->negative.load())
        negative->pump();
}
FlotsamMemoryCache::Statistics FlotsamMemoryCache::statistics() const {
    std::lock_guard lock(mutex_);
    return stats_;
}
namespace {
// CLR custom numeric formatting uses fifteen significant decimal digits for
// Double, then decimal midpoint rounding away from zero for the 0.00 pattern.
std::string percent(double value) {
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return value < 0 ? "-Infinity" : "Infinity";
    std::array<char, 128> buffer{};
    auto converted =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::scientific, 14);
    if (converted.ec != std::errc{})
        throw std::runtime_error("Cache rate formatting failed");
    std::string text(buffer.data(), converted.ptr);
    auto e = text.find('e');
    int exponent = std::stoi(text.substr(e + 1));
    std::string digits = text.substr(0, e);
    digits.erase(digits.find('.'), 1);
    int decimal = exponent + 1;
    if (decimal < 0) {
        digits = std::string(static_cast<std::size_t>(-decimal), '0') + digits;
        decimal = 0;
    }
    if (static_cast<int>(digits.size()) < decimal + 3)
        digits.resize(decimal + 3, '0');
    auto keep = static_cast<std::size_t>(decimal + 2);
    bool up = digits[keep] >= '5';
    digits.resize(keep);
    if (up) {
        auto i = digits.size();
        while (i && digits[i - 1] == '9') {
            digits[--i] = '0';
        }
        if (i)
            ++digits[i - 1];
        else {
            digits.insert(digits.begin(), '1');
            ++decimal;
        }
    }
    if (decimal == 0) {
        digits.insert(digits.begin(), '0');
        ++decimal;
    }
    digits.insert(static_cast<std::size_t>(decimal), 1, '.');
    return digits;
}
}
FlotsamMemoryCache::Status FlotsamMemoryCache::status() const {
    Status result;
    {
        std::lock_guard lock(mutex_);
        result.hits = stats_;
        result.weak_entries = static_cast<std::int32_t>(weak_.size());
        for (const auto& [id, asset] : weak_)
            if (!asset.expired())
                ++result.weak_alive;
    }
    if (auto memory = containers_->memory.load())
        result.memory_entries = memory->count();
    if (auto negative = containers_->negative.load())
        result.negative_entries = negative->count();
    if (files_ && files_->statistics)
        result.files = files_->statistics();
    return result;
}
std::vector<std::string> FlotsamMemoryCache::hit_report() const {
    return hit_report(status(), settings().memory);
}
std::vector<std::string> FlotsamMemoryCache::hit_report(const Status& s, bool memory) {
    double inverse = 100.0 / static_cast<double>(s.hits.requests);
    double weak = static_cast<double>(s.hits.weak_hits) * inverse;
    double disk = static_cast<double>(s.files.hits) * inverse;
    double total = weak + disk;
    std::vector<std::string> out = {"Total requests: " + std::to_string(s.hits.requests),
                                    "unCollected Hit Rate: " + percent(weak) + "% (" +
                                        std::to_string(s.weak_entries) + " entries " +
                                        std::to_string(s.weak_alive) + " alive)",
                                    "File Hit Rate: " + percent(disk) + "%"};
    if (memory) {
        double rate = static_cast<double>(s.hits.memory_hits) * inverse;
        out.push_back("Memory Hit Rate: " + percent(rate) + "%");
        total += rate;
    }
    out.push_back("Total Hit Rate: " + percent(total) + "%");
    out.push_back("Requests overlap during file writing: " + std::to_string(s.files.in_progress));
    return out;
}
void FlotsamMemoryCache::clear_memory() {
    if (settings().memory)
        memory_entries()->clear();
}
std::int32_t FlotsamMemoryCache::clear_negatives() {
    auto entries = negative_entries();
    auto count = entries->count();
    if (settings().negative)
        entries->clear();
    return count;
}
}
