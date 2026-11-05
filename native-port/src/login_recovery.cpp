// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_recovery.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/network/nwc_http.hpp"
#include "opensim/network/web_http_clients.hpp"
#include "opensim/framework/text.hpp"
#include <chrono>
namespace opensim::services {
network::Value duplicate_login_message(const framework::Uuid& scope, const framework::Uuid& user,
                                       std::uint32_t timestamp, const std::string& key) {
    using network::Value;
    auto value = Value::object({{"from_agent_id", "6571e388-6218-4574-87db-f9379718315e"},
                                {"from_agent_session", std::string(framework::ZeroUuid)},
                                {"to_agent_id", user.str()},
                                {"im_session_id", std::string(framework::ZeroUuid)},
                                {"timestamp", std::to_string(timestamp)},
                                {"from_agent_name", "GRID"},
                                {"message", "New login detected"},
                                {"from_group", "FALSE"},
                                {"parent_estate_id", "0"},
                                {"position_x", "0"},
                                {"position_y", "0"},
                                {"position_z", "0"},
                                {"region_id", scope.str()},
                                {"binary_bucket", "AA=="},
                                {"dialog", "+g=="},
                                {"offline", "AA=="},
                                {"region_handle", Value(std::int32_t{0})}});
    if (!key.empty())
        value.map.emplace("message_key", Value(key));
    return value;
}
bool send_grid_instant_message(const std::string& url, const network::Value& message,
                               const std::shared_ptr<network::WebHttpClients>& clients) {
    try {
        auto parameters = std::make_shared<network::NwcXmlRpcRequest::Parameters>();
        parameters->push_back(std::make_shared<network::Value>(message));
        auto request = std::make_shared<network::NwcXmlRpcRequest>(u"grid_instant_message", parameters);
        auto response =
            network::NwcXmlRpcHttpTransport(clients).send(request, framework::utf16_replacing(url), 10000);
        if (!response || !response->value)
            return false;
        const auto& value = *response->value;
        return value.kind == network::Value::Kind::Map &&
               value.get("success").kind == network::Value::Kind::String &&
               value.get("success").text == "TRUE";
    } catch (const std::exception&) {
        return false;
    }
}
bool recover_duplicate_session(GridService& grid, GridUserService& users, const framework::Uuid& scope,
                               const framework::Uuid& user, const GridUserInfo& info,
                               const std::string& message_key,
                               std::shared_ptr<network::WebHttpClients> clients,
                               std::string_view grid_user_id) {
    const auto region = grid.by_id(scope, info.last_region);
    if (!region)
        return false;
    const auto uri = region->server_uri();
    if (uri.empty())
        return false;
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    if (!clients) {
        clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory());
        clients->setup(false, false, nullptr, 32);
    }
    send_grid_instant_message(
        uri, duplicate_login_message(scope, user, static_cast<std::uint32_t>(seconds), message_key), clients);
    // The original ignores delivery success and clears stale grid-user state.
    users.logged_out(grid_user_id.empty() ? user.str() : std::string(grid_user_id), {}, info.last_region,
                     info.last_position, info.last_look_at);
    return true;
}
}
