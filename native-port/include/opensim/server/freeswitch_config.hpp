// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/network/http_server.hpp"
#include <map>
#include <string>
#include <string_view>

namespace opensim::server {
std::map<std::string, std::string> parse_freeswitch_form(std::string_view body);
network::HttpResponse freeswitch_config(std::string_view body, std::string_view realm, std::string_view context);
} // namespace opensim::server
