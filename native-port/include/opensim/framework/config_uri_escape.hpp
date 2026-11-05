// Source .NET8.0.31 UriHelper.EscapeString; MIT .NET license.
#pragma once
#include "opensim/framework/config_uri.hpp"
#include <stdexcept>
#include <cstdint>
namespace opensim::framework {
enum class ConfigUriEscapeSet:int{Unreserved=0,UnreservedReserved=1,ExceptHash=2,ExceptQuestionMarkHash=3};
class ConfigUriEscapeError : public std::runtime_error {
public:
    ConfigUriEscapeError() : runtime_error("Value cannot be null. (Parameter 'stringToEscape')") {};
    const std::string source_type = "System.ArgumentNullException";
    const std::uint32_t hresult = 0x80004003;
    const std::u16string parameter = u"stringToEscape";
};
std::u16string config_uri_escape(std::u16string_view,bool check_existing_escaped,ConfigUriEscapeSet);
ConfigUriText config_uri_escape(ConfigUriText,bool check_existing_escaped,ConfigUriEscapeSet);
}
