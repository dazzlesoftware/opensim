// Source .NET8.0.31 Uri.PrivateParseMinimal file syntax; MIT .NET license.
#pragma once
#include "opensim/framework/config_file_uri_host.hpp"
namespace opensim::framework {
struct ConfigFileUriMinimalResult {
    std::int32_t error = 0;
    std::uint64_t flags = 0;
    std::u16string parse_string;
    bool unix_file_syntax = false;
};
ConfigFileUriMinimalResult config_file_uri_parse_minimal(std::u16string, std::u16string_view original,
                                                         std::uint64_t flags, bool windows,
                                                         bool unix_file_syntax,
                                                         const ConfigUriUnicodeBackend&);
} // namespace opensim::framework
