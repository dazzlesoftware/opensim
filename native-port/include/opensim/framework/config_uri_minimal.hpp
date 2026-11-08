// Native .NET8.0.31 Uri.PrivateParseMinimal; .NET Foundation MIT license.
#pragma once
#include "opensim/framework/config_uri_authority.hpp"
namespace opensim::framework {
struct ConfigUriMinimalResult {
    std::int32_t error = 0;
    std::uint64_t flags = 0;
    std::u16string parse_string;
    ConfigUriSyntaxPtr syntax;
};
ConfigUriMinimalResult config_uri_parse_minimal(std::u16string, std::u16string_view original,
                                                std::uint64_t flags, ConfigUriSyntaxPtr, bool windows,
                                                const ConfigUriUnicodeBackend&);
} // namespace opensim::framework
