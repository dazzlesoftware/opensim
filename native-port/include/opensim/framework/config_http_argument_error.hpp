// Typed argument failure consumed by HttpConnectionPool endpoint construction.
#pragma once
#include "opensim/framework/config_uri_components.hpp"
namespace opensim::framework {
class ConfigHttpArgumentOutOfRangeError : public ConfigUriComponentsError {
  public:
    ConfigHttpArgumentOutOfRangeError(std::u16string message, std::u16string parameter, std::int64_t value)
        : ConfigUriComponentsError("System.ArgumentOutOfRangeException", std::move(message), 0x80131502,
                                   std::move(parameter)),
          actual_value(value) {}
    std::int64_t actual_value;
};
}
