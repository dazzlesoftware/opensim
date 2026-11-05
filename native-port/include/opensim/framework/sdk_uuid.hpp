// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
namespace opensim::framework {
Uuid construct_sdk_uuid(std::u16string_view text);
bool try_parse_sdk_uuid(std::u16string_view text,Uuid& result);
}
