// Consumed HTTP System.Uri components, source .NET8.0.31; MIT .NET license.
#pragma once
#include "opensim/framework/config_uri.hpp"
#include <stdexcept>
#include <cstdint>
namespace opensim::framework {
class ConfigUriComponentsError : public std::runtime_error {
public:
    ConfigUriComponentsError(std::string, std::u16string, std::uint32_t,
                             std::optional<std::u16string> parameter = {});
    std::string source_type;
    std::u16string message;
    std::uint32_t hresult;
    std::optional<std::u16string> parameter;
};
struct ConfigUriComponentsBackend {
    ConfigUriUnicodeBackend unicode;
    std::function<ConfigUriText(ConfigUriText)> lower_invariant;
    // DomainNameHelper.IdnEquivalent's actual IdnMapping.GetAscii dependency.
    std::function<ConfigUriText(ConfigUriText)> idn_ascii;
};
// Selected ICU normalization/IDNA/casing; custom providers own NLS/invariant modes.
std::shared_ptr<ConfigUriComponentsBackend> native_config_uri_components_backend();
// Component view of source-backed built-in URI objects. HTTP senders use
// is_http() for redirect policy; custom UriParser registration remains external.
class ConfigUriObject;
class ConfigUriComponents {
    struct State;
    std::shared_ptr<State> state_;
    explicit ConfigUriComponents(std::shared_ptr<State>);

public:
    // Share the native generic object's getter caches and constructor state.
    static std::shared_ptr<ConfigUriComponents> from_object(std::shared_ptr<const ConfigUriObject>,
                                                            std::shared_ptr<ConfigUriComponentsBackend>,
                                                            bool windows);
    std::shared_ptr<const ConfigUriObject> source_object() const;
    std::shared_ptr<ConfigUriComponents> rebind(std::shared_ptr<const ConfigUriObject>) const;
    static std::shared_ptr<ConfigUriComponents> absolute(ConfigUriText,
                                                         std::shared_ptr<ConfigUriComponentsBackend>);
    static std::shared_ptr<ConfigUriComponents> resolve(std::shared_ptr<ConfigUriComponents> base,
                                                        ConfigUriText relative);
    ConfigUriText original_string() const;
    ConfigUriText scheme() const;
    bool is_http() const;
    ConfigUriText host() const;
    ConfigUriText authority() const;
    ConfigUriText idn_host() const;
    ConfigUriText dns_safe_host() const;
    std::int32_t port() const;
    ConfigUriText user_info() const;
    ConfigUriText absolute_path() const;
    ConfigUriText query() const;
    ConfigUriText fragment() const;
    ConfigUriText absolute_uri() const;
};
}
