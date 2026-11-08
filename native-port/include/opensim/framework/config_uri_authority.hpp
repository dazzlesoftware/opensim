// Native .NET8.0.31 Uri.CheckAuthorityHelper; .NET Foundation MIT license.
#pragma once
#include "opensim/framework/config_uri_syntax.hpp"
#include <cstdint>
namespace opensim::framework {
struct ConfigUriAuthorityResult {
    std::size_t end = 0;
    std::uint64_t flags = 0;
    std::int32_t error = 0;
    std::optional<std::u16string> rewritten_prefix;
};
// Validated parser slice, actual syntax flags and NFC provider; no URL policy.
ConfigUriAuthorityResult config_uri_check_authority(std::u16string_view, std::size_t start, std::size_t end,
                                                    std::uint64_t flags, const ConfigUriSyntax&,
                                                    const ConfigUriUnicodeBackend&);
} // namespace opensim::framework
