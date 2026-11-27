// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/http_server.hpp"
#include "opensim/network/rpc.hpp"
#include <optional>

namespace opensim::network {
std::string inflate_gzip(std::string_view bytes);
std::string decode_rpc_request_body(const HttpRequest &request);
std::optional<Value> simulation_json_body(const HttpRequest &request);
} // namespace opensim::network
