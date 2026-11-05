// .NET8.0.31 file-syntax CheckAuthorityHelper closure. MIT .NET license.
#pragma once
#include "opensim/framework/config_uri.hpp"
#include <cstdint>
namespace opensim::framework {
struct ConfigFileUriHostResult {
    std::size_t end = 0;
    std::uint64_t flags = 0;
    std::int32_t error = 0;
    std::optional<std::u16string> rewritten_prefix;
};
ConfigFileUriHostResult config_file_uri_check_host(std::u16string_view,std::size_t start,std::size_t end,std::uint64_t flags,const ConfigUriUnicodeBackend&);
}
