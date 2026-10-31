// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/rpc.hpp"
#include "opensim/framework/uuid.hpp"
namespace opensim::network {
framework::Uuid osd_uuid(const Value& value);
bool osd_boolean(const Value& value);
// CLR invariant Float|AllowThousands parsing, with failure distinguished from zero.
bool try_parse_invariant_real(std::string_view,double&);
double osd_real(const Value& value);
std::int32_t osd_integer(const Value& value);
std::uint32_t osd_unsigned(const Value& value);
std::string osd_string(const Value& value);
std::string osd_to_string(const Value& value);
using BinaryData=std::shared_ptr<std::vector<std::uint8_t>>;
BinaryData osd_binary(const Value& value);
Value osd_from_binary(BinaryData bytes);
Value osd_from_unsigned(std::uint32_t number);
std::string binary_base64(const Value& value);
}
