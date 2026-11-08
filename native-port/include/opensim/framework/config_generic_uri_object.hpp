// Native built-in/V1 System.Uri constructor and consumed getters, runtime8.0.31.
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/config_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
namespace opensim::framework {
// No custom UriParser registration or foreign URI implementation is fabricated.
ConfigUriObjectPtr config_generic_uri_create(ConfigUriText, ConfigUriObjectKind,
                                             std::shared_ptr<ConfigUriComponentsBackend>, bool windows,
                                             bool user_escaped = false);
ConfigUriObjectPtr config_generic_uri_try_create(ConfigUriText, ConfigUriObjectKind,
                                                 std::shared_ptr<ConfigUriComponentsBackend>, bool windows);
// GetComponents(Path | KeepDelimiter, format), including the original-string fast path.
ConfigUriText config_generic_uri_path(ConfigUriObjectPtr, std::int32_t format);
ConfigUriObjectPtr config_generic_uri_combine_string(
    ConfigUriObjectPtr, ConfigUriText,
    const std::function<ConfigUriObjectPtr(ConfigUriText, ConfigUriObjectKind)>&);
bool config_generic_uri_owns(ConfigUriObjectPtr);
ConfigUriObjectPtr
config_generic_uri_combine_uri(ConfigUriObjectPtr, ConfigUriObjectPtr,
                               const std::function<ConfigUriObjectPtr(ConfigUriText, ConfigUriObjectKind)>&);
ConfigUriObjectPtr config_generic_uri_with_fragment(
    ConfigUriObjectPtr, ConfigUriText,
    const std::function<ConfigUriObjectPtr(ConfigUriText, ConfigUriObjectKind)>&);
ConfigUriObjectPtr config_generic_uri_clone(ConfigUriObjectPtr);
enum class ConfigGenericUriComponent { Host, IdnHost, DnsSafeHost, UserInfo, AbsolutePath, Query };
ConfigUriText config_generic_uri_component(ConfigUriObjectPtr, ConfigGenericUriComponent);
std::int32_t config_generic_uri_port(ConfigUriObjectPtr);
ConfigUriText config_generic_uri_authority(ConfigUriObjectPtr);
// Uri.Equals remote URL for built-in HTTP proxy pool keys: HttpRequestUrl,
// SafeUnescaped, excluding user information and fragment.
ConfigUriText config_generic_uri_http_pool_identity(ConfigUriObjectPtr);
} // namespace opensim::framework
