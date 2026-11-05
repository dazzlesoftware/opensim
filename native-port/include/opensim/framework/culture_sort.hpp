// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <string_view>
namespace opensim::framework {
int invariant_compare(std::string_view left,std::string_view right);
int invariant_compare_utf16(std::u16string_view left,std::u16string_view right);
bool invariant_starts_with_ignore_case(std::string_view text,std::string_view prefix);
}
