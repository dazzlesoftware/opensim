// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/osd_value.hpp"
namespace opensim::network {
std::string osd_json(const Value& value,bool preserve_defaults=false);
Value parse_osd_json(std::string_view text);
}
