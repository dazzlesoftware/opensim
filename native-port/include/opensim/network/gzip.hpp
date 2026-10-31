// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <string>
#include <string_view>
namespace opensim::network {
std::string gzip_compress(std::string_view bytes);
}
