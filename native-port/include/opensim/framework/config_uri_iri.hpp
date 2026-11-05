// Consumed .NET8 IriHelper and Uri.CheckForUnicodeOrEscapedUnreserved.
#pragma once
#include <string>
#include <string_view>
#include <cstdint>
namespace opensim::framework {
bool config_uri_has_iri_characters(std::u16string_view);
// component is the exact UriComponents bit pattern (Query is32); input is the
// valid source start/end slice. No normalization or constructor validation.
std::u16string config_uri_escape_unescape_iri(std::u16string_view,std::uint32_t component);
}
