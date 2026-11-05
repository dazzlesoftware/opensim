// Exact installed .NET8.0.31 Uri.GetException invariant fallback strings; MIT .NET license.
#pragma once
#include <array>
#include <string_view>
namespace opensim::framework {inline constexpr std::array<std::u16string_view,13> config_file_uri_error_messages={u"",u"Invalid URI: The format of the URI could not be determined.",u"Invalid URI: The URI scheme is not valid.",u"Invalid URI: The Authority/Host could not be parsed.",u"Invalid URI: The URI is empty.",u"Invalid URI: The Uri scheme is too long.",u"Invalid URI: The Uri string is too long.",u"Invalid URI: A Dos path must be rooted, for example, 'c:\\\\'.",u"Invalid URI: The hostname could not be parsed.",u"Invalid URI: The format of the URI could not be determined.",u"Invalid URI: Invalid port specified.",u"Invalid URI: The Authority/Host cannot end with a backslash character ('\\\\').",u"A relative URI cannot be created because the 'uriString' parameter represents an absolute URI."};}
