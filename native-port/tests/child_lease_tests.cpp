#include "opensim/scene/region_runtime.hpp"
#include "opensim/framework/agent_data.hpp"
#include <iostream>
#include <stdexcept>

using namespace opensim;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

void verify(bool in_transit) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = framework::Uuid::random();
    region.name = "Child lease";
    auto remote = region;
    remote.id = framework::Uuid::random();
    remote.x += 256;
    scene::RegionPolicies policies;
    policies.background_admission = false;
    policies.generate_map_on_startup = false;
    policies.access = [](auto, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.teleport_destination = [remote](auto) { return std::optional(remote); };
    policies.teleport_access = [](auto &, const auto &, auto, bool, auto &) { return std::string{}; };
    policies.teleport_create = [](const auto &, auto &, auto &, const auto &, auto, auto, auto &, const auto &,
                                  std::uint32_t, const auto *, auto &, auto &) {
        auto finish = network::lludp::make_packet("TeleportFinish");
        finish.block("Info")["TeleportFlags"] = std::uint64_t{16};
        return std::optional(std::move(finish));
    };
    auto listener = std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")});
    scene::RegionRuntime runtime(region, presence, policies);
    runtime.start(listener);
    auto circuit = std::make_shared<framework::AgentCircuitData>();
    circuit->agent_id = framework::Uuid::random();
    circuit->session_id = framework::Uuid::random();
    circuit->secure_session_id = framework::Uuid::random();
    circuit->circuit_code = 42;
    circuit->caps_path = framework::Uuid::random().str();
    circuit->start_position = {128, 128, 30};
    require(presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id), "Presence login");
    services::LoginTransferContext context;
    std::string reason;
    require(runtime.create_agent(nullptr, region, circuit, 128, context, reason), "Initial reservation failed");
    std::uint32_t sequence = 0;
    const auto send = [&](network::lludp::Packet packet) {
        packet.sequence = ++sequence;
        runtime.viewers().receive({"127.0.0.1", 12001}, network::lludp::encode_datagram(packet));
    };
    auto use = network::lludp::make_packet("UseCircuitCode");
    use.block("CircuitCode")["ID"] = circuit->agent_id;
    use.block("CircuitCode")["SessionID"] = circuit->session_id;
    use.block("CircuitCode")["Code"] = std::uint64_t{42};
    send(std::move(use));
    auto arrival = network::lludp::make_packet("CompleteAgentMovement");
    arrival.block("AgentData")["AgentID"] = circuit->agent_id;
    arrival.block("AgentData")["SessionID"] = circuit->session_id;
    arrival.block("AgentData")["CircuitCode"] = std::uint64_t{42};
    send(std::move(arrival));
    const auto root = runtime.viewers().agent(circuit->agent_id);
    require(root && root->root, "Viewer did not become root");
    if (in_transit) {
        auto teleport = network::lludp::make_packet("TeleportLocationRequest");
        teleport.block("AgentData")["AgentID"] = circuit->agent_id;
        teleport.block("AgentData")["SessionID"] = circuit->session_id;
        teleport.block("Info")["RegionHandle"] = remote.handle();
        teleport.block("Info")["Position"] = std::vector<double>{128, 128, 30};
        teleport.block("Info")["LookAt"] = std::vector<double>{1, 0, 0};
        send(std::move(teleport));
    }
    require(bool(runtime.viewers().demote_after_crossing(circuit->agent_id, circuit->session_id)), "Demotion failed");
    require(circuit->teleport_flags == 0, "Child retained its initial-login teleport flags");
    auto renewal = std::make_shared<framework::AgentCircuitData>(*circuit);
    renewal->child = false;
    require(runtime.create_agent(&remote, region, renewal, 128, context, reason), "Child renewal failed");
    auto data = std::make_shared<framework::AgentData>();
    data->region_id = remote.id;
    data->agent_id = circuit->agent_id;
    data->session_id = circuit->session_id;
    data->circuit_code = circuit->circuit_code;
    data->position = circuit->start_position;
    require(runtime.update_agent(region, data, context), "Renewed child transfer data failed");
    require(!runtime.close_agent_session(circuit->agent_id, framework::Uuid::random().str()),
            "Wrong session closed the renewed child");
    if (in_transit) {
        require(!runtime.close_agent_session(circuit->agent_id, circuit->session_id.str()),
                "First stale close ignored the renewed child lease");
        require(bool(runtime.viewers().agent(circuit->agent_id)), "Protected close removed the viewer");
        const auto protected_arrival = runtime.viewers().agent(circuit->agent_id);
        require(protected_arrival->origin_region_id == remote.id && protected_arrival->crossing_arrival_ready,
                "Refused protected close discarded active arrival metadata");
    }
    require(runtime.close_agent_session(circuit->agent_id, circuit->session_id.str()), "Next close did not succeed");
    const auto retained = runtime.viewers().agent(circuit->agent_id);
    require(retained && retained->origin_region_id.is_zero() && !retained->crossing_arrival_ready &&
                !retained->transfer_group_information,
            "Cancelled return retained stale arrival metadata");
    require(runtime.close_agent_session(circuit->agent_id, circuit->session_id.str()), "Retained child did not close");
    require(!runtime.viewers().agent(circuit->agent_id), "Closed child remained visible");
    runtime.stop();
}
} // namespace

int main() {
    try {
        verify(false);
        verify(true);
        std::cout << "PASS: ordinary renewal closes immediately; in-transit renewal consumes one protected close\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
