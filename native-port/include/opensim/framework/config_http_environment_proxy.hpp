// Consumed .NET8 HttpEnvironmentProxy and companion credential controller.
// .NET Foundation MIT license, third_party/dotnet_collation/LICENSE.
#pragma once
#include <stdexcept>
#include <optional>
#include <vector>
#include <atomic>
#include "opensim/framework/config_uri_object.hpp"
#include "opensim/framework/config_xml_transport.hpp"
namespace opensim::framework {
class ConfigHttpProxyProviderError : public std::logic_error {
public:
    using std::logic_error::logic_error;
};
class ConfigHttpProxyUriBuilder {
public:
    virtual ~ConfigHttpProxyUriBuilder() = default;
    virtual void user_name(ConfigUriText) = 0;
    virtual void password(ConfigUriText) = 0;
    virtual ConfigUriObjectPtr uri() = 0;
};
struct ConfigHttpEnvironmentProxyBackend {
    std::function<ConfigUriText(ConfigUriText)> environment;
    std::function<std::shared_ptr<ConfigHttpProxyUriBuilder>(ConfigUriText, ConfigUriText, std::uint16_t)>
        new_builder;
    std::function<ConfigUriObjectPtr(ConfigUriText)> absolute_uri;
    std::function<ConfigUriText(ConfigUriText)> unescape_data, escape_data;
    std::function<ConfigUriText(ConfigUriObjectPtr)> user_info, host;
    std::function<bool(ConfigUriObjectPtr, ConfigUriObjectPtr)> uri_equal;
    std::function<bool(std::u16string_view, std::u16string_view)> ordinal_equal, ordinal_ends_with;
    std::function<bool(char16_t)> white_space, is_digit;
    std::function<std::shared_ptr<ConfigXmlCredentials>(ConfigUriText, ConfigUriText, ConfigUriText)>
        network_credential;
    std::function<std::shared_ptr<ConfigXmlCredentials>()> default_network_credentials;
};
// Backend absence must remain explicit, never swallowed as a bad proxy URI.
// Source-managed URI/encoding failures in GetUriFromString are swallowed exactly.
ConfigUriObjectPtr config_http_environment_proxy_uri(ConfigUriText,
                                                     std::shared_ptr<ConfigHttpEnvironmentProxyBackend>);
class ConfigHttpEnvironmentProxyCredentials : public ConfigXmlCredentials {
    ConfigUriObjectPtr http_, https_;
    std::shared_ptr<ConfigXmlCredentials> http_credential_, https_credential_;
    std::shared_ptr<ConfigHttpEnvironmentProxyBackend> backend_;

public:
    ConfigHttpEnvironmentProxyCredentials(ConfigUriObjectPtr, std::shared_ptr<ConfigXmlCredentials>,
                                          ConfigUriObjectPtr, std::shared_ptr<ConfigXmlCredentials>,
                                          std::shared_ptr<ConfigHttpEnvironmentProxyBackend>);
    std::shared_ptr<ConfigXmlCredentials> get_credential(ConfigUriObjectPtr, ConfigUriText auth_type) const;
    static std::shared_ptr<ConfigHttpEnvironmentProxyCredentials>
        try_create(ConfigUriObjectPtr, ConfigUriObjectPtr,
                   std::shared_ptr<ConfigHttpEnvironmentProxyBackend>);
};
// Consumed IWebProxy surface shared by environment, no-proxy and OS adapters.
class ConfigHttpProxy : public ConfigXmlProxy {
public:
    virtual ConfigUriObjectPtr get_proxy(ConfigUriObjectPtr) const = 0;
    virtual bool is_bypassed(ConfigUriObjectPtr) const = 0;
    virtual std::shared_ptr<ConfigXmlCredentials> credentials() const = 0;
    virtual void credentials(std::shared_ptr<ConfigXmlCredentials>) = 0;
};
enum class ConfigHttpEnvironmentProxyPlatform { Windows, Unix };
class ConfigHttpEnvironmentProxy : public ConfigHttpProxy {
    ConfigUriObjectPtr http_, https_;
    std::optional<std::vector<ConfigUriText>> bypass_;
    std::atomic<std::shared_ptr<ConfigXmlCredentials>> credentials_;
    std::shared_ptr<ConfigHttpEnvironmentProxyBackend> backend_;
    ConfigHttpEnvironmentProxy(ConfigUriObjectPtr, ConfigUriObjectPtr, ConfigUriText,
                               std::shared_ptr<ConfigHttpEnvironmentProxyBackend>);

public:
    static std::shared_ptr<ConfigHttpEnvironmentProxy>
        try_create(std::shared_ptr<ConfigHttpEnvironmentProxyBackend>, ConfigHttpEnvironmentProxyPlatform);
    ConfigUriObjectPtr get_proxy(ConfigUriObjectPtr) const;
    bool is_bypassed(ConfigUriObjectPtr) const;
    std::shared_ptr<ConfigXmlCredentials> credentials() const {
        return credentials_.load();
    }
    void credentials(std::shared_ptr<ConfigXmlCredentials> value) {
        credentials_.store(std::move(value));
    }
};
}
