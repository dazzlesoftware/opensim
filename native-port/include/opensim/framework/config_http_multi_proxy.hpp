// Windows .NET8 MultiProxy.ReadNext shared failure/renewal policy.
#pragma once
#include "opensim/framework/config_http_multi_proxy_candidates.hpp"
#include "opensim/framework/config_http_failed_proxy_cache.hpp"
#include "opensim/framework/config_http_environment_proxy.hpp"
namespace opensim::framework {
class ConfigHttpMultiProxy {
    std::shared_ptr<ConfigHttpFailedProxyCache> cache_;
    ConfigHttpMultiProxyCandidates candidates_;
    ConfigUriObjectPtr current_;

  public:
    ConfigHttpMultiProxy(ConfigHttpMultiProxyCandidates, std::shared_ptr<ConfigHttpFailedProxyCache>);
    ConfigHttpMultiProxyCandidate read_next();
    std::size_t position() const {
        return candidates_.position();
    }
};
class ConfigHttpMultiWebProxy : public ConfigHttpProxy {
  public:
    virtual ConfigHttpMultiProxy get_multi_proxy(ConfigUriObjectPtr) const = 0;
};
}
