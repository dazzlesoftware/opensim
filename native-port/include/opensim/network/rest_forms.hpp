// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/service_protocol.hpp"
#include <functional>
#include <memory>

namespace opensim::network {
class WebHttpClients;
using RestFormsRequest = std::function<std::string(const std::string &url, const std::string &body)>;
RestFormsRequest rest_forms_transport(std::optional<std::pair<std::string, std::string>> auth = {});
RestFormsRequest rest_forms_transport(std::optional<std::pair<std::string, std::string>> auth,
                                      std::int32_t timeout_milliseconds, bool raw_response = false);
RestFormsRequest rest_forms_transport(std::shared_ptr<WebHttpClients> clients,
                                      std::optional<std::pair<std::string, std::string>> auth = {});
RestFormsRequest rest_forms_transport(std::shared_ptr<WebHttpClients> clients,
                                      std::optional<std::pair<std::string, std::string>> auth,
                                      std::int32_t timeout_milliseconds, bool raw_response = false);
} // namespace opensim::network
