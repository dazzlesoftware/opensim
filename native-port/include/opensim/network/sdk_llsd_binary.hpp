// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/rpc.hpp"
namespace opensim::network {
// Bundled OSDParser.DeserializeLLSDBinary on a seekable byte stream.
Value deserialize_sdk_llsd_binary(std::string_view bytes);
}
