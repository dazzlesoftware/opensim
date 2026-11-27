// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/account_store.hpp"
#include "opensim/network/rest_forms.hpp"
#include "opensim/server/config.hpp"

namespace opensim::services {
AccountBackend remote_account_backend(const server::IniConfig &, network::RestFormsRequest = {},
                                      std::shared_ptr<network::WebHttpClients> = {});
}
