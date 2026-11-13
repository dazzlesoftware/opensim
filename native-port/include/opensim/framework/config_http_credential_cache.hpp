#pragma once
#include "opensim/framework/config_http_environment_proxy_native.hpp"
#include "opensim/framework/config_xml_uri_object.hpp"
#include <mutex>

namespace opensim::framework {
class ConfigHttpCredentialCache final : public ConfigHttpOriginCredentials {
  public:
    using Credential = std::shared_ptr<ConfigHttpNetworkCredential>;
    explicit ConfigHttpCredentialCache(ConfigXmlUriObjects objects);

    void add(ConfigUriObjectPtr prefix, std::u16string scheme, Credential credential);
    void add(std::u16string host, int port, std::u16string scheme, Credential credential);
    void remove(const ConfigUriObjectPtr& prefix, std::u16string_view scheme);
    void remove(std::u16string_view host, int port, std::u16string_view scheme);
    Credential get_credential(const ConfigUriObjectPtr& uri, std::u16string_view scheme) const;
    Credential get_credential(std::u16string_view host, int port, std::u16string_view scheme) const;
    Credential get_credential(const std::shared_ptr<ConfigXmlUri>& uri, std::string_view scheme) const override;
    bool allows_redirect_lookup() const override { return true; }

  private:
    struct UriEntry {
        ConfigUriObjectPtr prefix;
        std::u16string scheme;
        Credential credential;
    };
    struct HostEntry {
        std::u16string host;
        int port;
        std::u16string scheme;
        Credential credential;
    };
    ConfigXmlUriObjects objects_;
    mutable std::mutex mutex_;
    std::vector<UriEntry> uris_;
    std::vector<HostEntry> hosts_;
};
}
