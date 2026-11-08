// Source .NET8.0.31 Uri.CheckCanonical for built-in file syntax. MIT .NET license.
#pragma once
#include <string_view>
#include <cstdint>
namespace opensim::framework {
struct ConfigFileUriCanonicalResult {
    std::int32_t check = 0;
    std::size_t position = 0;
};
ConfigFileUriCanonicalResult config_file_uri_check_canonical(std::u16string_view, std::size_t start,
                                                             std::size_t end, char16_t delimiter,
                                                             bool implicit_file, bool has_unicode);
} // namespace opensim::framework
