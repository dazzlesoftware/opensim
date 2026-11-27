// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include <string>
#include <string_view>

namespace opensim::framework {

std::string sha512_hex(std::string_view bytes, bool sha384 = false);

} // namespace opensim::framework
