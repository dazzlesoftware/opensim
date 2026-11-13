// Source .NET8.0.31 Uri.Compress; .NET Foundation MIT license.
#pragma once
#include <string>
#include <cstdint>
namespace opensim::framework {
// Internal consumed file-URI canonicalization kernel; not a URI parser.
std::u16string config_file_uri_compress(std::u16string, bool convert_slashes, bool canonicalize_file = true);
std::u16string config_file_uri_unescape_dots(std::u16string,bool convert_slashes);
struct ConfigFileUriSchemeScan {
    std::int32_t error = 0;
    std::uint64_t flags = 0;
    std::u16string scheme;
    bool unix_file_syntax = false;
};
ConfigFileUriSchemeScan config_file_uri_scan_scheme(std::u16string_view,bool windows);
bool config_file_uri_implicit_kind_is_relative(const ConfigFileUriSchemeScan&, std::u16string_view,
                                               std::int32_t kind, bool windows);
}
