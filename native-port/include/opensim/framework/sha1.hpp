// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
namespace opensim::framework {
std::array<std::uint8_t,20> sha1(std::span<const std::uint8_t> input);
std::string sha1_hex(std::span<const std::uint8_t> input);
}
