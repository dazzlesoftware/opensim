// Native .NET8.0.31 Uri.CheckCanonical; .NET Foundation MIT license.
#pragma once
#include "opensim/framework/config_uri_syntax.hpp"
namespace opensim::framework {
struct ConfigUriCanonicalResult {
    std::int32_t check = 0;
    std::size_t position = 0;
};
ConfigUriCanonicalResult config_uri_check_canonical(std::u16string_view,std::size_t start,std::size_t end,char16_t delimiter,bool implicit_file,bool has_unicode,const ConfigUriSyntax* syntax);
}
