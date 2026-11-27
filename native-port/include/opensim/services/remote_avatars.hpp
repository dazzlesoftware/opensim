// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/avatar_service.hpp"
#include "opensim/network/rest_forms.hpp"
#include "opensim/server/config.hpp"

namespace opensim::services {
std::unique_ptr<AvatarService> remote_avatar_service(AvatarStore &, const server::IniConfig &,
                                                     network::RestFormsRequest = {},
                                                     std::shared_ptr<network::WebHttpClients> = {});
}
