// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/agent_circuit.hpp"
namespace opensim::services {
std::shared_ptr<framework::AgentCircuitData::ServiceUrls> decode_account_service_urls(std::string_view text);
std::string encode_account_service_urls(const std::shared_ptr<framework::AgentCircuitData::ServiceUrls>& urls);
}
