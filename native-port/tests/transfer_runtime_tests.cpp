#include "teleport_event_probe.hpp"
#include "opensim/scene/region_runtime.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <future>
#include <thread>
#include <limits>
using namespace opensim;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Fixture {
    services::NullPresenceStore store;
    services::PresenceService presence{store};
    services::GridRegion region, remote;
    std::unique_ptr<scene::RegionRuntime> runtime;
    std::shared_ptr<framework::AgentCircuitData> circuit;
    std::function<void()> lookup, access, cancel_destination, logout;
    std::function<void(framework::Uuid)> create;
    std::function<void(int)> wait_sleep;
    std::function<bool()> update;
    float outbound_version;
    std::chrono::steady_clock::time_point cleanup_time{};
    std::string denial;
    framework::Uuid token;
    int creations = 0, cancellations = 0;
    std::uint32_t sequence = 0;

    Fixture(bool wait_for_callback = true, float version = .8f, bool presence_available = true,
            std::function<void(scene::RegionPolicies &)> configure = {})
        : outbound_version(version) {
        region.id = framework::Uuid::random();
        region.name = "Transfer binding";
        region.y = 256;
        remote = region;
        remote.id = framework::Uuid::random();
        remote.x += 256;
        scene::RegionPolicies policies;
        policies.background_admission = false;
        policies.generate_map_on_startup = false;
        policies.wait_for_teleport_callback = wait_for_callback;
        policies.transfer_cleanup_clock = [this] { return cleanup_time; };
        policies.transfer_wait_sleep = [this](int delay) {
            if (wait_sleep)
                wait_sleep(delay);
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        };
        policies.access = [](auto, const auto &, auto &) { return true; };
        policies.authorize = [](const auto &, auto &) { return true; };
        policies.teleport_destination = [this](auto) {
            if (lookup)
                lookup();
            return std::optional(remote);
        };
        policies.teleport_access = [this](auto &, const auto &, auto, bool, auto &context) {
            context.outbound_version = outbound_version;
            if (access)
                access();
            return denial;
        };
        policies.teleport_create = [this](const auto &, auto &, auto &, const auto &, auto, auto, auto &,
                                          const auto &callback, std::uint32_t, const auto *, auto &deferred_update,
                                          auto &) {
            ++creations;
            token = callback;
            if (create)
                create(callback);
            if (update)
                deferred_update = update;
            auto finish = network::lludp::make_packet("TeleportFinish");
            finish.block("Info")["TeleportFlags"] = std::uint64_t{16};
            return std::optional(std::move(finish));
        };
        if (presence_available)
            policies.teleport_arrived = [](const auto &, const auto &) { return false; };
        policies.cancel_teleport_destination = [this](auto &, auto, auto) {
            ++cancellations;
            if (cancel_destination)
                cancel_destination();
        };
        policies.logged_out = [this](const auto &) {
            if (logout)
                logout();
        };
        if (configure)
            configure(policies);
        runtime = std::make_unique<scene::RegionRuntime>(region, presence, policies);
        auto listener = std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")});
        runtime->start(listener);
        circuit = std::make_shared<framework::AgentCircuitData>();
        circuit->agent_id = framework::Uuid::random();
        circuit->session_id = framework::Uuid::random();
        circuit->secure_session_id = framework::Uuid::random();
        circuit->circuit_code = 42;
        circuit->caps_path = framework::Uuid::random().str();
        circuit->start_position = {128, 128, 30};
        presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id);
        services::LoginTransferContext context;
        std::string reason;
        require(runtime->create_agent(nullptr, region, circuit, 128, context, reason), "Reservation failed");
        auto use = network::lludp::make_packet("UseCircuitCode");
        use.block("CircuitCode")["ID"] = circuit->agent_id;
        use.block("CircuitCode")["SessionID"] = circuit->session_id;
        use.block("CircuitCode")["Code"] = std::uint64_t{42};
        send(std::move(use));
        complete_current();
    }

    ~Fixture() {
        lookup = {};
        access = {};
        create = {};
        cancel_destination = {};
        logout = {};
        runtime.reset();
    }

    void send(network::lludp::Packet packet) {
        packet.sequence = ++sequence;
        runtime->viewers().receive({"127.0.0.1", 12001}, network::lludp::encode_datagram(packet));
    }

    std::optional<scene::AgentTransferState> phase() {
        return runtime->agent_transfer_state(circuit->agent_id);
    }

    void teleport() {
        auto packet = network::lludp::make_packet("TeleportLocationRequest");
        packet.block("AgentData")["AgentID"] = circuit->agent_id;
        packet.block("AgentData")["SessionID"] = circuit->session_id;
        packet.block("Info")["RegionHandle"] = remote.handle();
        packet.block("Info")["Position"] = std::vector<double>{128, 128, 30};
        packet.block("Info")["LookAt"] = std::vector<double>{1, 0, 0};
        send(std::move(packet));
    }

    void cancel() {
        auto packet = network::lludp::make_packet("TeleportCancel");
        packet.block("Info")["AgentID"] = circuit->agent_id;
        packet.block("Info")["SessionID"] = circuit->session_id;
        send(std::move(packet));
    }

    void logout_viewer() {
        auto packet = network::lludp::make_packet("LogoutRequest");
        packet.block("AgentData")["AgentID"] = circuit->agent_id;
        packet.block("AgentData")["SessionID"] = circuit->session_id;
        send(std::move(packet));
    }

    void use_current() {
        auto use = network::lludp::make_packet("UseCircuitCode");
        use.block("CircuitCode")["ID"] = circuit->agent_id;
        use.block("CircuitCode")["SessionID"] = circuit->session_id;
        use.block("CircuitCode")["Code"] = std::uint64_t{circuit->circuit_code};
        send(std::move(use));
    }

    void complete_current(bool expected_root = true) {
        auto arrival = network::lludp::make_packet("CompleteAgentMovement");
        arrival.block("AgentData")["AgentID"] = circuit->agent_id;
        arrival.block("AgentData")["SessionID"] = circuit->session_id;
        arrival.block("AgentData")["CircuitCode"] = std::uint64_t{circuit->circuit_code};
        send(std::move(arrival));
        const auto viewer = runtime->viewers().agent(circuit->agent_id);
        require(viewer && viewer->root == expected_root, "Viewer movement completion has the wrong root state");
    }

    void reconnect_current() {
        use_current();
        complete_current();
    }
};

