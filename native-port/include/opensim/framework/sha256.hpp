// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <string>
#include <string_view>

namespace opensim::framework {
std::string sha256_hex(std::string_view bytes, bool sha224 = false);
}
