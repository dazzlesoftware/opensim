// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/rpc.hpp"
namespace opensim::network {
// Bundled OSDParser.DeserializeLLSDBinary on a seekable byte stream.
Value deserialize_sdk_llsd_binary(std::string_view bytes);
Value deserialize_sdk_llsd_binary_prefix(std::string_view bytes, std::size_t& consumed, std::size_t max_depth);
Value deserialize_zipped_llsd_binary(std::string_view bytes);
Value deserialize_zipped_llsd_binary(std::string_view bytes, std::size_t max_depth);
std::string serialize_zipped_llsd_binary(const Value& value);
}