void normal(bool early) {
    Fixture f;
    bool logged_out = false;
    f.lookup = [&] { require(f.phase() == scene::AgentTransferState::Preparing, "Lookup outside Preparing"); };
    f.access = [&] { require(f.phase() == scene::AgentTransferState::Preparing, "Access outside Preparing"); };
    f.create = [&](auto token) {
        require(f.phase() == scene::AgentTransferState::Transferring, "Remote creation outside Transferring");
        if (early)
            require(f.runtime->confirm_teleport_arrival(f.circuit->agent_id, token), "Early arrival lost");
        if (early)
            require(f.runtime->confirm_teleport_arrival(f.circuit->agent_id, token),
                    "Duplicate early arrival rejected");
    };
    f.logout = [&] { logged_out = true; };
    f.teleport();
    require(f.phase() ==
                (early ? scene::AgentTransferState::ReceivedAtDestination : scene::AgentTransferState::Transferring),
            "Queued transfer phase mismatch");
    f.teleport();
    require(f.creations == 1, "Duplicate request started another transfer");
    require(!f.runtime->confirm_teleport_arrival(f.circuit->agent_id, framework::Uuid::random()),
            "Wrong token accepted");
    require(f.runtime->confirm_teleport_arrival(f.circuit->agent_id, f.token), "Arrival rejected");
    f.runtime->poll();
    require(!f.phase() && !f.runtime->viewers().agent(f.circuit->agent_id),
            "Completed transfer retained its source client or reservation");
    require(!logged_out && f.presence.agent(f.circuit->session_id).has_value(),
            "Moving to another region incorrectly logged out the global session");
}

void cancellation(bool during_create, bool throw_cleanup) {
    Fixture f;
    bool cleanup_seen = false;
    f.cancel_destination = [&] {
        cleanup_seen = f.phase() == scene::AgentTransferState::CleaningUp;
        require(f.phase() == scene::AgentTransferState::CleaningUp, "Destination cancellation outside cleanup");
        f.teleport();
        require(f.phase() == scene::AgentTransferState::CleaningUp && f.creations == 1,
                "A reentrant request replaced cleanup's reservation");
        if (throw_cleanup)
            throw std::runtime_error("Controlled cleanup failure");
    };
    if (during_create)
        f.create = [&](auto) { f.cancel(); };
    f.teleport();
    if (!during_create)
        f.cancel();
    require(cleanup_seen && !f.phase() && f.cancellations == 1,
            "Cancellation did not release its reservation exactly once after cleanup");
    const auto stale_token = f.token;
    f.create = {};
    f.cancel_destination = {};
    f.teleport();
    require(f.creations == 2 && f.phase() == scene::AgentTransferState::Transferring,
            "Cancelled session could not retry");
    require(!f.runtime->confirm_teleport_arrival(f.circuit->agent_id, stale_token) &&
                f.phase() == scene::AgentTransferState::Transferring,
            "An old callback changed the retried transfer");
}

void abortion(bool during_create) {
    Fixture f;
    bool abort_seen = false;
    if (during_create)
        f.create = [&](auto) {
            f.logout_viewer();
            abort_seen = f.phase() == scene::AgentTransferState::Aborting;
        };
    f.teleport();
    if (!during_create) {
        f.logout_viewer();
        abort_seen = f.phase() == scene::AgentTransferState::Aborting;
        f.runtime->poll();
    }
    require(abort_seen && !f.phase() && f.cancellations == 0,
            "Logout abort did not release the transfer without compensating cancellation");
}

void preparation_failure(bool cancel, bool throws) {
    Fixture f;
    if (cancel)
        f.access = [&] { f.cancel(); };
    else if (throws)
        f.access = [] { throw std::runtime_error("Controlled access failure"); };
    else
        f.denial = "Denied by fixture";
    f.teleport();
    require(!f.phase() && f.creations == 0, "Failed preparation left a transfer in transit");
    f.access = {};
    f.denial.clear();
    f.teleport();
    require(f.creations == 1 && f.phase() == scene::AgentTransferState::Transferring,
            "Preparation failure prevented retry");
}

void stopped_transfer() {
    Fixture f;
    f.teleport();
    require(f.phase() == scene::AgentTransferState::Transferring, "No pending transfer before shutdown");
    const auto token = f.token;
    f.runtime->stop();
    require(!f.phase() && !f.runtime->confirm_teleport_arrival(f.circuit->agent_id, token),
            "Stopped runtime retained an actionable transfer token");
}

void callback_setting(float version, bool legacy) {
    Fixture f(false, version, false);
    f.teleport();
    f.runtime->poll();
    require(f.phase().has_value() != legacy, "Callback setting was not scoped to the negotiated V1 protocol");
}

void legacy_wait(int arrival_after, char action = 'a') {
    std::atomic<int> sleeps = 0;
    std::atomic<bool> accepted = false, initial_state = false;
    Fixture f(true, .1f);
    f.wait_sleep = [&](int delay) {
        require(delay == 100, "Legacy wait used another interval");
        const auto count = ++sleeps;
        if (count != arrival_after)
            return;
        if (action == 'a')
            accepted = f.runtime->confirm_teleport_arrival(f.circuit->agent_id, f.token);
        else if (action == 'c') {
            f.cancel();
            initial_state = f.phase() == scene::AgentTransferState::Cancelling && f.cancellations == 0;
        } else {
            f.logout_viewer();
            initial_state = f.phase() == scene::AgentTransferState::Aborting;
        }
    };
    f.teleport();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (f.phase() && std::chrono::steady_clock::now() < deadline) {
        f.runtime->poll();
        std::this_thread::yield();
    }
    require(!f.phase(), "Legacy wait never completed");
    if (action == 'a') {
        const bool success = arrival_after > 0 && arrival_after < 399;
        require((arrival_after == 0 || accepted) && f.cancellations == (success ? 0 : 1),
                "Legacy arrival disagrees with the original final-poll semantics");
        require(sleeps == (arrival_after > 0 && arrival_after < 400 ? arrival_after : 400),
                "Legacy wait did not retain its exact source poll count");
    } else {
        require(initial_state && sleeps == 400 && f.cancellations == (action == 'c' ? 1 : 0),
                "Legacy cancellation/abort bypassed the original wait or compensation rules");
    }
}

