// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/service_protocol.hpp"

namespace opensim::services {
network::ServiceXmlValue read_legacy_offline_im(std::string_view xml);
std::string write_legacy_offline_im(const network::ServiceXmlValue& message);
}
