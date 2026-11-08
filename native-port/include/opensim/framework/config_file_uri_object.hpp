// Source .NET8.0.31 consumed built-in file Uri object; MIT .NET license.
#pragma once
#include "opensim/framework/config_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/config_file_uri_path.hpp"
namespace opensim::framework {
class ConfigFileUriRangeError : public ConfigUriComponentsError {
  public:
    ConfigFileUriRangeError(std::u16string message, std::u16string parameter,
                            std::optional<std::int64_t> value = {})
        : ConfigUriComponentsError("System.ArgumentOutOfRangeException", std::move(message), 0x80131502,
                                   std::move(parameter)),
          actual_value(value) {};
    std::optional<std::int64_t> actual_value;
};
bool config_file_uri_applies(const ConfigFileUriSchemeScan&);
// Only file/DOS/UNC dispatch. Generic schemes belong to the composing backend.
ConfigUriObjectPtr config_file_uri_create(ConfigUriText, ConfigUriObjectKind,
                                          std::shared_ptr<ConfigUriComponentsBackend>, bool windows);
ConfigUriObjectPtr config_file_uri_try_create(ConfigUriText, ConfigUriObjectKind,
                                              std::shared_ptr<ConfigUriComponentsBackend>, bool windows);

using ConfigFileUriCreate = std::function<ConfigUriObjectPtr(ConfigUriText, ConfigUriObjectKind)>;
ConfigUriObjectPtr config_file_uri_combine_string(ConfigUriObjectPtr, ConfigUriText,
                                                  const ConfigFileUriCreate&);
ConfigUriObjectPtr config_file_uri_combine_uri(ConfigUriObjectPtr, ConfigUriObjectPtr,
                                               const ConfigFileUriCreate&);
ConfigUriObjectPtr config_file_uri_with_fragment(ConfigUriObjectPtr, ConfigUriText,
                                                 const ConfigFileUriCreate&);
} // namespace opensim::framework
