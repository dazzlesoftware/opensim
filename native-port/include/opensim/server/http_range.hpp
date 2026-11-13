// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace opensim::server {
bool try_parse_http_range(const std::optional<std::string>& header, std::int32_t& start, std::int32_t& end);
}
