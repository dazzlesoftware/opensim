#include "opensim/scene/region_runtime.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace opensim;

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        services::NullPresenceStore store;
        services::PresenceService presence(store);
        services::GridRegion region;
        region.id = framework::Uuid::random();
        region.name = "Admission restart";
        region.external_host = "127.0.0.1";
        auto listener = std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")});
        std::promise<void> entered, release;
        auto active = entered.get_future();
        auto released = release.get_future().share();
        bool block = true;
        scene::RegionPolicies policies;
        policies.generate_map_on_startup = false;
        policies.access = [](auto, const auto &, auto &) { return true; };
        policies.authorize = [](const auto &, auto &) { return true; };
        policies.verify_foreign_client = [&](const auto &, auto) {
            if (block) {
                entered.set_value();
                released.wait();
            }
            return true;
        };
        scene::RegionRuntime runtime(region, presence, policies);
        runtime.start(listener);
        const auto reserve = [&] {
            auto circuit = std::make_shared<framework::AgentCircuitData>();
            circuit->agent_id = framework::Uuid::random();
            circuit->session_id = framework::Uuid::random();
            circuit->secure_session_id = framework::Uuid::random();
            circuit->circuit_code = 47;
            circuit->verified_foreign_login = true;
            circuit->caps_path = framework::Uuid::random().str();
            circuit->start_position = {128, 128, 30};
            require(presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id),
                    "Could not establish presence");
            services::LoginTransferContext context;
            std::string reason;
            require(runtime.create_agent(nullptr, region, circuit, 128, context, reason), "Could not reserve circuit");
            presence.report(circuit->session_id, region.id);
            return circuit;
        };
        const auto connect = [&](const auto &circuit) {
            auto packet = network::lludp::make_packet("UseCircuitCode");
            packet.sequence = 1;
            auto &fields = packet.block("CircuitCode");
            fields["ID"] = circuit->agent_id;
            fields["SessionID"] = circuit->session_id;
            fields["Code"] = std::uint64_t{circuit->circuit_code};
            runtime.viewers().receive({"127.0.0.1", 12001}, network::lludp::encode_datagram(packet));
        };
        auto stale = reserve();
        // Declare the future before the guard so exceptions release the blocked
        // callback before an async future destructor joins the stopping thread.
        std::future<void> stopping;

        struct Release {
            std::promise<void> &promise;
            bool done = false;

            ~Release() {
                if (!done)
                    promise.set_value();
            }
        } guard{release};

        connect(stale);
        require(active.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                "Foreign verification never started on the background admission lane");
        stopping = std::async(std::launch::async, [&] { runtime.stop(); });
        require(stopping.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready,
                "Stop returned while verification still owned the old runtime");
        release.set_value();
        guard.done = true;
        require(stopping.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                "Stop did not finish after verification was released");
        stopping.get();
        require(!presence.agent(stale->session_id), "Stop retained old session presence");
        require(listener->routes.simple_keys().empty() && listener->routes.poll_keys().empty(),
                "Stop retained old capability routes");
        block = false;
        runtime.start(listener);
        require(!runtime.viewers().agent(stale->agent_id), "Old admission published into the restarted viewer");
        auto fresh = reserve();
        connect(fresh);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!runtime.viewers().agent(fresh->agent_id) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        require(bool(runtime.viewers().agent(fresh->agent_id)), "Restarted runtime could not admit a fresh session");
        require(!runtime.viewers().agent(stale->agent_id), "Late old generation restored its presence");
        runtime.stop();
        std::cout << "PASS: blocked admission shutdown, capability/presence cleanup and fresh generation restart\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
