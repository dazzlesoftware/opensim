#pragma once
#include "opensim/framework/config_http_multi_proxy.hpp"
#include "opensim/framework/config_http_wininet_proxy_helper.hpp"
#include "opensim/framework/config_http_windows_proxy_bypass.hpp"
namespace opensim::framework {
struct ConfigHttpWindowsProxyOptions {
    std::function<ConfigHttpWinInetSettings()> read_settings = native_config_http_wininet_settings;
    std::function<std::shared_ptr<ConfigHttpWinHttpSession>()> open_session = native_config_http_winhttp_session;
    std::function<std::int32_t()> ticks = config_http_wininet_tick_count;
    ConfigUriObjectBackend uris;
    std::optional<ConfigHttpWindowsProxyBypassBackend> bypass;
};
struct ConfigHttpWindowsProxyFixture;
class ConfigHttpWindowsProxy final : public ConfigHttpMultiWebProxy {
    std::shared_ptr<ConfigHttpWinInetProxyHelper> helper_;
    std::atomic<std::shared_ptr<ConfigHttpWinHttpSession>> session_;
    std::atomic<std::shared_ptr<ConfigXmlCredentials>> credentials_;
    std::atomic<bool> disposed_{false};
    ConfigUriObjectBackend uris_;
    std::shared_ptr<ConfigHttpFailedProxyCache> cache_;
    ConfigHttpMultiProxyCandidates insecure_, secure_;
    std::unique_ptr<ConfigHttpWindowsProxyBypass> bypass_;
    ConfigHttpWindowsProxy(std::shared_ptr<ConfigHttpWinInetProxyHelper>, std::shared_ptr<ConfigHttpWinHttpSession>,
                           ConfigHttpWindowsProxyOptions);
    ConfigHttpMultiProxy empty() const;
    friend struct ConfigHttpWindowsProxyFixture;

  public:
    static std::shared_ptr<ConfigHttpWindowsProxy> try_create(ConfigHttpWindowsProxyOptions = {});
    ConfigHttpMultiProxy get_multi_proxy(ConfigUriObjectPtr) const override;
    ConfigUriObjectPtr get_proxy(ConfigUriObjectPtr) const override;
    bool is_bypassed(ConfigUriObjectPtr) const override { return false; }
    std::shared_ptr<ConfigXmlCredentials> credentials() const override { return credentials_.load(); }
    void credentials(std::shared_ptr<ConfigXmlCredentials> value) override { credentials_.store(std::move(value)); }
    void dispose();
};
std::shared_ptr<ConfigHttpProxy> native_config_http_windows_proxy();
}
