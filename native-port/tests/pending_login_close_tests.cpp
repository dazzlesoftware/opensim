// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace opensim;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Fixture {
    services::NullPresenceStore store;
    services::PresenceService presence{store};
    services::GridRegion region, source;
    std::shared_ptr<server::RegistryCapabilityListener> listener;
    int closed_events = 0;
    std::unique_ptr<scene::RegionRuntime> runtime;

    Fixture(std::function<void(scene::RegionPolicies &)> configure = {}) {
        region.id = framework::Uuid::random();
        region.name = "Pending login close";
        source = region;
        source.id = framework::Uuid::random();
        scene::RegionPolicies policies;
        policies.background_admission = false;
        policies.generate_map_on_startup = false;
        policies.access = [](auto, const auto &, auto &) { return true; };
        policies.authorize = [](const auto &, auto &) { return true; };
        if (configure)
            configure(policies);
        runtime = std::make_unique<scene::RegionRuntime>(region, presence, policies);
        listener = std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")});
        runtime->start(listener);
        runtime->capabilities()->shared_events()->on_client_closed.add(
            {{this, "PendingClose.ClientClosed"}, [&](auto, auto) { ++closed_events; }});
    }

    auto reserve(std::uint32_t flags, bool child) {
        auto circuit = std::make_shared<framework::AgentCircuitData>();
        circuit->agent_id = framework::Uuid::random();
        circuit->session_id = framework::Uuid::random();
        circuit->secure_session_id = framework::Uuid::random();
        circuit->circuit_code = 47;
        circuit->caps_path = framework::Uuid::random().str();
        circuit->child = child;
        circuit->start_position = {128, 128, 30};
        circuit->service_urls = std::make_shared<framework::AgentCircuitData::ServiceUrls>();
        (*circuit->service_urls)["HomeURI"] = std::string("http://home.invalid");
        require(presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id),
                "Could not create test session presence");
        services::LoginTransferContext context;
        std::string reason;
        require(runtime->create_agent(flags == 16 ? &source : nullptr, region, circuit, flags, context, reason),
                "Could not reserve pre-viewer circuit");
        return circuit;
    }

    void use(const auto &circuit) {
        auto packet = network::lludp::make_packet("UseCircuitCode");
        packet.sequence = 1;
        auto &data = packet.block("CircuitCode");
        data["ID"] = circuit->agent_id;
        data["SessionID"] = circuit->session_id;
        data["Code"] = std::uint64_t{circuit->circuit_code};
        runtime->viewers().receive({"127.0.0.1", 12001}, network::lludp::encode_datagram(packet));
    }
};

void before_viewer(std::uint32_t flags, bool child) {
    Fixture f;
    const auto circuit = f.reserve(flags, child);
    require(!f.runtime->viewers().agent(circuit->agent_id) && f.runtime->profile_service_urls(circuit->agent_id) &&
                !f.listener->routes.simple_keys().empty(),
            "Circuit/capabilities were not reserved before viewer arrival");
    require(!f.runtime->close_agent_session(circuit->agent_id, framework::Uuid::random().str()) &&
                f.runtime->profile_service_urls(circuit->agent_id),
            "Wrong-session close removed a pending circuit");
    if (flags != 0)
        require(!f.runtime->close_agent_session(circuit->agent_id, circuit->session_id.str(), true) &&
                    f.runtime->profile_service_urls(circuit->agent_id),
                "Child-only close removed a pending root login/transfer");
    require(!f.runtime->close_agent_session(circuit->agent_id, circuit->session_id.str()),
            "Close without a scene presence reported an active client closed");
    require(!f.runtime->profile_service_urls(circuit->agent_id), "Close retained the pending circuit");
    require(f.listener->routes.simple_keys().empty() && f.listener->routes.poll_keys().empty(),
            "Close retained pending capability routes");
    require(f.presence.agent(circuit->session_id).has_value(), "No-client close logged out the global session");
    require(f.closed_events == 0, "No-client close emitted a scene client-closed event");
    f.use(circuit);
    require(!f.runtime->viewers().agent(circuit->agent_id), "Cancelled circuit admitted a late viewer");
    f.runtime->stop();
}

void during_verification() {
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    Fixture f([&](scene::RegionPolicies &policies) {
        policies.background_admission = true;
        policies.verify_foreign_client = [&](const auto &, auto) {
            entered.set_value();
            gate.wait();
            return true;
        };
    });
    const auto circuit = f.reserve(128, false);
    circuit->verified_foreign_login = true;

    struct Release {
        std::promise<void> &promise;
        bool released = false;

        ~Release() {
            if (!released)
                promise.set_value();
        }
    } guard{release};

    f.use(circuit);
    require(entered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready,
            "Background verifier did not start");
    require(!f.runtime->close_agent_session(circuit->agent_id, circuit->session_id.str()),
            "Pending verifier was reported as an active scene client");
    require(!f.runtime->profile_service_urls(circuit->agent_id), "In-flight verification kept the cancelled circuit");
    release.set_value();
    guard.released = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (f.runtime->viewers().users_logging_in() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(!f.runtime->viewers().users_logging_in() && !f.runtime->viewers().agent(circuit->agent_id) &&
                !f.runtime->viewers().transport().connection(circuit->agent_id),
            "Late verification published a viewer after its circuit was cancelled");
    require(f.listener->routes.simple_keys().empty() && f.listener->routes.poll_keys().empty(),
            "Late verifier restored cancelled capability routes");
    require(f.closed_events == 0, "Cancelled verifier emitted a scene client-closed event");
    f.runtime->stop();
}
} // namespace

int main() {
    try {
        before_viewer(128, false);
        before_viewer(0, true);
        before_viewer(16, false);
        during_verification();
        std::cout << "PASS: pending login/child/transfer close, auth guards, capabilities and late verification\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
