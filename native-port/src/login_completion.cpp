// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_completion.hpp"
#include <stdexcept>
namespace opensim::services {
LoginCompletion::Result LoginCompletion::finish(PreparedLogin& prepared,
                                                const LoginCompletionRequest& request) {
    if (!dependencies_.launcher)
        throw std::runtime_error("Login agent launcher unavailable");
    AgentLaunchRequest launch;
    launch.destination =
        prepared.destination.region ? std::make_shared<GridRegion>(*prepared.destination.region) : nullptr;
    launch.gatekeeper = prepared.destination.gatekeeper
                            ? std::make_shared<GridRegion>(*prepared.destination.gatekeeper)
                            : nullptr;
    launch.account_scope = framework::Uuid(prepared.account.scope_id);
    launch.where = prepared.destination.where;
    launch.flags = prepared.destination.flags;
    launch.make_agent = [&](const GridRegion&, std::uint32_t code) {
        if (!dependencies_.factory)
            throw std::runtime_error("Login agent factory unavailable");
        LoginAgentDetails details;
        details.session = prepared.session;
        details.secure_session = prepared.secure_session;
        details.circuit = code;
        details.position = prepared.destination.position;
        details.ip_address = request.remote_ip;
        details.viewer = request.viewer;
        details.channel = request.channel;
        details.mac = request.mac;
        details.id0 = request.id0;
        return dependencies_.factory->make_agent(prepared.account, prepared.appearance.get(), details);
    };
    auto result = dependencies_.launcher->launch(launch);
    if (!result.circuit)
        return network::Value::object({{"reason", "key"}, {"message", result.reason}, {"login", "false"}});
    if (!dependencies_.logged_in)
        throw std::runtime_error("Grid-user login service unavailable");
    auto user = dependencies_.logged_in(prepared.account.principal_id);
    std::optional<std::vector<std::shared_ptr<LoginFriendInfo>>> friends =
        std::vector<std::shared_ptr<LoginFriendInfo>>{};
    if (dependencies_.friends)
        friends = dependencies_.friends(framework::Uuid(prepared.account.principal_id));
    auto message = welcome_message();
    std::string name = request.first.value_or("") + " " + request.last.value_or("");
    for (std::size_t pos = 0; (pos = message.find("<USERNAME>", pos)) != std::string::npos;
         pos += name.size())
        message.replace(pos, 10, name);
    if (!dependencies_.gestures)
        throw std::runtime_error("Inventory gesture service unavailable");
    auto gestures = dependencies_.gestures(framework::Uuid(prepared.account.principal_id));
    LoginResponseInput input;
    input.account = &prepared.account;
    input.circuit = result.circuit.get();
    input.grid_user = user ? &*user : nullptr;
    input.destination = result.destination.get();
    input.home = prepared.home ? &*prepared.home : nullptr;
    input.skeleton = LoginResponseLibrary::Folders{};
    for (const auto& folder : prepared.skeleton)
        input.skeleton->push_back(std::make_shared<LoginInventoryFolder>(folder));
    input.friends = std::move(friends);
    input.gestures = std::move(gestures);
    input.library = dependencies_.library;
    input.where = result.where;
    input.start_location = request.start;
    input.position = prepared.destination.position;
    input.look_at = prepared.destination.look_at;
    input.client_endpoint = request.client_endpoint;
    input.real_id = request.real_id;
    input.message = message;
    input.map_url = options_.map_url;
    input.profile_url = options_.profile_url;
    input.openid_url = options_.openid_url;
    input.search_url = options_.search_url;
    input.currency = options_.currency;
    input.dst_zone = options_.dst_zone;
    input.destinations_url = options_.destinations_url;
    input.avatars_url = options_.avatars_url;
    input.classified_fee = options_.classified_fee;
    input.max_agent_groups = options_.max_agent_groups;
    return std::make_shared<LoginResponse>(input, dependencies_.environment);
}
}
