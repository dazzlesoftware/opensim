// .NET8 FailedProxyCache; MIT .NET runtime license.
#pragma once
#include "opensim/framework/config_uri_object.hpp"
#include <atomic>
#include <mutex>
#include <vector>
namespace opensim::framework {
struct ConfigHttpFailedProxyCacheBackend {
    std::function<std::int64_t()> ticks;
    std::function<bool(ConfigUriObjectPtr, ConfigUriObjectPtr)> equal;
};
ConfigHttpFailedProxyCacheBackend native_config_http_failed_proxy_cache_backend();
struct ConfigHttpFailedProxyCacheFixture;
class ConfigHttpFailedProxyCache {
    struct Entry {
        ConfigUriObjectPtr uri;
        std::int64_t ticks;
    };
    ConfigHttpFailedProxyCacheBackend backend_;
    std::mutex mutex_, flush_mutex_;
    std::vector<Entry> entries_;
    std::atomic<std::int64_t> next_flush_;
    std::optional<std::int64_t> find(ConfigUriObjectPtr);
    void cleanup();
    friend struct ConfigHttpFailedProxyCacheFixture;

public:
    explicit ConfigHttpFailedProxyCache(
        ConfigHttpFailedProxyCacheBackend = native_config_http_failed_proxy_cache_backend());
    std::int64_t get_proxy_renew_ticks(ConfigUriObjectPtr);
    void set_proxy_failed(ConfigUriObjectPtr);
    bool try_renew_proxy(ConfigUriObjectPtr, std::int64_t);
};
}
