// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/login_orchestration.hpp"
namespace opensim::network {
class WebHttpClients;
}
namespace opensim::services {
bool send_grid_instant_message(const std::string& url, const network::Value& message,
                               const std::shared_ptr<network::WebHttpClients>& clients);
// Focused SendAgentGodKillToRegion port; general IM service routing is separate.
network::Value duplicate_login_message(const framework::Uuid& scope, const framework::Uuid& user,
                                       std::uint32_t timestamp, const std::string& message_key);
bool recover_duplicate_session(GridService& grid, GridUserService& users, const framework::Uuid& scope,
                               const framework::Uuid& user, const GridUserInfo& info,
                               const std::string& message_key,
                               std::shared_ptr<network::WebHttpClients> clients = {},
                               std::string_view grid_user_id = {});
}
