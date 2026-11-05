#include "opensim/framework/config_http_multi_proxy.hpp"
#include "opensim/framework/config_http_environment_proxy.hpp"
#include <limits>
namespace opensim::framework {
ConfigHttpMultiProxy::ConfigHttpMultiProxy(ConfigHttpMultiProxyCandidates candidates,
                                           std::shared_ptr<ConfigHttpFailedProxyCache> cache)
    : cache_(std::move(cache)), candidates_(std::move(candidates)) {
    if (!cache_)
        throw ConfigHttpProxyProviderError("MultiProxy requires its shared FailedProxyCache");
}
ConfigHttpMultiProxyCandidate ConfigHttpMultiProxy::read_next() {
    if (current_)
        cache_->set_proxy_failed(current_);
    auto result = candidates_.next();
    if (!result.found) {
        current_ = {};
        return result;
    }
    ConfigUriObjectPtr earliest;
    auto earliest_ticks = std::numeric_limits<std::int64_t>::max();
    do {
        auto ticks = cache_->get_proxy_renew_ticks(result.uri);
        if (ticks == 0) {
            current_ = result.uri;
            return result;
        }
        if (ticks < earliest_ticks) {
            earliest = result.uri;
            earliest_ticks = ticks;
        }
    } while ((result = candidates_.next()).found);
    if (!current_) {
        result.uri = earliest;
        current_ = earliest;
        if (earliest) {
            cache_->try_renew_proxy(earliest, earliest_ticks);
            result.found = true;
            return result;
        }
    }
    return result;
}
}
