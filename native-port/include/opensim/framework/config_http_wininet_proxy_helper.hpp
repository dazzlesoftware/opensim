// .NET8 WinInetProxyHelper policy and native WinHTTP boundary.
#pragma once
#include "opensim/framework/config_http_environment_proxy.hpp"
#include <atomic>
namespace opensim::framework {
struct ConfigHttpWinInetSettings {
    ConfigUriText auto_config_url, proxy, proxy_bypass;
    bool auto_detect = false, use_proxy = false;
};
struct ConfigHttpWinHttpAutoOptions {
    ConfigUriText auto_config_url;
    std::uint32_t flags = 0, auto_detect_flags = 0;
    bool auto_login = false;
};
struct ConfigHttpWinHttpProxyInfo {
    std::uint32_t access_type = 1;
    ConfigUriText proxy, proxy_bypass;
};
struct ConfigHttpWinHttpProxyResult {
    bool success = false;
    int error = 0;
    ConfigHttpWinHttpProxyInfo info;
};
class ConfigHttpWinHttpSession {
public:
    virtual ~ConfigHttpWinHttpSession() = default;
    virtual ConfigHttpWinHttpProxyResult get_proxy_for_url(ConfigUriText,
                                                           const ConfigHttpWinHttpAutoOptions&) = 0;
};
std::int32_t config_http_wininet_tick_count();
ConfigHttpWinInetSettings native_config_http_wininet_settings();
// null is the source WinHttpOpen invalid-handle result; native API unavailable
// on other platforms is an explicit provider error.
std::shared_ptr<ConfigHttpWinHttpSession> native_config_http_winhttp_session();
class ConfigHttpWinInetProxyHelper {
    ConfigHttpWinInetSettings settings_;
    std::function<std::int32_t()> ticks_;
    std::atomic<bool> auto_failed_{false};
    std::atomic<std::int32_t> last_failure_{0};

public:
    explicit ConfigHttpWinInetProxyHelper(ConfigHttpWinInetSettings,
                                          std::function<std::int32_t()> = config_http_wininet_tick_count);
    const ConfigHttpWinInetSettings& settings() const {
        return settings_;
    }
    bool auto_settings_used() const;
    bool manual_settings_used() const;
    bool manual_settings_only() const {
        return !auto_settings_used() && manual_settings_used();
    }
    bool recent_auto_detection_failure() const;
    ConfigHttpWinHttpProxyResult get_proxy_for_url(std::shared_ptr<ConfigHttpWinHttpSession>,
                                                   ConfigUriObjectPtr);
};
}
