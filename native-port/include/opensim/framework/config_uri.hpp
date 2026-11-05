// Consumed System.Uri predicate; MIT .NET source license:
// native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
namespace opensim::framework {
using ConfigUriText=std::shared_ptr<const std::u16string>;
struct ConfigUriUnicodeBackend {
 // String.Normalize(FormC), returning null only for invalid Unicode sequences.
 // .NET globalization-invariant/NLS modes must supply their own normalization.
 std::function<std::optional<std::u16string>(std::u16string_view)> normalize_nfc;
};
// Default native ICU selected from the host OS, matching the startup runtime.
ConfigUriUnicodeBackend native_config_uri_unicode_backend();
// Exactly ConfigurationLoader.IsUri: Uri.TryCreate(Absolute) && HTTP(S) scheme.
// This is a configuration classifier, not HTTP-client request policy or IDN host resolution.
bool is_http_configuration_uri(ConfigUriText,const ConfigUriUnicodeBackend&);
bool is_http_configuration_uri(ConfigUriText);
}
