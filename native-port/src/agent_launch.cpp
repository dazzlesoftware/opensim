// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/agent_launch.hpp"
#include "opensim/framework/http_uri.hpp"
#include <random>
#include <stdexcept>
namespace opensim::services {
AgentLauncher::AgentLauncher(GridService *grid, ISimulationLogin *local, ISimulationLogin *remote,
                             IUserAgentLogin *user_agent, std::string uri, CircuitCode code)
    : grid_(grid), local_(local), remote_(remote), user_agent_(user_agent), gatekeeper_uri_(std::move(uri)),
      circuit_code_(std::move(code)) {
    if (!circuit_code_)
        circuit_code_ = []() {
            static thread_local std::mt19937 random(std::random_device{}());
            return std::uniform_int_distribution<std::uint32_t>(0, 2147483646u)(random);
        };
}
void AgentLauncher::resolve_gatekeeper() {
    try {
        framework::HttpUri uri(gatekeeper_uri_);
        if (!uri.valid())
            return;
        gatekeeper_host_ = uri.host.find(':') == std::string::npos ? uri.host : "[" + uri.host + "]";
        gatekeeper_port_ = uri.port;
    } catch (const std::exception &) {
    }
}
bool AgentLauncher::directly(ISimulationLogin &connector, GridRegion &region,
                             const std::shared_ptr<framework::AgentCircuitData> &circuit, std::uint32_t flags,
                             std::string &reason) {
    LoginTransferContext context;
    std::vector<framework::Uuid> features;
    if (!connector.query_access(region, circuit->agent_id, std::nullopt, true, circuit->start_position,
                                features, context, reason))
        return false;
    return connector.create_agent(nullptr, region, circuit, flags, context, reason);
}
AgentLaunchResult AgentLauncher::launch(const AgentLaunchRequest &request) {
    AgentLaunchResult result;
    result.where = request.where;
    auto destination = request.destination, gatekeeper = request.gatekeeper;
    std::shared_ptr<framework::AgentCircuitData> circuit;
    bool success;
    if (!user_agent_) {
        auto *connector = local_ ? local_ : remote_;
        if (!connector)
            return result;
        if (!destination)
            throw std::invalid_argument("Null launch destination");
        circuit = request.make_agent(*destination, circuit_code_());
        if (!circuit)
            throw std::invalid_argument("MakeAgent returned null");
        success = directly(*connector, *destination, circuit, request.flags, result.reason);
        if (!success && grid_) {
            const auto fallbacks = grid_->fallback(request.account_scope, destination->x, destination->y);
            for (const auto &fallback : fallbacks) {
                auto region = std::make_shared<GridRegion>(fallback);
                success = directly(*connector, *region, circuit, request.flags | 4096u, result.reason);
                if (success) {
                    result.where = "safe";
                    destination = std::move(region);
                    break;
                }
            }
        }
    } else {
        if (!gatekeeper) {
            std::lock_guard lock(gatekeeper_mutex_);
            if (gatekeeper_host_.empty())
                resolve_gatekeeper();
            if (!destination)
                throw std::invalid_argument("Null launch destination");
            gatekeeper = std::make_shared<GridRegion>(destination->legacy_copy());
            gatekeeper->external_host = gatekeeper_host_;
            gatekeeper->http_port = static_cast<std::uint32_t>(gatekeeper_port_);
            gatekeeper->set_server_uri(gatekeeper_uri_);
        }
        if (!destination)
            throw std::invalid_argument("Null launch destination");
        circuit = request.make_agent(*destination, circuit_code_());
        if (!circuit)
            throw std::invalid_argument("MakeAgent returned null");
        circuit->teleport_flags |= request.flags;
        success = user_agent_->login_agent_to_grid_owned(nullptr, circuit, gatekeeper, destination, true,
                                                         result.reason);
        if (!success && grid_) {
            const auto fallbacks = grid_->fallback(request.account_scope, destination->x, destination->y);
            for (const auto &fallback : fallbacks) {
                auto region = std::make_shared<GridRegion>(fallback);
                success = user_agent_->login_agent_to_grid_owned(nullptr, circuit, gatekeeper, region, true,
                                                                 result.reason);
                if (success) {
                    result.where = "safe";
                    destination = std::move(region);
                    break;
                }
            }
        }
    }
    result.destination = std::move(destination);
    if (success)
        result.circuit = std::move(circuit);
    return result;
}
}
