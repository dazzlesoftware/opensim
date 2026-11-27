// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct ReleaseGate {
    std::promise<void> &release;
    bool done = false;

    void open() {
        if (!done) {
            done = true;
            release.set_value();
        }
    }

    ~ReleaseGate() {
        open();
    }
};

void overlap() {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = framework::Uuid::random();
    region.name = "Deterministic Removing overlap";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    auto listener = std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")});
    auto old = std::make_shared<framework::AgentCircuitData>();
    old->agent_id = framework::Uuid::random();
    old->session_id = framework::Uuid::random();
    old->secure_session_id = framework::Uuid::random();
    old->circuit_code = 42;
    old->caps_path = framework::Uuid::random().str();
    old->start_position = {128, 128, 30};
    std::promise<void> observed, release;
    const auto released = release.get_future().share();
    auto observation = observed.get_future();
    std::atomic<int> mode = 1, notifications = 0;
    std::atomic<bool> correct_identity = false;
    scene::RegionPolicies policies;
    policies.background_admission = false;
    policies.generate_map_on_startup = false;
    policies.access = [](auto, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.removing_presence_observer = [&](auto agent, auto session, auto state) {
        ++notifications;
        if (mode == 2)
            throw std::runtime_error("intentional observer failure");
        if (mode != 1)
            return;
        correct_identity =
            agent == old->agent_id && session == old->session_id && state == services::PresenceState::removing;
        observed.set_value();
        released.wait();
    };
    scene::RegionRuntime runtime(region, presence, policies);
    const auto endpoint = runtime.start(listener);
    std::atomic<unsigned> old_handshakes = 0, fresh_handshakes = 0;
    network::UdpSocket old_viewer({"127.0.0.1", 0}, [&](const auto &, auto bytes) {
        const auto packet = network::lludp::decode_datagram(bytes);
        if (packet.spec->name == "RegionHandshake")
            ++old_handshakes;
    });
    network::UdpSocket fresh_viewer({"127.0.0.1", 0}, [&](const auto &, auto bytes) {
        const auto packet = network::lludp::decode_datagram(bytes);
        if (packet.spec->name == "RegionHandshake")
            ++fresh_handshakes;
    });
    old_viewer.start();
    fresh_viewer.start();
    const auto reserve = [&](const auto &circuit) {
        require(presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id),
                "Could not establish session presence");
        services::LoginTransferContext context;
        std::string reason;
        return runtime.create_agent(nullptr, region, circuit, 128, context, reason);
    };
    const auto wait = [&](const auto &condition, const char *message) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!condition() && std::chrono::steady_clock::now() < deadline) {
            runtime.poll();
            std::this_thread::sleep_for(5ms);
        }
        require(condition(), message);
    };
    const auto connect = [&](network::UdpSocket &viewer, const auto &circuit) {
        auto use = network::lludp::make_packet("UseCircuitCode");
        use.sequence = 1;
        use.block("CircuitCode")["ID"] = circuit->agent_id;
        use.block("CircuitCode")["SessionID"] = circuit->session_id;
        use.block("CircuitCode")["Code"] = std::uint64_t(circuit->circuit_code);
        require(viewer.send_to(endpoint, network::lludp::encode_datagram(use)), "Could not send real UDP circuit");
        wait(
            [&] {
                const auto agent = runtime.viewers().agent(circuit->agent_id);
                return agent && agent->circuit->session_id == circuit->session_id;
            },
            "Fresh UDP circuit did not publish the matching viewer");
        auto complete = network::lludp::make_packet("CompleteAgentMovement");
        complete.sequence = 2;
        complete.block("AgentData")["AgentID"] = circuit->agent_id;
        complete.block("AgentData")["SessionID"] = circuit->session_id;
        complete.block("AgentData")["CircuitCode"] = std::uint64_t(circuit->circuit_code);
        require(viewer.send_to(endpoint, network::lludp::encode_datagram(complete)), "Could not send movement");
        wait(
            [&] {
                const auto agent = runtime.viewers().agent(circuit->agent_id);
                return agent && agent->root && agent->circuit->session_id == circuit->session_id;
            },
            "Authenticated viewer did not become root");
    };
    require(reserve(old), "Initial reservation failed");
    connect(old_viewer, old);
    wait([&] { return old_handshakes != 0; }, "Initial viewer received no actual RegionHandshake");
    require(runtime.capabilities()->capabilities().get_caps_for_user(old->circuit_code) != nullptr,
            "Initial circuit has no capability object");
    auto fresh = std::make_shared<framework::AgentCircuitData>(*old);
    fresh->session_id = framework::Uuid::random();
    fresh->secure_session_id = framework::Uuid::random();
    fresh->caps_path = framework::Uuid::random().str();
    fresh->circuit_code = 43;
    // Futures precede the release guard: any assertion first unblocks cleanup,
    // then joins async work, instead of hanging in an async future destructor.
    std::future<bool> closing, creating, lock_probe;
    ReleaseGate gate{release};
    closing = std::async(std::launch::async,
                         [&] { return runtime.close_agent_session(old->agent_id, old->session_id.str()); });
    require(observation.wait_for(3s) == std::future_status::ready, "Actual Removing observer never reached");
    require(correct_identity && notifications == 1, "Observer did not see the matching real Removing actor");
    const auto old_connection = runtime.viewers().transport().connection(old->agent_id);
    require(old_connection && old_connection->session == old->session_id,
            "Gate must precede transport removal, rather than observe an already completed logout");
    creating = std::async(std::launch::async, [&] { return reserve(fresh); });
    require(creating.wait_for(1200ms) == std::future_status::timeout,
            "Fresh CreateAgent completed while the old actor was deterministically Removing");
    lock_probe = std::async(std::launch::async, [&] {
        return runtime.close_agent_session(framework::Uuid::random(), framework::Uuid::random().str());
    });
    require(lock_probe.wait_for(500ms) == std::future_status::ready && !lock_probe.get(),
            "Waiting CreateAgent or the observer retained the region reservation mutex");
    mode = 0;
    gate.open();
    require(closing.wait_for(5s) == std::future_status::ready && closing.get(), "Actual old removal did not finish");
    require(creating.wait_for(5s) == std::future_status::ready && creating.get(),
            "Fresh CreateAgent failed after actual Removing completion");
    connect(fresh_viewer, fresh);
    wait([&] { return fresh_handshakes != 0; }, "Fresh UDP endpoint received no actual RegionHandshake");
    auto *caps = runtime.capabilities();
    require(caps->capabilities().get_caps_for_user(old->circuit_code) == nullptr,
            "Old circuit capabilities survived completion");
    require(caps->capabilities().get_caps_for_user(fresh->circuit_code) != nullptr,
            "Fresh capabilities disappeared during old-session cleanup");
    require(!presence.agent(old->session_id) && presence.agent(fresh->session_id),
            "Old removal did not preserve the distinct fresh presence session");
    require(!runtime.close_agent_session(old->agent_id, old->session_id.str()),
            "Late old-session close was allowed to remove a fresh session");
    require(runtime.viewers().agent(fresh->agent_id)->circuit == fresh &&
                caps->capabilities().get_caps_for_user(fresh->circuit_code) != nullptr,
            "Late old-session callback changed the fresh viewer/capability identity");
    mode = 2;
    require(runtime.close_agent_session(fresh->agent_id, fresh->session_id.str()),
            "Observer exception prevented actual transport removal");
    require(!runtime.viewers().agent(fresh->agent_id) && !caps->capabilities().get_caps_for_user(fresh->circuit_code),
            "Observer exception prevented viewer/capability cleanup");
    old_viewer.stop();
    fresh_viewer.stop();
    runtime.stop();
}
} // namespace

int main() {
    try {
        overlap();
        std::cout << "PASS: deterministic Removing session-close overlap, unlocked admission wait, fresh UDP "
                     "handshake/caps, stale-session refusal and observer exception cleanup\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
