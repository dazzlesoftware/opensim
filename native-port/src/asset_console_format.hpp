// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace opensim::server {
std::string asset_console_flags(std::int32_t flags);
void asset_console_hex(std::span<const std::uint8_t> data,
                       const std::function<void(const std::string&)>& output);
}
