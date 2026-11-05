// .NET8.0.31 Uri.CreateUriInfo/ParseRemaining, built-in file syntax. MIT license.
#pragma once
#include "opensim/framework/config_file_uri_minimal.hpp"
#include <stdexcept>
namespace opensim::framework {
struct ConfigFileUriOffsets {
    std::uint16_t scheme = 0, user = 0, host = 0, port = 0, path = 0, query = 0, fragment = 0, end = 0;
};
struct ConfigFileUriState {
    std::uint64_t flags = 0;
    std::u16string parse_string, original_string;
    ConfigFileUriOffsets offsets;
    bool info = false, unix_file_syntax = false;
};
class ConfigFileUriStateError : public std::runtime_error {
public:
    ConfigFileUriStateError(bool index = false)
        : runtime_error(index ? "Index was outside the bounds of the array."
                              : "Invalid URI: The Uri string is too long."),
          source_type(index ? "System.IndexOutOfRangeException" : "System.UriFormatException"),
          hresult(index ? 0x80131508 : 0x80131537) {};
    const std::string source_type;
    const std::uint32_t hresult;
};
void config_file_uri_create_info(ConfigFileUriState&);
void config_file_uri_parse_remaining(ConfigFileUriState&);
}
