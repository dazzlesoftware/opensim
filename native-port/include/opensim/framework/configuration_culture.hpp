// Consumed .NET8 invariant StartsWith closure; MIT source license:
// native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
namespace opensim::framework {
using ConfigurationCultureText = std::shared_ptr<const std::u16string>;
class ConfigurationCultureNullError : public std::runtime_error {
  public:
    ConfigurationCultureNullError()
        : runtime_error("Object reference not set to an instance of an object.") {}
    const char* source_type() const noexcept {
        return "System.NullReferenceException";
    }
    std::uint32_t hresult() const noexcept {
        return 0x80004003u;
    }
};
// ConfigurationLoader consumes only text.StartsWith("Include-") under the
// actual startup InvariantCulture with default selected ICU. NLS and
// globalization-invariant modes need different host providers. Arbitrary
// CurrentCulture must stay a host
// provider; this helper never changes the thread/process culture.
bool startup_invariant_configuration_include_key(ConfigurationCultureText);
}
