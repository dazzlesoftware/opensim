// Concrete raw process environment and consumed HTTP proxy URI dependencies.
#pragma once
#include "opensim/framework/config_http_environment_proxy.hpp"
#include "opensim/framework/file_system_name.hpp"
namespace opensim::framework {
// NetworkCredential value representation; authentication protocols consume this
// separately. Default credentials are a process-wide identity marker, not a
// fabricated username/password from the host environment.
class ConfigHttpNetworkCredential final : public ConfigXmlCredentials {
  public:
    ConfigHttpNetworkCredential(ConfigUriText user, ConfigUriText password, ConfigUriText domain,
                                bool defaults = false)
        : user_name(user ? std::move(user) : nini_writer_text(u"")),
          password(password ? std::move(password) : nini_writer_text(u"")),
          domain(domain ? std::move(domain) : nini_writer_text(u"")), is_default(defaults) {}
    ConfigUriText user_name, password, domain;
    bool is_default;
};
std::shared_ptr<ConfigXmlCredentials> config_http_default_network_credentials();

// ICredentials lookup for origin challenges. A null result declines that scheme.
class ConfigHttpOriginCredentials : public ConfigXmlCredentials {
  public:
    virtual std::shared_ptr<ConfigHttpNetworkCredential>
    get_credential(const std::shared_ptr<ConfigXmlUri>& uri, std::string_view scheme) const = 0;
    // URI-scoped caches may perform a fresh lookup after an automatic redirect.
    virtual bool allows_redirect_lookup() const { return false; }
};

struct ConfigHttpEnvironmentProxyNativeOptions {
    ConfigUriObjectBackend uris;
    std::function<ConfigUriText(ConfigUriText)> environment;
    std::optional<file_system_name::CasingBackend> casing;
    // Optional overrides for foreign URI/credential implementations.
    std::function<std::shared_ptr<ConfigHttpProxyUriBuilder>(ConfigUriText, ConfigUriText, std::uint16_t)>
        other_builder;
    std::function<ConfigUriText(ConfigUriObjectPtr)> other_host, other_user_info;
    std::function<bool(ConfigUriObjectPtr, ConfigUriObjectPtr)> other_uri_equal;
    std::function<std::shared_ptr<ConfigXmlCredentials>(ConfigUriText, ConfigUriText, ConfigUriText)>
        network_credential;
    std::function<std::shared_ptr<ConfigXmlCredentials>()> default_network_credentials;
};
bool config_http_proxy_dotnet8_is_digit(char16_t);
// HTTP/relative consumed Uri.Equals, not general file/custom UriParser comparison.
// Unsupported schemes remain required rather than approximated with string equality.
bool config_http_proxy_http_uri_equal(ConfigUriObjectPtr, ConfigUriObjectPtr);
// Normal pinned .NET8 Unicode casing is the explicit default; supply casing for
// globalization-invariant/NLS/custom runtime profiles. OS proxy/PAC is separate.
std::shared_ptr<ConfigHttpEnvironmentProxyBackend>
    native_config_http_environment_proxy_backend(ConfigHttpEnvironmentProxyNativeOptions = {});
}
