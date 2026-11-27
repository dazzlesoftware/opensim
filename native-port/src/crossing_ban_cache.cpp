// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/crossing_ban_cache.hpp"
#include "opensim/framework/expiring_cache.hpp"
#include <mutex>
#include <map>

namespace opensim::scene {
using framework::Uuid;

struct CrossingBanCache::State {
    struct Entries {
        std::mutex mutex;
        std::map<Uuid, double> agents;
    };

    using Regions = framework::ExpiringCacheOS<std::uint64_t, std::shared_ptr<Entries>>;
    std::function<double()> seconds;
    std::unique_ptr<Regions> regions;

    explicit State(framework::ExpiringCacheBackend backend)
        : seconds([clock = backend.timestamp] { return clock() * .001; }),
          regions(std::make_unique<Regions>(15000, std::move(backend))) {}

    Regions &cache() {
        if (!regions)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        return *regions;
    }
};

CrossingBanCache::CrossingBanCache() : CrossingBanCache(framework::native_expiring_cache_backend()) {}

CrossingBanCache::CrossingBanCache(framework::ExpiringCacheBackend backend)
    : state_(std::make_unique<State>(std::move(backend))) {}

CrossingBanCache::~CrossingBanCache() = default;

bool CrossingBanCache::if_banned(std::uint64_t region, Uuid agent) {
    std::shared_ptr<State::Entries> entries;
    if (state_->cache().try_get_value(region, entries)) {
        std::lock_guard lock(entries->mutex);
        if (const auto found = entries->agents.find(agent); found != entries->agents.end()) {
            // Preserve BannedRegionCache.IfBanned's original comparison: future
            // and equal entries are removed; past timestamps return true.
            if (found->second < state_->seconds())
                return true;
            entries->agents.erase(found);
        }
    }
    return false;
}

void CrossingBanCache::add(std::uint64_t region, Uuid agent, double seconds) {
    std::shared_ptr<State::Entries> entries;
    if (state_->cache().try_get_value(region, entries)) {
        std::lock_guard lock(entries->mutex);
        entries->agents[agent] = state_->seconds() + seconds;
        state_->cache().add_or_update(region, entries, seconds);
    } else {
        entries = std::make_shared<State::Entries>();
        entries->agents.emplace(agent, state_->seconds() + seconds);
        state_->cache().add_or_update(region, entries, seconds);
    }
}

void CrossingBanCache::remove(std::uint64_t region, Uuid agent) {
    std::shared_ptr<State::Entries> entries;
    if (state_->cache().try_get_value(region, entries)) {
        std::lock_guard lock(entries->mutex);
        entries->agents.erase(agent);
    }
}

void CrossingBanCache::dispose() {
    if (state_->regions) {
        state_->regions->dispose();
        state_->regions.reset();
    }
}
} // namespace opensim::scene
