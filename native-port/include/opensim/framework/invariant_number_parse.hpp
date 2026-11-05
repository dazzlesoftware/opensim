// Consumed .NET invariant Convert(string, IFormatProvider) numeric closure.
// Portions derived from .NET Runtime v8.0.31, MIT license:
// third_party/dotnet_collation/LICENSE.
#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
namespace opensim::framework {
class InvariantNumberError : public std::runtime_error {
  public:
    enum class Kind { Format, Overflow };
    InvariantNumberError(Kind, std::u16string);
    Kind kind;
    std::u16string message;
};
// Fixed invariant NumberFormatInfo. Null converts to zero, as Convert does;
// these are not arbitrary-culture or arbitrary-NumberStyles parsers.
std::int32_t invariant_convert_to_int32(const std::optional<std::u16string>&);
std::int64_t invariant_convert_to_int64(const std::optional<std::u16string>&);
float invariant_convert_to_single(const std::optional<std::u16string>&);
double invariant_convert_to_double(const std::optional<std::u16string>&);
}