void stop_legacy_wait() {
    std::promise<void> entered, release;
    auto entered_future = entered.get_future();
    auto released = release.get_future().share();
    Fixture f(true, .1f);
    f.wait_sleep = [&](int) {
        entered.set_value();
        released.wait();
    };
    std::future<void> stopped;

    struct Release {
        std::promise<void> &promise;
        bool done = false;

        ~Release() {
            if (!done)
                promise.set_value();
        }
    } guard{release};

    f.teleport();
    require(entered_future.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
            "Legacy wait worker did not start");
    stopped = std::async(std::launch::async, [&] { f.runtime->stop(); });
    require(stopped.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready,
            "Shutdown did not own the active legacy wait");
    release.set_value();
    guard.done = true;
    require(stopped.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
            "Legacy wait was not joined during shutdown");
    stopped.get();
    require(!f.phase(), "Shutdown retained a legacy transfer reservation");
}

void legacy_child_cleanup(bool far, bool renew) {
    Fixture f(false, .1f);
    if (far)
        f.remote.x += 4096;
    f.teleport();
    f.runtime->poll();
    const auto child = f.runtime->viewers().agent(f.circuit->agent_id);
    require(child && !child->root && child->crossed_child, "V1 cleanup did not make a child first");
    require(f.presence.agent(f.circuit->session_id).has_value(), "V1 child demotion deleted grid presence");
    if (!far) {
        require(!f.phase(), "Nearby V1 child retained transit state");
        f.cleanup_time += std::chrono::seconds(5);
        f.runtime->poll();
        require(f.runtime->viewers().agent(f.circuit->agent_id).has_value(), "Nearby V1 child was closed");
        return;
    }
    require(f.phase() == scene::AgentTransferState::CleaningUp, "Delayed V1 close released transit too early");
    if (renew) {
        auto circuit = std::make_shared<framework::AgentCircuitData>(*child->circuit);
        circuit->child = true;
        services::LoginTransferContext context;
        std::string reason;
        require(f.runtime->create_agent(&f.remote, f.region, circuit, 0, context, reason),
                "Child lease renewal failed");
    }
    f.cleanup_time += std::chrono::milliseconds(1999);
    f.runtime->poll();
    require(f.runtime->viewers().agent(f.circuit->agent_id).has_value() &&
                f.phase() == scene::AgentTransferState::CleaningUp,
            "V1 close ran before two seconds");
    f.cleanup_time += std::chrono::milliseconds(1);
    f.runtime->poll();
    require(!f.phase(), "V1 close did not release transit after its delay");
    require(f.runtime->viewers().agent(f.circuit->agent_id).has_value() == renew,
            "Delayed close did not honor the renewed child lease");
    if (renew) {
        require(f.runtime->close_agent_session(f.circuit->agent_id, f.circuit->session_id.str()),
                "The protected child lease was not consumed by the delayed close");
        require(!f.runtime->viewers().agent(f.circuit->agent_id), "Renewed child was not eventually removable");
        require(f.runtime->capabilities() &&
                    !f.runtime->capabilities()->capabilities().get_caps_for_user(f.circuit->circuit_code),
                "Closing a renewed child leaked its replacement capabilities");
    }
}

void legacy_deferred_update(int outcome) {
    Fixture f(false, .1f);
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    f.update = [&] {
        entered.set_value();
        gate.wait();
        if (outcome == 3)
            throw std::runtime_error("Controlled V1 update exception");
        return outcome != 1;
    };
    f.teleport();
    const bool started = entered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    const auto before = f.runtime->viewers().agent(f.circuit->agent_id);
    f.runtime->poll();
    if (outcome == 2)
        f.cancel();
    if (outcome == 4) {
        auto stopping = std::async(std::launch::async, [&] { f.runtime->stop(); });
        const bool waited = stopping.wait_for(std::chrono::milliseconds(25)) == std::future_status::timeout;
        release.set_value();
        stopping.get();
        require(started && before && before->root && waited && !f.phase(),
                "V1 shutdown failed to join its deferred update");
        return;
    }
    release.set_value();
    require(started && before && before->root, "V1 demoted its source before UpdateAgent completed");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (f.phase() && std::chrono::steady_clock::now() < deadline) {
        f.runtime->poll();
        std::this_thread::yield();
    }
    require(!f.phase(), "V1 deferred update did not release its reservation");
    const auto after = f.runtime->viewers().agent(f.circuit->agent_id);
    require(after && after->root == (outcome != 0), "V1 deferred update restored the wrong root state");
    require(f.cancellations == (outcome == 0 ? 0 : 1), "V1 deferred update cleanup count differs");
    require(tests::take_teleport_event(*f.runtime, f.circuit->agent_id).has_value() == (outcome == 0),
            "V1 sent TeleportFinish before successful update/cancellation checks");
}

void legacy_notification_delay() {
    Fixture f(false, .1f);
    f.remote.x += 4096;
    std::atomic<int> observed = 0;
    f.wait_sleep = [&](int milliseconds) {
        require(milliseconds == 200, "V1 used a different pre-update notification delay");
        ++observed;
    };
    f.update = [&] {
        require(observed == 1, "V1 UpdateAgent preceded EstablishAgentCommunication delay");
        return false;
    };
    f.teleport();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (f.phase() && std::chrono::steady_clock::now() < deadline) {
        f.runtime->poll();
        std::this_thread::yield();
    }
    require(!f.phase() && observed == 1 && f.cancellations == 1,
            "V1 notification delay or failed-update cleanup differs");
}

