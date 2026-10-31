// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <string_view>
namespace opensim::framework {
// OpenSim HTTP request threads select CultureInfo.InvariantCulture.
bool invariant_starts_with(std::string_view text,std::string_view prefix);
bool invariant_equals_ignore_case(std::string_view left,std::string_view right);
bool ordinal_equals_ignore_case(std::string_view left,std::string_view right);
int ordinal_compare_ignore_case(std::string_view left,std::string_view right);
}
