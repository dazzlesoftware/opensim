// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/rpc.hpp"
namespace opensim::network {
// OSDParser.Deserialize(Stream), invariant-culture HTTP request context.
Value deserialize_sdk_osd_stream(std::string_view bytes, bool seekable = true);
Value deserialize_sdk_osd_bytes(std::string_view bytes);
Value deserialize_sdk_osd_text(std::u16string_view text);
}
