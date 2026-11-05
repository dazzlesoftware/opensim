#pragma once
#include "opensim/framework/config_http_environment_proxy.hpp"
#include "opensim/framework/config_uri_network_identity.hpp"
#include "opensim/framework/file_system_name.hpp"
#include "opensim/framework/ip_address.hpp"
namespace opensim::framework {
struct ConfigHttpWindowsProxyBypassBackend {
    file_system_name::CasingBackend casing;
    std::function<bool(char16_t)> white_space;
    std::function<ConfigUriNetworkIdentity(ConfigUriObjectPtr)> identity;
    std::function<ConfigUriText(ConfigUriObjectPtr)> idn_host;
    std::function<std::vector<IpAddress>()> local_addresses;
};
std::vector<IpAddress> native_config_http_windows_local_addresses();
ConfigHttpWindowsProxyBypassBackend native_config_http_windows_proxy_bypass_backend();
bool config_http_windows_proxy_star_match(std::u16string_view, std::u16string_view,
                                          const file_system_name::CasingBackend&);
class ConfigHttpWindowsProxyBypass {
    ConfigHttpWindowsProxyBypassBackend backend_;
    bool local_ = false;
    std::vector<ConfigUriText> patterns_;
    std::vector<IpAddress> addresses_;

public:
    explicit ConfigHttpWindowsProxyBypass(
        ConfigUriText, ConfigHttpWindowsProxyBypassBackend = native_config_http_windows_proxy_bypass_backend());
    bool bypasses(ConfigUriObjectPtr) const;
    bool bypass_local() const {
        return local_;
    }
    const std::vector<ConfigUriText>& patterns() const {
        return patterns_;
    }
};
}
