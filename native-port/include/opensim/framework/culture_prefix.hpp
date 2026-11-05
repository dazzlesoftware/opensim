// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <string_view>
#include <string>
namespace opensim::framework {
// OpenSim HTTP request threads select CultureInfo.InvariantCulture.
int invariant_index_of(std::u16string_view text,std::u16string_view pattern);
int invariant_index_of_ignore_case(std::u16string_view text,std::u16string_view pattern);
// CompareInfo search with the pinned .NET custom grapheme break rules.
int invariant_index_of_grapheme_utf16(std::u16string_view text,std::u16string_view pattern);
bool invariant_starts_with(std::string_view text,std::string_view prefix);
bool invariant_starts_with_ignore_case(std::string_view text,std::string_view prefix);
// CurrentCulture=InvariantCulture EndsWith(string), including ignorable suffixes.
bool invariant_ends_with_utf16(std::u16string_view text,std::u16string_view suffix);
bool invariant_equals_ignore_case(std::string_view left,std::string_view right);
// Raw UTF16 selected-ICU forms preserve lone surrogates and avoid UTF8 replacement.
int invariant_compare_ignore_case_utf16(std::u16string_view left,std::u16string_view right);
bool invariant_equals_ignore_case_utf16(std::u16string_view left,std::u16string_view right);
std::u16string invariant_upper_utf16(std::u16string_view text);
std::u16string invariant_lower_utf16(std::u16string_view text);
bool ordinal_equals_ignore_case(std::string_view left,std::string_view right);
int ordinal_compare_ignore_case(std::string_view left,std::string_view right);
}
