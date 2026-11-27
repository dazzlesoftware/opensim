// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/server/config.hpp"
#include "opensim/services/grid_region.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace opensim::server {

std::uint32_t read_region_uint32(const std::string &text, std::string_view key);
std::vector<IniConfig> load_local_region_settings(const IniConfig &config);
services::GridRegion describe_local_region(const IniConfig &settings);

} // namespace opensim::server
