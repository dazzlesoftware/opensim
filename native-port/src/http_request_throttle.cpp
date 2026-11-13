// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/http_request_throttle.hpp"

namespace opensim::script {

HttpRequestThrottle::HttpRequestThrottle(HttpRequestThrottleSettings settings) : settings_(settings) {}

bool HttpRequestThrottle::Bucket::consume(double now_seconds, float rate, float burst) {
    const auto elapsed = now_seconds - last_time;
    last_time = now_seconds;
    credit += static_cast<float>(rate * elapsed);
    if (credit > burst) {
        credit = burst - 1.f;
        return true;
    }
    if (credit > 0.f) {
        --credit;
        return true;
    }
    return false;
}

bool HttpRequestThrottle::allow(std::uint32_t prim, framework::Uuid owner, double now_seconds) {
    std::lock_guard lock(mutex_);
    const Bucket initial{now_seconds, settings_.prim_burst - 1.f};
    const auto [prim_entry, new_prim] = prims_.try_emplace(prim, initial);
    if (!new_prim && !prim_entry->second.consume(now_seconds, settings_.prim_rate, settings_.prim_burst))
        return false;

    // The managed module seeds new owner buckets with the prim burst.
    const auto [owner_entry, new_owner] = owners_.try_emplace(owner, initial);
    return new_owner || owner_entry->second.consume(now_seconds, settings_.owner_rate, settings_.owner_burst);
}

void HttpRequestThrottle::stop(std::uint32_t prim, double now_seconds) {
    std::lock_guard lock(mutex_);
    const auto entry = prims_.find(prim);
    if (entry == prims_.end())
        return;
    const auto& [last_time, credit] = entry->second;
    // StopHttpRequest uses the owner refill rate for this prim's cleanup check.
    if (credit + settings_.owner_rate * (now_seconds - last_time) >= settings_.prim_burst)
        prims_.erase(entry);
}

void HttpRequestThrottle::clear() {
    std::lock_guard lock(mutex_);
    prims_.clear();
    owners_.clear();
}

}
