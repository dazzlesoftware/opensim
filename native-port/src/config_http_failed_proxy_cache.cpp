#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/config_http_failed_proxy_cache.hpp"
#include "opensim/framework/config_http_environment_proxy_native.hpp"
#include <chrono>
#include <bit>
namespace opensim::framework {
namespace {
void key(ConfigUriObjectPtr uri) {
    if (!uri)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"Value cannot be null. (Parameter 'key')", 0x80004003, u"key");
}
std::int64_t plus(std::int64_t tick, std::uint64_t delta) {
    return std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(tick) + delta);
}
}
ConfigHttpFailedProxyCacheBackend native_config_http_failed_proxy_cache_backend() {
    return {[] {
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            },
            config_http_proxy_http_uri_equal};
}
ConfigHttpFailedProxyCache::ConfigHttpFailedProxyCache(ConfigHttpFailedProxyCacheBackend backend)
    : backend_(std::move(backend)), next_flush_(0) {
    if (!backend_.ticks || !backend_.equal)
        throw ConfigHttpProxyProviderError("FailedProxyCache requires tick and Uri.Equals providers");
    next_flush_.store(plus(backend_.ticks(), 300000));
}
std::optional<std::int64_t> ConfigHttpFailedProxyCache::find(ConfigUriObjectPtr uri) {
    key(uri);
    std::lock_guard lock(mutex_);
    for (auto& entry : entries_)
        if (backend_.equal(entry.uri, uri))
            return entry.ticks;
    return {};
}
void ConfigHttpFailedProxyCache::set_proxy_failed(ConfigUriObjectPtr uri) {
    auto ticks = plus(backend_.ticks(), 1800000);
    key(uri);
    {
        std::lock_guard lock(mutex_);
        bool found = false;
        for (auto& entry : entries_)
            if (backend_.equal(entry.uri, uri)) {
                entry.ticks = ticks;
                found = true;
                break;
            }
        if (!found)
            entries_.push_back({std::move(uri), ticks});
    }
    cleanup();
}
bool ConfigHttpFailedProxyCache::try_renew_proxy(ConfigUriObjectPtr uri, std::int64_t ticks) {
    if (!uri)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"TKey is a reference type and item.Key is null. (Parameter 'item')",
                                       0x80004003, u"item");
    std::lock_guard lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
        if (backend_.equal(it->uri, uri)) {
            if (it->ticks != ticks)
                return false;
            entries_.erase(it);
            return true;
        }
    return false;
}
std::int64_t ConfigHttpFailedProxyCache::get_proxy_renew_ticks(ConfigUriObjectPtr uri) {
    cleanup();
    auto ticks = find(uri);
    if (!ticks)
        return 0;
    if (backend_.ticks() < *ticks)
        return *ticks;
    if (try_renew_proxy(uri, *ticks))
        return 0;
    return find(uri).value_or(0);
}
void ConfigHttpFailedProxyCache::cleanup() {
    {
        std::lock_guard lock(mutex_);
        if (entries_.size() <= 8)
            return;
    }
    if (backend_.ticks() < next_flush_.load())
        return;
    std::unique_lock flush(flush_mutex_, std::try_to_lock);
    if (!flush.owns_lock())
        return;
    try {
        auto now = backend_.ticks();
        std::vector<Entry> snapshot;
        {
            std::lock_guard lock(mutex_);
            snapshot = entries_;
        }
        for (auto& entry : snapshot)
            if (now >= entry.ticks)
                try_renew_proxy(entry.uri, entry.ticks);
    } catch (...) {
        next_flush_.store(plus(backend_.ticks(), 300000));
        throw;
    }
    next_flush_.store(plus(backend_.ticks(), 300000));
}
}
