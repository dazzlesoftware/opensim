// Consumed CoreCLR process environment; .NET MIT license in third_party/dotnet_collation/LICENSE.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
namespace opensim::framework {
using RuntimeEnvironmentText = std::shared_ptr<const std::u16string>;
class RuntimeEnvironmentError : public std::runtime_error {
  public:
    RuntimeEnvironmentError(std::string source_type, std::int32_t hresult, std::u16string message,
                            std::optional<std::u16string> parameter = {});
    std::string source_type;
    std::int32_t hresult;
    std::u16string message;
    std::optional<std::u16string> parameter;
};
// Source string.Empty, shared by OS empty returns and compiled helper defaults.
RuntimeEnvironmentText runtime_environment_empty_text();
RuntimeEnvironmentText runtime_environment_get(RuntimeEnvironmentText variable);
void runtime_environment_set(RuntimeEnvironmentText variable, RuntimeEnvironmentText value);
}
