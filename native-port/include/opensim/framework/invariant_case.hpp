// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <string>
#include <string_view>
namespace opensim::framework {
std::u16string lower_invariant_culture(std::u16string_view);
std::string lower_invariant_culture(std::string_view);
}
