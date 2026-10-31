// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/service_protocol.hpp"
#include <functional>
namespace opensim::network {
using RestFormsRequest=std::function<std::string(const std::string& url,const std::string& body)>;
RestFormsRequest rest_forms_transport(std::optional<std::pair<std::string,std::string>> auth={});
}
