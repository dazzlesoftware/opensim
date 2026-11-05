// Native .NET8.0.31 Uri state/offset publication; .NET Foundation MIT license.
#pragma once
#include "opensim/framework/config_uri_minimal.hpp"
#include <stdexcept>
namespace opensim::framework {
struct ConfigUriOffsets {
    std::uint16_t scheme = 0, user = 0, host = 0, port_value = 0, path = 0, query = 0, fragment = 0, end = 0;
};
struct ConfigUriState {
    std::uint64_t flags = 0;
    std::u16string parse_string, original_string;
    ConfigUriSyntaxPtr syntax;
    ConfigUriOffsets offsets;
    bool info = false;
};
class ConfigUriStateError : public std::runtime_error {
  public:
    explicit ConfigUriStateError(bool index = false)
        : runtime_error(index ? "Index was outside the bounds of the array."
                              : "Invalid URI: The Uri string is too long."),
          source_type(index ? "System.IndexOutOfRangeException" : "System.UriFormatException"),
          hresult(index ? 0x80131508 : 0x80131537) {}
    const std::string source_type;
    const std::uint32_t hresult;
};
// The owning URI synchronizes state; these are source phase operations.
void config_uri_create_info(ConfigUriState&);
void config_uri_parse_remaining(ConfigUriState&);
}
