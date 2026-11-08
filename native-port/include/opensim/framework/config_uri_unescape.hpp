// .NET8.0.31 UriHelper.UnescapeString/PercentEncodingHelper; MIT .NET license.
#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstdint>
namespace opensim::framework {
class ConfigUriUnescapeError : public std::runtime_error {
  public:
    const std::string source_type = "System.UriFormatException";
    const std::uint32_t hresult = 0x80131537;
    const std::u16string message = u"Invalid URI: There is an invalid sequence in the string.";
    ConfigUriUnescapeError() : runtime_error("Invalid URI: There is an invalid sequence in the string.") {};
};
// Exact UnescapeMode bits:Copy0,Escape1,Unescape2,V1ToString4,All8,AllOrThrow24.
std::u16string config_uri_unescape(std::u16string_view, char16_t reserved1, char16_t reserved2,
                                   char16_t reserved3, int mode, bool iri_syntax, bool is_query);
} // namespace opensim::framework