void receiver_root_wait(int outcome) {
    Fixture f;
    require(f.runtime->close_agent_session(f.circuit->agent_id, f.circuit->session_id.str()),
            "Could not release fixture endpoint before receiving transfer");
    auto incoming = std::make_shared<framework::AgentCircuitData>(*f.circuit);
    incoming->agent_id = framework::Uuid::random();
    incoming->session_id = framework::Uuid::random();
    incoming->secure_session_id = framework::Uuid::random();
    incoming->caps_path = framework::Uuid::random().str();
    incoming->child = false;
    ++incoming->circuit_code;
    require(f.presence.login(incoming->agent_id.str(), incoming->session_id, incoming->secure_session_id),
            "Incoming presence login failed");
    services::LoginTransferContext context;
    std::string reason;
    require(f.runtime->create_agent(&f.remote, f.region, incoming, 16, context, reason),
            "Incoming transfer reservation failed");
    f.circuit = incoming;
    f.use_current();
    const auto child = f.runtime->viewers().agent(incoming->agent_id);
    require(child && !child->root, "Incoming viewer did not connect as a child before data");
    auto data = std::make_shared<framework::AgentData>();
    data->region_id = f.remote.id;
    data->agent_id = incoming->agent_id;
    data->session_id = incoming->session_id;
    data->circuit_code = incoming->circuit_code;
    data->position = incoming->start_position;
    data->sender_wants_to_wait_for_root = true;
    if (outcome == 6 || outcome == 7)
        data->region_id = {};
    std::promise<void> sleeping, release;
    auto gate = release.get_future().share();
    std::atomic<int> sleeps = 0;
    f.wait_sleep = [&](int milliseconds) {
        require(milliseconds == 250, "Receiver used a non-source wait interval");
        const int count = ++sleeps;
        if (count == 1) {
            sleeping.set_value();
            if (outcome != 1)
                gate.wait();
        }
        if (outcome == 3 && count == 100) {
            f.circuit = incoming;
            f.complete_current();
        }
    };
    auto update = std::async(std::launch::async, [&] { return f.runtime->update_agent(f.region, data, context); });
    const bool started = sleeping.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    bool held = update.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout;
    if (outcome == 0 || outcome == 8 || outcome == 9) {
        f.circuit = incoming;
        f.complete_current();
        if (outcome == 8 || outcome == 9) {
            require(f.runtime->viewers().demote_after_crossing(incoming->agent_id, incoming->session_id).has_value(),
                    "Could not leave root before its sender observed arrival");
            if (outcome == 9) {
                auto replacement = std::make_shared<framework::AgentCircuitData>(*incoming);
                replacement->child = false;
                replacement->caps_path = framework::Uuid::random().str();
                require(f.runtime->create_agent(&f.remote, f.region, replacement, 16, context, reason),
                        "Could not renew a circuit after its confirmed arrival");
            }
        }
    } else if (outcome == 6 || outcome == 7) {
        f.complete_current(false);
        if (outcome == 7) {
            auto corrected = std::make_shared<framework::AgentData>(*data);
            corrected->region_id = f.remote.id;
            corrected->sender_wants_to_wait_for_root = false;
            require(f.runtime->update_agent(f.region, corrected, context), "Corrected origin update failed");
            f.complete_current();
        }
    } else if (outcome == 2) {
        f.runtime->stop();
    } else if (outcome == 5) {
        f.runtime->stop();
        f.runtime->start(std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")}));
        incoming->child = false;
        f.presence.login(incoming->agent_id.str(), incoming->session_id, incoming->secure_session_id);
        require(f.runtime->create_agent(&f.remote, f.region, incoming, 16, context, reason),
                "Could not reuse the circuit after receiver restart");
        f.use_current();
    } else if (outcome == 4) {
        require(f.runtime->close_agent_session(incoming->agent_id, incoming->session_id.str()),
                "Could not close the replaced receiving child");
        auto replacement = std::make_shared<framework::AgentCircuitData>(*incoming);
        replacement->session_id = framework::Uuid::random();
        replacement->secure_session_id = framework::Uuid::random();
        ++replacement->circuit_code;
        f.presence.login(replacement->agent_id.str(), replacement->session_id, replacement->secure_session_id);
        require(f.runtime->create_agent(nullptr, f.region, replacement, 128, context, reason),
                "Waiting update held the admission lock");
    }
    release.set_value();
    const bool result = update.get();
    require(started && (outcome == 1 || held), "Incoming update did not wait for root");
    require(result == (outcome == 0 || outcome == 3 || outcome == 7 || outcome == 8 || outcome == 9),
            "Incoming root wait returned the wrong result");
    if (outcome == 1 || outcome == 3 || outcome == 6)
        require(sleeps == 100, "Receiver did not preserve the 100-poll boundary");
    f.wait_sleep = {};
}

void receiver_presence_wait(int outcome) {
    Fixture f;
    require(f.runtime->close_agent_session(f.circuit->agent_id, f.circuit->session_id.str()),
            "Could not release the initial receiver endpoint");
    auto incoming = std::make_shared<framework::AgentCircuitData>(*f.circuit);
    incoming->agent_id = framework::Uuid::random();
    incoming->session_id = framework::Uuid::random();
    incoming->secure_session_id = framework::Uuid::random();
    incoming->caps_path = framework::Uuid::random().str();
    incoming->child = false;
    ++incoming->circuit_code;
    require(f.presence.login(incoming->agent_id.str(), incoming->session_id, incoming->secure_session_id),
            "Receiver presence session failed");
    services::LoginTransferContext context;
    std::string reason;
    require(f.runtime->create_agent(&f.remote, f.region, incoming, 16, context, reason),
            "Receiver circuit reservation failed");
    require(incoming->awaiting_transfer_data, "Transfer request was not reserved as a root arrival");
    f.circuit = incoming;
    auto data = std::make_shared<framework::AgentData>();
    data->region_id = f.remote.id;
    data->agent_id = incoming->agent_id;
    data->session_id = incoming->session_id;
    data->circuit_code = incoming->circuit_code;
    data->position = {91, 102, 30};
    std::promise<void> sleeping, release;
    const auto gate = release.get_future().share();
    std::atomic<int> sleeps = 0;
    f.wait_sleep = [&](int milliseconds) {
        require(milliseconds == 250, "Receiver presence wait interval changed");
        const int count = ++sleeps;
        if (count == 1) {
            sleeping.set_value();
            if (outcome != 1)
                gate.wait();
        }
        if (outcome == 3 && count == 120)
            f.use_current();
    };
    auto update = std::async(std::launch::async, [&] { return f.runtime->update_agent(f.region, data, context); });
    const bool started = sleeping.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    const bool held = update.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout;
    const bool unstaged = incoming->start_position == framework::Vector3{128, 128, 30};
    if (outcome == 0 || outcome == 5) {
        f.use_current();
        f.complete_current(false);
        if (outcome == 5) {
            require(f.runtime->close_agent_session(incoming->agent_id, incoming->session_id.str()),
                    "Could not close a new receiving child before data");
            require(!f.runtime->viewers().agent(incoming->agent_id),
                    "Closing a new receiving child incorrectly retained it as an old neighbour");
        }
    } else if (outcome == 2) {
        f.runtime->stop();
    } else if (outcome == 6) {
        f.runtime->stop();
        f.runtime->start(std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")}));
        incoming->child = false;
        f.presence.login(incoming->agent_id.str(), incoming->session_id, incoming->secure_session_id);
        require(f.runtime->create_agent(&f.remote, f.region, incoming, 16, context, reason),
                "Could not reserve the reused presence circuit after restart");
        f.use_current();
    } else if (outcome == 4) {
        auto replacement = std::make_shared<framework::AgentCircuitData>(*incoming);
        replacement->session_id = framework::Uuid::random();
        replacement->secure_session_id = framework::Uuid::random();
        ++replacement->circuit_code;
        f.presence.login(replacement->agent_id.str(), replacement->session_id, replacement->secure_session_id);
        require(f.runtime->create_agent(nullptr, f.region, replacement, 128, context, reason),
                "Waiting transfer prevented replacement admission");
    }
    release.set_value();
    const bool result = update.get();
    require(started && unstaged && (outcome == 1 || held), "Receiver staged data before finding a presence");
    require(result == (outcome == 0 || outcome == 3), "Receiver presence wait result differs");
    if (outcome == 1 || outcome == 3)
        require(sleeps == 120, "Receiver presence wait changed its 120-poll boundary");
    if (outcome == 0) {
        const auto root = f.runtime->viewers().agent(incoming->agent_id);
        require(root && root->root && root->position == data->position,
                "Early movement was not replayed after transfer data arrived");
    }
    if (outcome == 3)
        f.complete_current();
    f.wait_sleep = {};
}

void receiver_wire_identity(std::uint32_t code, bool wrong_session, int group_case = -1, int friend_case = -1) {
    const auto cache = std::make_shared<scene::FriendsOnlineCache>();
    const auto friend_a = framework::Uuid::random(), friend_b = framework::Uuid::random(),
               friend_c = framework::Uuid::random(), other_user = framework::Uuid::random();
    const auto selected_group = framework::Uuid::random();
    const auto destination_group = framework::Uuid::random();
    int group_refreshes = 0;
    Fixture f(true, .8f, true, [&](scene::RegionPolicies &policies) {
        if (friend_case >= 0 && friend_case != 4)
            policies.friends_online_cache = cache;
        if (group_case < 0)
            return;
        policies.inventory_group_member = [&](auto, auto group) { return group_case < 2 && group == selected_group; };
        policies.inventory_group_powers = [](auto, auto) { return std::uint64_t{0x1234}; };
        if (group_case != 5)
            policies.viewer_active_group = [&](auto, auto selection) -> std::optional<scene::ActiveViewerGroup> {
                require(!selection, "Incoming transfer changed the persisted active group");
                ++group_refreshes;
                return scene::ActiveViewerGroup{destination_group, "Destination group", "Destination title", 17};
            };
    });
    require(f.runtime->close_agent_session(f.circuit->agent_id, f.circuit->session_id.str()),
            "Could not release the wire identity fixture endpoint");
    auto incoming = std::make_shared<framework::AgentCircuitData>(*f.circuit);
    incoming->agent_id = framework::Uuid::random();
    incoming->session_id = framework::Uuid::random();
    incoming->secure_session_id = framework::Uuid::random();
    incoming->child = false;
    incoming->caps_path = framework::Uuid::random().str();
    ++incoming->circuit_code;
    f.presence.login(incoming->agent_id.str(), incoming->session_id, incoming->secure_session_id);
    services::LoginTransferContext context;
    std::string reason;
    require(f.runtime->create_agent(&f.remote, f.region, incoming, 16, context, reason),
            "Wire identity fixture could not reserve its receiver");
    f.circuit = incoming;
    f.use_current();
    group_refreshes = 0;
    if (friend_case >= 0) {
        const std::vector<framework::Uuid> original{friend_a};
        cache->update(other_user, original, true);
        if (friend_case == 0 || friend_case == 1)
            cache->update(incoming->agent_id, original, true);
    }
    auto data = std::make_shared<framework::AgentData>();
    data->region_id = f.remote.id;
    data->agent_id = incoming->agent_id;
    data->session_id = wrong_session ? framework::Uuid::random() : incoming->session_id;
    data->circuit_code = code;
    if (group_case >= 0) {
        data->active_group_id = selected_group;
        data->active_group_name = "Crossing group";
        if (group_case != 3)
            data->active_group_title = group_case == 1 ? "" : "Crossing title";
    }
    data->head_rotation = {0, .6f, 0, .8f};
    data->position = {91, 102, 30};
    if (friend_case == 1 || friend_case == 2 || friend_case == 4) {
        data->cached_friends_online = std::make_shared<framework::TransferList<framework::Uuid>>();
        if (friend_case != 2)
            for (const auto id : {friend_b, friend_b, friend_c})
                data->cached_friends_online->push_back(id);
    }
    data->appearance = std::make_shared<framework::AvatarAppearance>();
    data->appearance->serial = 73;
    data->appearance->hover_z = .23f;
    const auto baked_texture = framework::Uuid::random();
    data->appearance->texture->create_face(8)->texture_id(baked_texture);
    (*data->appearance->visual_params)[0] = 42;
    const auto expected_textures = data->appearance->texture->bakes_bytes();
    require(f.runtime->update_agent(f.region, data, context) == !wrong_session,
            "Receiver wire identity differs from original session validation");
    if (!wrong_session) {
        const auto child = f.runtime->viewers().agent(incoming->agent_id);
        if (group_case >= 0) {
            require(child->transfer_group_information == (group_case != 3),
                    "Receiver lost the null-versus-empty group title distinction");
            if (group_case < 2)
                require(child->active_group_override && child->active_group_id() == selected_group &&
                            group_refreshes == 0,
                        "Known crossing group was not applied to the child");
            else if (group_case == 2)
                require(child->active_group_id() == destination_group && group_refreshes == 1,
                        "Unknown group refresh was deferred until root arrival");
            else if (group_case == 3)
                require(!child->active_group_override && group_refreshes == 0,
                        "Missing group title triggered refresh before root arrival");
        }
        require(child && child->appearance() && child->appearance() != data->appearance &&
                    child->appearance()->serial == 73 && child->appearance()->hover_z == .23f &&
                    child->appearance()->texture->bakes_bytes() == expected_textures &&
                    child->appearance()->texture->get_face(8)->texture_id() == baked_texture &&
                    child->appearance()->visual_params != data->appearance->visual_params &&
                    *child->appearance()->visual_params == *data->appearance->visual_params,
                "Receiver did not clone the full update appearance into the child");
        f.complete_current();
        if (group_case >= 0) {
            const auto root = f.runtime->viewers().agent(incoming->agent_id);
            require(!root->transfer_group_information,
                    "Arrival retained the transient crossing group-information flag");
            if (group_case < 2)
                require(root->active_group_override && root->active_group_id() == selected_group &&
                            root->active_group_override->name == "Crossing group" &&
                            root->active_group_override->title == *data->active_group_title &&
                            root->active_group_override->powers == 0x1234 && group_refreshes == 0,
                        "Known crossing group lost its title or destination powers");
            else if (group_case == 5)
                require(root->active_group_id() != selected_group && group_refreshes == 0,
                        "Unknown group was accepted without a group module");
            else
                require(root->active_group_override && root->active_group_id() == destination_group &&
                            root->active_group_override->title == "Destination title" && group_refreshes == 1,
                        "Unknown or absent transfer group did not refresh exactly once");
        }
        require(f.runtime->viewers().agent(incoming->agent_id)->head_rotation == data->head_rotation,
                "Receiver lost transferred head rotation during root completion");
        data->appearance->serial = 99;
        require(f.runtime->viewers().agent(incoming->agent_id)->appearance()->serial == 73,
                "Receiver appearance aliases the transferred payload");
        if (friend_case >= 0) {
            framework::AgentData outgoing;
            f.runtime->snapshot_attachments(*f.runtime->viewers().agent(incoming->agent_id), outgoing);
            if (friend_case < 3) {
                const auto expected = friend_case == 0 ? 1u : friend_case == 1 ? 3u : 0u;
                require(outgoing.cached_friends_online && outgoing.cached_friends_online->size() == expected,
                        "Transfer friend cache lost merge, duplicate or null/empty semantics");
                require(f.runtime->notify_friend_status(incoming->agent_id, friend_b, true, incoming->session_id) &&
                            cache->contains(incoming->agent_id, friend_b),
                        "Online notification did not update the transfer cache");
                require(
                    !f.runtime->notify_friend_status(incoming->agent_id, friend_b, false, framework::Uuid::random()) &&
                        cache->contains(incoming->agent_id, friend_b),
                    "Stale friend notification changed the current cache");
                require(f.runtime->notify_friend_status(incoming->agent_id, friend_b, false, incoming->session_id) &&
                            !cache->contains(incoming->agent_id, friend_b),
                        "Offline notification did not update the transfer cache");
            } else
                require(!outgoing.cached_friends_online, "Absent or disabled friend cache produced a transfer list");
            require(f.runtime->close_agent_session(incoming->agent_id, incoming->session_id.str()) &&
                        !cache->get(incoming->agent_id) && cache->contains(other_user, friend_a),
                    "Client close did not clear only its own friend-online cache");
        }
    } else
        require(incoming->awaiting_transfer_data && incoming->start_position == framework::Vector3{128, 128, 30},
                "Wrong-session update changed the receiving child");
}

void departure_creation_return(float version = .8f) {
    Fixture f(true, version);
    f.update = [] { return true; };
    f.create = [&](auto) {
        require(f.runtime->viewers().set_transfer_child(f.circuit->agent_id, f.circuit->session_id, true),
                "Could not model source departure before creation reply");
        auto returning = std::make_shared<framework::AgentCircuitData>(*f.circuit);
        returning->child = true;
        returning->caps_path = framework::Uuid::random().str();
        services::LoginTransferContext context;
        std::string reason;
        require(f.runtime->create_agent(&f.remote, f.region, returning, 16, context, reason),
                "Return reservation failed while old creation was completing");
        auto data = std::make_shared<framework::AgentData>();
        data->agent_id = returning->agent_id;
        data->session_id = returning->session_id;
        data->region_id = f.remote.id;
        data->position = {92, 103, 30};
        require(f.runtime->update_agent(f.region, data, context), "Early return data was rejected");
        f.circuit = returning;
        f.complete_current();
    };
    f.teleport();
    const auto returned = f.runtime->viewers().agent(f.circuit->agent_id);
    require(returned && returned->root && !returned->transfer_child && returned->circuit == f.circuit,
            "Old departure setup demoted a newer root arrival");
    require(!tests::take_teleport_event(*f.runtime, f.circuit->agent_id),
            "Old departure sent its handoff to a newer root arrival");
    f.create = {};
}

void late_departure_return(bool success = true, float version = .8f) {
    Fixture f(true, version);
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    f.update = [&] {
        entered.set_value();
        gate.wait();
        return success;
    };
    f.teleport();
    try {
        require(entered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready,
                "Old departure update did not start");
        if (version < .2f)
            require(f.runtime->viewers().set_transfer_child(f.circuit->agent_id, f.circuit->session_id, true),
                    "Could not model legacy departure before its update reply");
        auto returning = std::make_shared<framework::AgentCircuitData>(*f.circuit);
        returning->child = false;
        returning->caps_path = framework::Uuid::random().str();
        services::LoginTransferContext context;
        std::string reason;
        require(f.runtime->create_agent(&f.remote, f.region, returning, 16, context, reason),
                "Return reservation failed during old departure");
        auto data = std::make_shared<framework::AgentData>();
        data->agent_id = returning->agent_id;
        data->session_id = returning->session_id;
        data->region_id = f.remote.id;
        data->position = {92, 103, 30};
        require(f.runtime->update_agent(f.region, data, context), "Return data failed during old departure");
        f.circuit = returning;
        f.complete_current();
        f.runtime->poll();
        require(!f.phase(), "New root remained blocked by an older pending departure");
    } catch (...) {
        release.set_value();
        throw;
    }
    release.set_value();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (f.phase() && std::chrono::steady_clock::now() < deadline) {
        f.runtime->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto returned = f.runtime->viewers().agent(f.circuit->agent_id);
    require(!f.phase() && returned && returned->root && !returned->transfer_child && returned->circuit == f.circuit &&
                f.cancellations == 0 && returned->position == framework::Vector3{92, 103, 30},
            "Late departure completion demoted a newer return arrival");
    require(!f.runtime->close_agent_session(f.circuit->agent_id, f.circuit->session_id.str(), true),
            "Child-only cleanup removed the renewed root");
    require(!f.runtime->close_agent_session(f.circuit->agent_id, framework::Uuid::random().str()),
            "Wrong-session cleanup removed the renewed root");
    const bool first_close = f.runtime->close_agent_session(f.circuit->agent_id, f.circuit->session_id.str());
    if (version < .2f) {
        // V1 MakeChildAgent follows UpdateAgent and its arrival callback. The
        // old scene presence was still root during this early return, so
        // NewUserConnection did not renew a protected child lease.
        require(first_close && !f.runtime->viewers().agent(f.circuit->agent_id),
                "Legacy root close was refused without a renewed child lease");
        return;
    }
    require(!first_close, "V2 close did not consume the protected lease retained through root arrival");
    const auto protected_root = f.runtime->viewers().agent(f.circuit->agent_id);
    require(protected_root && protected_root->root && protected_root->circuit == f.circuit &&
                f.presence.agent(f.circuit->session_id).has_value(),
            "Refused close changed the renewed root or global session");
    require(f.runtime->close_agent_session(f.circuit->agent_id, f.circuit->session_id.str()) &&
                !f.runtime->viewers().agent(f.circuit->agent_id),
            "Consumed root lease prevented a subsequent close");
}

void renewed_child_registration_return() {
    Fixture f;
    f.update = [] { return true; };
    f.teleport();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (f.phase() && std::chrono::steady_clock::now() < deadline) {
        f.runtime->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto retained = f.runtime->viewers().agent(f.circuit->agent_id);
    require(!f.phase() && retained && !retained->root, "Departure did not retain its nearby child");
    auto child = std::make_shared<framework::AgentCircuitData>(*retained->circuit);
    child->child = true;
    services::LoginTransferContext context;
    std::string reason;
    require(f.runtime->create_agent(&f.remote, f.region, child, 0, context, reason),
            "Neighbour child registration did not renew");
    auto returning = std::make_shared<framework::AgentCircuitData>(*retained->circuit);
    returning->child = false;
    returning->caps_path = framework::Uuid::random().str();
    for (int credential : {0, 1, 2}) {
        auto stale = std::make_shared<framework::AgentCircuitData>(*returning);
        if (credential == 0)
            stale->session_id = framework::Uuid::random();
        else if (credential == 1)
            stale->secure_session_id = framework::Uuid::random();
        else
            ++stale->circuit_code;
        require(!f.runtime->create_agent(&f.remote, f.region, stale, 16, context, reason),
                "Renewed registration admitted mismatched return credentials");
    }
    require(f.runtime->create_agent(&f.remote, f.region, returning, 16, context, reason),
            "Return rejected a credential-matched neighbour registration");
    auto data = std::make_shared<framework::AgentData>();
    data->agent_id = returning->agent_id;
    data->session_id = returning->session_id;
    data->region_id = f.remote.id;
    data->position = {93, 104, 30};
    require(f.runtime->update_agent(f.region, data, context), "Renewed return update failed");
    f.circuit = returning;
    f.complete_current();
}

void admission_login_marker(std::uint32_t flags, bool child, bool missing_source) {
    Fixture f;
    require(f.runtime->close_agent_session(f.circuit->agent_id, f.circuit->session_id.str()),
            "Could not release login-marker endpoint");
    auto incoming = std::make_shared<framework::AgentCircuitData>(*f.circuit);
    incoming->agent_id = framework::Uuid::random();
    incoming->session_id = framework::Uuid::random();
    incoming->secure_session_id = framework::Uuid::random();
    incoming->caps_path = framework::Uuid::random().str();
    incoming->child = child;
    ++incoming->circuit_code;
    f.presence.login(incoming->agent_id.str(), incoming->session_id, incoming->secure_session_id);
    services::LoginTransferContext context;
    std::string reason;
    require(f.runtime->create_agent(missing_source ? nullptr : &f.remote, f.region, incoming, flags, context, reason),
            "Login-marker reservation failed");
    const bool real_login = (flags & (128u | (1u << 30))) == 128u;
    require(incoming->awaiting_transfer_data == !real_login,
            "Admission confused a real login with a transfer or HG login");
    f.circuit = incoming;
    f.use_current();
    f.complete_current(real_login);
    if (!real_login) {
        auto data = std::make_shared<framework::AgentData>();
        data->agent_id = incoming->agent_id;
        data->session_id = incoming->session_id;
        data->region_id = f.remote.id;
        data->position = {94, 105, 30};
        require(f.runtime->update_agent(f.region, data, context), "Login-marker transfer update failed");
        const auto root = f.runtime->viewers().agent(incoming->agent_id);
        require(root && root->root && root->position == data->position,
                "Transfer/HG movement was not replayed after full data");
    }
}

void login_friend_cache() {
    const auto cache = std::make_shared<scene::FriendsOnlineCache>();
    const auto friend_id = framework::Uuid::random();
    Fixture f(true, .8f, true, [&](scene::RegionPolicies &policies) {
        policies.friends_online_cache = cache;
        policies.online_friends = [=](auto) { return std::vector{friend_id, friend_id}; };
    });
    framework::AgentData outgoing;
    f.runtime->snapshot_attachments(*f.runtime->viewers().agent(f.circuit->agent_id), outgoing);
    require(outgoing.cached_friends_online && outgoing.cached_friends_online->size() == 1 &&
                outgoing.cached_friends_online->get(0) == friend_id,
            "Login online friends were not cached for the next transfer");
}

void v2_update_phase(int outcome) {
    Fixture f;
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    f.update = [&] {
        entered.set_value();
        gate.wait();
        if (outcome == 2)
            throw std::runtime_error("Controlled update failure");
        return outcome == 0;
    };
    f.teleport();
    const bool started = entered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    const auto during = f.runtime->viewers().agent(f.circuit->agent_id);
    f.runtime->poll(); // A blocked UpdateAgent must not block the region owner loop.
    f.cancel();
    const bool cancelled_early = f.cancellations != 0;
    release.set_value();
    require(started, "Deferred UpdateAgent did not start");
    require(during && !during->root && during->transfer_child, "V2 did not mark the source child before UpdateAgent");
    require(!cancelled_early, "V2 cancelled the destination while UpdateAgent was still running");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (f.phase() && std::chrono::steady_clock::now() < deadline) {
        f.runtime->poll();
        std::this_thread::yield();
    }
    require(!f.phase(), "Deferred UpdateAgent completion did not release the transfer");
    const auto after = f.runtime->viewers().agent(f.circuit->agent_id);
    if (outcome == 0) {
        require(after && !after->root && after->crossed_child && !after->transfer_child && f.cancellations == 0,
                "Successful nearby V2 update did not retain its child");
        require(f.presence.agent(f.circuit->session_id).has_value(), "V2 departure lost the global session");
    } else {
        require(after && after->root && !after->transfer_child && f.cancellations == 1,
                "Failed V2 update did not restore the root and close the destination");
    }
}

void v2_distant_cleanup(bool callback, bool renewal, bool late_renewal = false) {
    Fixture f;
    f.remote.x += 4096;
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    f.update = [&] {
        entered.set_value();
        gate.wait();
        return true;
    };
    const auto renew = [&] {
        auto renewed = std::make_shared<framework::AgentCircuitData>(*f.circuit);
        renewed->child = true;
        renewed->caps_path = framework::Uuid::random().str();
        services::LoginTransferContext context;
        std::string reason;
        require(f.runtime->create_agent(&f.remote, f.region, renewed, 0, context, reason),
                "V2 distant child lease renewal failed");
    };
    f.teleport();
    const bool started = entered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    if (renewal && !late_renewal)
        renew(); // Admission protection consults the state machine, before its V2 reset.
    release.set_value();
    require(started, "Distant V2 update did not start");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (f.phase() && std::chrono::steady_clock::now() < deadline) {
        f.runtime->poll();
        std::this_thread::yield();
    }
    const auto child = f.runtime->viewers().agent(f.circuit->agent_id);
    require(!f.phase() && child && child->crossed_child && !child->root,
            "V2 did not reset its state before retaining the distant child");
    if (renewal && late_renewal)
        renew();
    if (callback) {
        require(!f.runtime->confirm_teleport_arrival(f.circuit->agent_id, framework::Uuid::random()),
                "Wrong V2 cleanup callback token accepted");
        require(f.runtime->confirm_teleport_arrival(f.circuit->agent_id, f.token),
                "V2 callback after state reset was lost");
    }
    const int intervals = callback ? 1 : 60;
    for (int interval = 0; interval < intervals; ++interval) {
        f.cleanup_time += std::chrono::milliseconds(249);
        f.runtime->poll();
        require(f.runtime->viewers().agent(f.circuit->agent_id).has_value(),
                "V2 child closed before its 250ms observation");
        f.cleanup_time += std::chrono::milliseconds(1);
        f.runtime->poll();
        if (interval + 1 < intervals)
            require(f.runtime->viewers().agent(f.circuit->agent_id).has_value(),
                    "V2 distant child closed before sixty polls without a callback");
    }
    const bool protected_close = renewal && !late_renewal;
    require(f.runtime->viewers().agent(f.circuit->agent_id).has_value() == protected_close,
            "V2 delayed close did not respect the renewed child lease");
    if (protected_close && !callback) {
        require(f.runtime->confirm_teleport_arrival(f.circuit->agent_id, f.token),
                "Timeout close refusal lost its still-in-transit callback");
        f.cleanup_time += std::chrono::milliseconds(250);
        f.runtime->poll();
        require(f.runtime->viewers().agent(f.circuit->agent_id).has_value(),
                "A late callback retried the already-refused close");
    }
    if (protected_close)
        require(f.runtime->close_agent_session(f.circuit->agent_id, f.circuit->session_id.str()),
                "V2 consumed lease did not permit a subsequent close");
    require(f.presence.agent(f.circuit->session_id).has_value(), "V2 child close logged out the travelling session");
}

void stop_v2_update() {
    Fixture f;
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    f.update = [&] {
        entered.set_value();
        gate.wait();
        return true;
    };
    f.teleport();
    const bool started = entered.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    auto stopping = std::async(std::launch::async, [&] { f.runtime->stop(); });
    const bool waited = stopping.wait_for(std::chrono::milliseconds(25)) == std::future_status::timeout;
    release.set_value();
    stopping.get();
    require(started && waited && !f.phase(), "Shutdown did not join the V2 update worker and clear transit");
}

void removing_presence_reentry() {
    Fixture f;
    for (int iteration = 0; iteration < 8; ++iteration) {
        const auto previous = f.circuit;
        auto next = std::make_shared<framework::AgentCircuitData>(*previous);
        next->session_id = framework::Uuid::random();
        next->secure_session_id = framework::Uuid::random();
        next->caps_path = framework::Uuid::random().str();
        ++next->circuit_code;
        require(f.presence.login(next->agent_id.str(), next->session_id, next->secure_session_id),
                "Replacement presence could not be established");
        auto closing = std::async(std::launch::async, [&] {
            return f.runtime->close_agent_session(previous->agent_id, previous->session_id.str());
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (f.runtime->viewers().transport().connection(previous->agent_id) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        services::LoginTransferContext context;
        std::string reason;
        require(f.runtime->create_agent(nullptr, f.region, next, 128, context, reason),
                "Reentry failed while the old presence was being removed");
        require(closing.wait_for(std::chrono::seconds(3)) == std::future_status::ready && closing.get(),
                "Reentry prevented old client removal from completing");
        f.circuit = next;
        f.reconnect_current();
    }
}
} // namespace

int main() {
    try {
        normal(false);
        normal(true);
        cancellation(false, false);
        cancellation(false, true);
        cancellation(true, false);
        preparation_failure(false, false);
        preparation_failure(false, true);
        preparation_failure(true, false);
        abortion(false);
        abortion(true);
        stopped_transfer();
        callback_setting(.1f, true);
        callback_setting(.19999999f, true);
        callback_setting(.2f, false);
        callback_setting(.8f, false);
        callback_setting(std::numeric_limits<float>::quiet_NaN(), true);
        for (int count : {0, 1, 398, 399, 400})
            legacy_wait(count);
        legacy_wait(1, 'c');
        legacy_wait(1, 'b');
        stop_legacy_wait();
        legacy_child_cleanup(false, false);
        legacy_child_cleanup(true, false);
        legacy_child_cleanup(true, true);
        removing_presence_reentry();
        for (int outcome : {0, 1, 2})
            v2_update_phase(outcome);
        stop_v2_update();
        v2_distant_cleanup(true, false);
        v2_distant_cleanup(false, false);
        v2_distant_cleanup(true, true);
        v2_distant_cleanup(false, true);
        v2_distant_cleanup(true, true, true);
        for (int outcome : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9})
            receiver_root_wait(outcome);
        for (int outcome : {0, 1, 2, 3, 4})
            legacy_deferred_update(outcome);
        legacy_notification_delay();
        for (int outcome : {0, 1, 2, 3, 4, 5, 6})
            receiver_presence_wait(outcome);
        receiver_wire_identity(0, false);
        receiver_wire_identity(9999, false);
        receiver_wire_identity(0, true);
        for (int group_case : {0, 1, 2, 3, 5})
            receiver_wire_identity(0, false, group_case);
        for (int friend_case : {0, 1, 2, 3, 4})
            receiver_wire_identity(0, false, -1, friend_case);
        login_friend_cache();
        late_departure_return();
        late_departure_return(false);
        late_departure_return(true, .1f);
        late_departure_return(false, .1f);
        departure_creation_return();
        departure_creation_return(.1f);
        renewed_child_registration_return();
        for (auto flags : {0u, 16u, 128u, 1u << 30, 128u | (1u << 30), 128u | 8192u})
            for (bool child : {false, true})
                for (bool missing_source : {false, true})
                    admission_login_marker(flags, child, missing_source);
        std::cout
            << "PASS: live transfer preparation, duplicate exclusion, early arrival, cancellation, cleanup and retry\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
