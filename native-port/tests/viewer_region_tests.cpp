// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/viewer_region.hpp"
#include "opensim/network/udp_socket.hpp"
#include <condition_variable>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <future>
#include <thread>
using namespace opensim;
using namespace framework;
using namespace network;
using namespace scene;

namespace {
int failures = 0;

void check(bool ok, const char *name) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

struct Fixture {
    World world{Uuid::random()};
    AgentCircuitManager circuits;
    std::shared_ptr<AgentCircuitData> circuit = std::make_shared<AgentCircuitData>();

    Fixture() {
        circuit->agent_id = Uuid::random();
        circuit->session_id = Uuid::random();
        circuit->circuit_code = 42;
        circuit->start_position = {128, 128, 70};
        circuits.add_new_circuit(circuit);
        world.terrain().clear_land();
    }

    lludp::Packet packet(const char *name, std::uint32_t seq) {
        auto p = lludp::make_packet(name);
        p.sequence = seq;
        if (p.blocks.contains("AgentData")) {
            auto &d = p.block("AgentData");
            d["AgentID"] = circuit->agent_id;
            d["SessionID"] = circuit->session_id;
        }
        return p;
    }

    lludp::Packet use() {
        auto p = packet("UseCircuitCode", 1);
        auto &d = p.block("CircuitCode");
        d["ID"] = circuit->agent_id;
        d["SessionID"] = circuit->session_id;
        d["Code"] = std::uint64_t(42);
        return p;
    }
};

void npc_presence_test() {
    Fixture f;
    ViewerRegion region(f.world, f.circuits, [](const auto &, auto) { return true; }, {});
    auto npc = std::make_shared<AgentCircuitData>();
    npc->agent_id = Uuid::random();
    npc->first_name = "NPC";
    npc->last_name = "Fixture";
    npc->start_position = {20, 30, 40};
    check(region.add_npc(npc, {}) && !region.add_npc(npc, {}), "NPC admission rejects a duplicate ID");
    const auto avatar = region.agent(npc->agent_id);
    check(avatar && avatar->npc_owner && avatar->npc_owner->is_zero() && avatar->root && !avatar->connected &&
              avatar->position == npc->start_position && avatar->local_id != 0 && region.root_agent_names().empty() &&
              !f.circuits.get_agent_circuit_data(npc->agent_id),
          "NPC is a root presence without a viewer circuit");
    check(region.remove_npc(npc->agent_id) && !region.agent(npc->agent_id) && !region.remove_npc(npc->agent_id),
          "NPC removal clears its presence");
    check(region.add_npc(npc, f.circuit->agent_id), "NPC admission reuses the freed slot");
    region.close();
    check(!region.agent(npc->agent_id), "Region shutdown clears NPCs without transport callbacks");
}

void admission_test() {
    Fixture f;
    std::vector<lludp::Packet> replies;
    ViewerRegion region(f.world, f.circuits,
                        [&](const auto &, auto bytes) {
                            replies.push_back(lludp::decode_datagram(bytes));
                            return true;
                        },
                        {});
    region.receive({"127.0.0.1", 12345}, lludp::encode_datagram(f.use()));
    check(replies.size() == 1 && replies[0].spec->name == "PacketAck" && !region.agent(f.circuit->agent_id),
          "registered credentials require actual scene admission");
}

void background_admission_test() {
    auto owner = std::make_shared<Fixture>();
    std::mutex replies_mutex;
    std::vector<lludp::Packet> replies;
    std::promise<void> entered;
    auto active = entered.get_future();
    std::promise<void> release;
    auto released = release.get_future().share();
    auto region = std::make_shared<ViewerRegion>(
        owner->world, owner->circuits,
        [&](const auto &, auto bytes) {
            std::lock_guard lock(replies_mutex);
            replies.push_back(lludp::decode_datagram(bytes));
            return true;
        },
        [&](const auto &, const auto &, const auto &) {
            entered.set_value();
            released.wait();
            return true;
        });
    std::future<void> closing;

    struct ReleaseOnExit {
        std::promise<void> &release;
        bool done = false;

        ~ReleaseOnExit() {
            if (!done)
                release.set_value();
        }
    } guard{release};

    region->enable_background_admission(owner);
    region->receive({"127.0.0.1", 12345}, lludp::encode_datagram(owner->use()));
    check(active.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
          "background host admission starts without blocking packet reception");
    auto invalid = owner->use();
    invalid.sequence = 2;
    invalid.block("CircuitCode")["Code"] = std::uint64_t{99};
    region->receive({"127.0.0.1", 12346}, lludp::encode_datagram(invalid));
    {
        std::lock_guard lock(replies_mutex);
        check(std::count_if(replies.begin(), replies.end(),
                            [](const auto &packet) { return packet.spec->name == "PacketAck"; }) == 2,
              "another endpoint receives its ACK while admission is blocked");
    }
    closing = std::async(std::launch::async, [&] { region->close(); });
    check(closing.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready,
          "shutdown waits for active host admission");
    release.set_value();
    guard.done = true;
    check(closing.wait_for(std::chrono::seconds(3)) == std::future_status::ready,
          "shutdown completes after host admission returns");
    closing.get();
    check(!region->agent(owner->circuit->agent_id), "shutdown does not retain the late admitted presence");
    std::weak_ptr<ViewerRegion> weak_region = region;
    region.reset();
    check(weak_region.expired(), "background dispatcher does not retain the viewer");
}

void repeated_circuit_handshake_test() {
    for (auto flags : {0u, 16u, 128u, 1u << 30}) {
        Fixture f;
        f.circuit->teleport_flags = flags;
        std::vector<lludp::Packet> replies;
        int admissions = 0;
        ViewerRegion region(
            f.world, f.circuits,
            [&](const auto &, auto bytes) {
                replies.push_back(lludp::decode_datagram(bytes));
                return true;
            },
            [&](const auto &, const auto &, const auto &) {
                ++admissions;
                return true;
            });
        const UdpEndpoint endpoint{"127.0.0.1", 12345};
        region.receive(endpoint, lludp::encode_datagram(f.use()));
        region.poll();
        const auto handshakes = [&] {
            return std::count_if(replies.begin(), replies.end(),
                                 [](const auto &packet) { return packet.spec->name == "RegionHandshake"; });
        };
        const auto initial = handshakes();
        for (std::uint32_t sequence = 2; sequence != 5; ++sequence) {
            auto repeated = f.use();
            repeated.sequence = sequence;
            const auto before = replies.size();
            region.receive(endpoint, lludp::encode_datagram(repeated));
            check(replies.size() == before, "established circuit ignores unreliable repeated circuit packets");
        }
        check(handshakes() == initial && admissions == 1,
              "repeated circuit neither restarts handshake nor readmits existing viewer");
    }
}

void arrival_handshake_order_test() {
    for (auto flags : {0u, 16u, 128u, 1u << 30}) {
        for (auto crossing : {0u, 1u, 3u, 7u}) {
            Fixture f;
            f.circuit->teleport_flags = flags;
            std::vector<lludp::Packet> replies;
            ViewerRegion region(
                f.world, f.circuits,
                [&](const auto &, auto bytes) {
                    replies.push_back(lludp::decode_datagram(bytes));
                    return true;
                },
                [](const auto &, const auto &, const auto &) { return true; });
            const UdpEndpoint endpoint{"127.0.0.1", 12345};
            region.receive(endpoint, lludp::encode_datagram(f.use()));
            region.poll();
            const auto count = [&] {
                return std::count_if(replies.begin(), replies.end(),
                                     [](const auto &p) { return p.spec->name == "RegionHandshake"; });
            };
            check(count() == (flags == 0 ? 1 : 0), "only ordinary child admission sends an initial handshake");
            replies.clear();
            check(region.stage_crossing_arrival(f.circuit, Uuid::random(), false, std::nullopt, nullptr,
                                                static_cast<std::uint8_t>(crossing)),
                  "arrival handshake fixture stages full transfer data");
            auto movement = f.packet("CompleteAgentMovement", 2);
            movement.block("AgentData")["CircuitCode"] = std::uint64_t{42};
            region.receive(endpoint, lludp::encode_datagram(movement));
            region.poll();
            const auto handshake = std::find_if(replies.begin(), replies.end(),
                                                [](const auto &p) { return p.spec->name == "RegionHandshake"; });
            const auto complete = std::find_if(replies.begin(), replies.end(),
                                               [](const auto &p) { return p.spec->name == "AgentMovementComplete"; });
            check(complete != replies.end() && region.agent(f.circuit->agent_id)->root,
                  "accepted movement promotes the arrival to root");
            const bool expects_handshake = flags != 0 || crossing == 0;
            check(count() == (expects_handshake ? 1 : 0),
                  "only crossing updates without teleport flags suppress the root arrival handshake");
            check(!expects_handshake || handshake < complete, "root handshake precedes movement completion");
            const auto before = count();
            movement.sequence = 3;
            region.receive(endpoint, lludp::encode_datagram(movement));
            region.poll();
            check(count() == before, "duplicate movement and polling do not repeat root handshake");
        }
    }
}

void group_completion_cleanup_test(bool fail) {
    Fixture f;
    f.circuit->awaiting_transfer_data = true;
    ViewerRegion region(
        f.world, f.circuits, [](const auto &, auto) { return true; },
        [](const auto &, const auto &, const auto &) { return true; });
    const UdpEndpoint endpoint{"127.0.0.1", 12345};
    region.receive(endpoint, lludp::encode_datagram(f.use()));
    bool observed = false;
    region.callbacks({},
                     [&](const ViewerAgent &avatar) {
                         observed = avatar.transfer_group_information &&
                                    region.agent(avatar.circuit->agent_id)->transfer_group_information;
                         if (fail)
                             throw std::runtime_error("Controlled completion failure");
                     },
                     {}, {}, {});
    auto arrival = f.packet("CompleteAgentMovement", 2);
    arrival.block("AgentData")["CircuitCode"] = std::uint64_t{42};
    region.receive(endpoint, lludp::encode_datagram(arrival));
    bool threw = false;
    try {
        region.stage_crossing_arrival(f.circuit, Uuid::random(), true,
                                      ActiveViewerGroup{Uuid::random(), "Group", "Title", 7});
    } catch (const std::runtime_error &) {
        threw = true;
    }
    const auto root = region.agent(f.circuit->agent_id);
    check(observed && threw == fail && root && root->root && !root->transfer_group_information &&
              root->active_group_override && root->active_group_override->title == "Title",
          "completion clears transient group information after callbacks, retaining accepted group state");
    region.close();
}

void origin_arrival_test(std::uint32_t flags, bool zero_origin, bool early) {
    Fixture f;
    f.circuit->awaiting_transfer_data = true;
    f.circuit->teleport_flags = flags;
    ViewerRegion region(
        f.world, f.circuits, [](const auto &, auto) { return true; },
        [](const auto &, const auto &, const auto &) { return true; });
    const UdpEndpoint endpoint{"127.0.0.1", 12345};
    const auto send = [&](const auto &packet) { region.receive(endpoint, lludp::encode_datagram(packet)); };
    send(f.use());
    auto arrival = f.packet("CompleteAgentMovement", 2);
    arrival.block("AgentData")["CircuitCode"] = std::uint64_t{42};
    const bool real_login = (flags & (128u | (1u << 30))) == 128u;
    if (early) {
        send(arrival);
        check(region.agent(f.circuit->agent_id)->root == real_login,
              "only a real login bypasses the transfer update wait");
        if (real_login) {
            region.close();
            return;
        }
    }
    const auto origin = zero_origin ? Uuid{} : Uuid::random();
    check(region.stage_crossing_arrival(f.circuit, origin, true), "origin fixture stages transfer data");
    if (!early)
        send(arrival);
    auto avatar = region.agent(f.circuit->agent_id);
    check(avatar && avatar->origin_region_id == origin && avatar->root == (real_login || !zero_origin),
          "non-login arrival requires a nonzero transferred origin");
    check(!avatar->transfer_group_information, "refused and completed arrivals clear transient group information");
    if (!real_login && zero_origin) {
        check(!avatar->crossing_arrival_ready, "invalid origin consumes the update notification");
        arrival.sequence = 3;
        send(arrival);
        check(!region.agent(f.circuit->agent_id)->root, "movement cannot reuse an invalid-origin notification");
        const auto corrected_origin = Uuid::random();
        check(region.stage_crossing_arrival(f.circuit, corrected_origin), "fresh transfer update can correct origin");
        avatar = region.agent(f.circuit->agent_id);
        check(avatar->root && avatar->origin_region_id == corrected_origin,
              "corrected origin replays the new pending movement");
    }
    if (region.agent(f.circuit->agent_id)->root) {
        check(region.demote_after_crossing(f.circuit->agent_id, f.circuit->session_id).has_value() &&
                  region.agent(f.circuit->agent_id)->origin_region_id.is_zero() && f.circuit->teleport_flags == 0 &&
                  !region.agent(f.circuit->agent_id)->transfer_group_information,
              "demotion resets the previous transfer origin");
        arrival.sequence = 4;
        send(arrival);
        check(!region.agent(f.circuit->agent_id)->root,
              "retained child cannot reuse its original login exemption on return");
    }
    region.close();
}

void flight_policy_test() {
    Fixture f;
    constexpr std::uint32_t fly = 0x2000;
    constexpr std::uint32_t block_flight = 0x80000;
    std::uint32_t flags = 0;
    ViewerRegionOptions options;
    options.current_region_flags = [&] { return flags; };
    ViewerRegion region(
        f.world, f.circuits, [](const auto &, auto) { return true; },
        [](const auto &, const auto &, const auto &) { return true; }, options);
    f.circuit->transfer_movement = AgentCircuitData::TransferredMovement{{2, 0, 0}, {}, fly, false};
    f.circuit->start_far = 640.f;
    const UdpEndpoint endpoint{"127.0.0.1", 12345};
    const auto send = [&](const auto &packet) { region.receive(endpoint, lludp::encode_datagram(packet)); };
    send(f.use());
    auto arrival = f.packet("CompleteAgentMovement", 2);
    arrival.block("AgentData")["CircuitCode"] = std::uint64_t{42};
    send(arrival);
    const auto allowed = region.agent(f.circuit->agent_id);
    const auto original_throttles = region.transport().packed_throttles(f.circuit->agent_id);
    check(allowed && allowed->root && allowed->flying, "arrival honors flight where permitted");
    check(allowed && allowed->draw_distance == 512.f, "initial circuit draw distance uses the region cap");
    check(region.demote_after_crossing(f.circuit->agent_id, f.circuit->session_id).has_value(),
          "retain a child circuit for the next arrival");
    flags = block_flight;
    f.circuit->transfer_throttles.assign(28, 0);
    f.circuit->transfer_camera = AgentCircuitData::TransferredCamera{{128, 128, 70}, {}, {}, {}, 224.f};
    arrival.sequence = 3;
    send(arrival);
    const auto waiting = region.agent(f.circuit->agent_id);
    check(waiting && !waiting->root, "early movement completion waits for transfer data");
    check(region.stage_crossing_arrival(f.circuit, Uuid::random()), "stage flying arrival into a no-flight region");
    const auto returned = region.agent(f.circuit->agent_id);
    check(returned && returned->root && !returned->flying && returned->velocity.x == 2.f,
          "crossing applies destination flight policy while retaining velocity");
    check(returned && returned->draw_distance == 224.f, "crossing restores the transferred draw distance");
    const auto transferred_throttles = region.transport().packed_throttles(f.circuit->agent_id);
    check(transferred_throttles && transferred_throttles != original_throttles,
          "crossing applies transferred viewer throttle rates");
    auto input = f.packet("AgentUpdate", 4);
    input.block("AgentData")["ControlFlags"] = std::uint64_t{fly};
    send(input);
    const auto blocked = region.agent(f.circuit->agent_id);
    check(blocked && !blocked->flying && blocked->controls == fly,
          "no-flight viewer state keeps the raw request available to scripts");
}

void agent_update_order_test() {
    Fixture f;
    bool in_transit = false;
    ViewerRegionOptions options;
    options.default_draw_distance = 96.f;
    options.max_draw_distance = 192.f;
    options.teleport_pending = [&](Uuid) { return in_transit; };
    ViewerRegion region(
        f.world, f.circuits, [](const auto &, auto) { return true; },
        [](const auto &, const auto &, const auto &) { return true; }, options);
    const UdpEndpoint endpoint{"127.0.0.1", 12345};
    const auto send = [&](const auto &packet) { region.receive(endpoint, lludp::encode_datagram(packet)); };
    const auto send_update = [&](std::uint32_t sequence, std::uint32_t controls, double camera_x, double far) {
        auto update = f.packet("AgentUpdate", sequence);
        update.block("AgentData")["ControlFlags"] = std::uint64_t{controls};
        update.block("AgentData")["CameraCenter"] = std::vector<double>{camera_x, 20, 30};
        update.block("AgentData")["Far"] = far;
        send(update);
    };
    const auto check_update = [&](std::uint32_t controls, float camera_x, float far, const char *description) {
        const auto viewer = region.agent(f.circuit->agent_id);
        check(viewer && viewer->controls == controls && viewer->camera_position.x == camera_x &&
                  viewer->draw_distance == far,
              description);
    };

    send(f.use());
    send_update(10, 1, 40, 128);
    check_update(0, 128, 96, "child input leaves movement and camera untouched");
    auto arrival = f.packet("CompleteAgentMovement", 2);
    arrival.block("AgentData")["CircuitCode"] = std::uint64_t{42};
    send(arrival);
    send_update(4, 2, 50, 256);
    check_update(2, 50, 192, "root input clamps draw distance to the region maximum");
    send_update(3, 1, 40, 128);
    check_update(2, 50, 192, "out-of-order input cannot overwrite newer movement or camera");
    in_transit = true;
    send_update(9, 1, 40, 128);
    check_update(2, 50, 192, "input in transit leaves movement and camera untouched");
    in_transit = false;
    send_update(5, 0, 60, 16);
    check_update(0, 60, 32, "root input clamps draw distance to the minimum");
}

void frozen_teleport_test() {
    Fixture f;
    bool frozen = true;
    std::vector<lludp::Packet> replies;
    ViewerRegionOptions options;
    options.movement_frozen = [&](const ViewerAgent &) { return frozen; };
    ViewerRegion region(
        f.world, f.circuits,
        [&](const auto &, auto bytes) {
            replies.push_back(lludp::decode_datagram(bytes));
            return true;
        },
        [](const auto &, const auto &, const auto &) { return true; }, options);
    const UdpEndpoint endpoint{"127.0.0.1", 12345};
    const auto send = [&](const auto &packet) {
        region.receive(endpoint, lludp::encode_datagram(packet));
        region.transport().poll();
    };
    send(f.use());
    auto arrival = f.packet("CompleteAgentMovement", 2);
    arrival.block("AgentData")["CircuitCode"] = std::uint64_t{42};
    send(arrival);

    auto teleport = f.packet("TeleportLocationRequest", 3);
    auto &destination = teleport.block("Info");
    destination["RegionHandle"] = (std::uint64_t(options.x + 256) << 32) | (options.y - 256);
    destination["Position"] = std::vector<double>{-116, 406, 80};
    destination["LookAt"] = std::vector<double>{0, 1, 0};
    replies.clear();
    send(teleport);
    const auto blocked = region.agent(f.circuit->agent_id);
    check(blocked && blocked->root && blocked->position == f.circuit->start_position,
          "a frozen avatar stays at its original position");
    check(std::ranges::any_of(replies,
                              [](const auto &reply) {
                                  if (reply.spec->name != "TeleportFailed")
                                      return false;
                                  const auto &reason =
                                      std::get<std::vector<std::uint8_t>>(reply.block("Info").at("Reason"));
                                  const std::string expected = "You are frozen";
                                  return !reason.empty() && reason.back() == 0 &&
                                         std::ranges::equal(expected, std::span(reason).first(reason.size() - 1));
                              }),
          "the viewer receives the original frozen teleport failure");
    check(std::ranges::none_of(replies,
                               [](const auto &reply) {
                                   return reply.spec->name == "TeleportStart" || reply.spec->name == "TeleportLocal";
                               }),
          "a frozen teleport sends no start or destination packet");

    frozen = false;
    teleport.sequence = 4;
    send(teleport);
    const auto moved = region.agent(f.circuit->agent_id);
    check(moved && moved->position == Vector3{140, 150, 80},
          "unfreezing permits the local destination expressed relative to an adjacent tile");
}

void root_agent_names_test() {
    Fixture f;
    ViewerRegion region(
        f.world, f.circuits, [](const auto &, auto) { return true; },
        [](const auto &, const auto &, const auto &) { return true; });
    const auto connect = [&](Uuid id, std::string name, std::uint32_t code, bool root) {
        auto circuit = std::make_shared<AgentCircuitData>();
        circuit->agent_id = id;
        circuit->session_id = Uuid::random();
        circuit->first_name = std::move(name);
        circuit->last_name = "Resident";
        circuit->circuit_code = code;
        circuit->start_position = {128, 128, 70};
        f.circuits.add_new_circuit(circuit);
        const UdpEndpoint endpoint{"127.0.0.1", static_cast<std::uint16_t>(12000 + code)};
        auto use = lludp::make_packet("UseCircuitCode");
        use.sequence = 1;
        auto &credentials = use.block("CircuitCode");
        credentials["ID"] = id;
        credentials["SessionID"] = circuit->session_id;
        credentials["Code"] = std::uint64_t(code);
        region.receive(endpoint, lludp::encode_datagram(use));
        if (root) {
            auto movement = lludp::make_packet("CompleteAgentMovement");
            movement.sequence = 2;
            auto &agent = movement.block("AgentData");
            agent["AgentID"] = id;
            agent["SessionID"] = circuit->session_id;
            agent["CircuitCode"] = std::uint64_t(code);
            region.receive(endpoint, lludp::encode_datagram(movement));
        }
    };
    const Uuid first("f0000000-0000-4000-8000-000000000001");
    const Uuid second("10000000-0000-4000-8000-000000000002");
    connect(first, "First", 51, true);
    connect(second, "Second", 52, true);
    connect(Uuid::random(), "Child", 53, false);
    check(region.root_agent_names() == std::vector<std::string>{"First Resident", "Second Resident"},
          "root name enumeration follows presence slots and excludes child agents");
    const auto avatars = region.root_avatars();
    check(avatars.size() == 2 && avatars[0].id == first && avatars[1].id == second &&
              avatars[0].position == Vector3{128, 128, 70} && avatars[0].name == "First Resident",
          "root avatar snapshots retain names, positions, and presence order");
    region.disconnect(first);
    const auto replacement = Uuid::random();
    connect(replacement, "Replacement", 54, true);
    check(region.root_agent_names() == std::vector<std::string>{"Replacement Resident", "Second Resident"},
          "root name enumeration reuses the vacated presence slot");
    region.disconnect(replacement);
    region.disconnect(second);
    connect(Uuid::random(), "Third", 55, true);
    connect(Uuid::random(), "Fourth", 56, true);
    check(region.root_agent_names() == std::vector<std::string>{"Fourth Resident", "Third Resident"},
          "multiple vacated presence slots are reused in reverse removal order");
}

void socket_test() {
    Fixture f;
    std::atomic<int> entered = 0, closed = 0, logout = 0;
    ViewerRegion *owner = nullptr;
    UdpSocket *socket = nullptr;
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<lludp::Packet> replies;
    UdpSocket server({"127.0.0.1", 0}, [&](const auto &from, auto bytes) { owner->receive(from, bytes); });
    socket = &server;
    ViewerRegionOptions options;
    options.name = "Connected Region";
    options.owner = f.circuit->agent_id;
    options.unix_time = [] { return 1234567u; };
    ViewerRegion region(
        f.world, f.circuits, [&](const auto &target, auto bytes) { return socket->send_to(target, bytes); },
        [](const auto &, const auto &, const auto &) { return true; }, options);
    owner = &region;
    region.callbacks(
        {},
        [&](const auto &) {
            ++entered;
            changed.notify_all();
        },
        [&](const auto &) {
            ++logout;
            changed.notify_all();
        },
        [&](const auto &) {
            ++closed;
            changed.notify_all();
        });
    auto bound = server.start();
    UdpSocket client({"127.0.0.1", 0}, [&](const auto &, auto bytes) {
        std::lock_guard lock(mutex);
        replies.push_back(lludp::decode_datagram(bytes, 65536, false));
        changed.notify_all();
    });
    client.start();
    auto send = [&](const auto &p) { client.send_to(bound, lludp::encode_datagram(p)); };
    auto wait = [&](const char *name) -> lludp::Packet {
        std::unique_lock lock(mutex);
        bool got = changed.wait_for(lock, std::chrono::seconds(3), [&] {
            return std::any_of(replies.begin(), replies.end(), [&](auto &p) { return p.spec->name == name; });
        });
        if (!got)
            throw std::runtime_error(std::string("Missing viewer packet: ") + name);
        for (auto &p : replies)
            if (p.spec->name == name)
                return p;
        throw std::runtime_error("packet");
    };
    send(f.use());
    auto handshake = wait("RegionHandshake");
    check(std::get<Uuid>(handshake.block("RegionInfo2").at("RegionID")) == f.world.region_id(),
          "UDP handshake identifies actual world");
    check(std::get<bool>(handshake.block("RegionInfo").at("IsEstateManager")), "estate owner handshake policy");
    check(std::get<double>(handshake.block("RegionInfo").at("WaterHeight")) == 20, "source water-height field");
    check(std::get<std::uint64_t>(handshake.block("RegionInfo4").at("RegionProtocols")) == (std::uint64_t(1) << 63),
          "source extended bake protocol bit");
    auto hs = f.packet("RegionHandshakeReply", 2);
    hs.block("RegionInfo")["Flags"] = std::uint64_t(7);
    send(hs);
    auto movement = f.packet("CompleteAgentMovement", 3);
    movement.block("AgentData")["CircuitCode"] = std::uint64_t(99);
    send(movement);
    movement.sequence = 4;
    movement.block("AgentData")["CircuitCode"] = std::uint64_t(42);
    send(movement);
    auto complete = wait("AgentMovementComplete");
    {
        std::unique_lock lock(mutex);
        check(changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered == 1; }),
              "scene enter callback completed");
    }
    check(std::get<std::vector<double>>(complete.block("Data").at("Position")) == std::vector<double>({128, 128, 70}),
          "movement completion preserves circuit start position");
    check(std::get<std::uint64_t>(complete.block("Data").at("Timestamp")) == 1234567, "movement completion timestamp");
    check(std::get<std::uint64_t>(complete.block("Data").at("RegionHandle")) ==
              ((std::uint64_t(256000) << 32) | 256000),
          "movement completion region handle");
    auto state = region.agent(f.circuit->agent_id);
    check(state && state->root && !state->handshake_reply && state->viewer_flags == 7 && entered == 1,
          "UDP viewer lifecycle reaches root state once for correct circuit");
    hs.sequence = 101;
    send(hs);
    const auto handshake_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < handshake_deadline && !region.agent(f.circuit->agent_id)->handshake_reply)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(region.agent(f.circuit->agent_id)->handshake_reply, "root arrival accepts a fresh handshake reply");
    check(state && state->local_id == 720001 && f.world.allocate_local_id() == 720002,
          "avatars share actual world local-ID allocator");
    auto stale = Uuid::random();
    auto before = region.agent(f.circuit->agent_id);
    check(!region.update_physics(f.circuit->agent_id, stale, {1, 2, 3}, {4, 5, 6}, {}, true),
          "stale physics session cannot update replacement viewer");
    check(region.agent(f.circuit->agent_id)->position == before->position,
          "rejected physics work preserves viewer position");
    check(!region.transport().enqueue_if_session(f.circuit->agent_id, stale, lludp::make_packet("StartPingCheck"),
                                                 ThrottleOutPacketType::Task),
          "stale session cannot enqueue packets into current circuit");
    check(region.update_physics(f.circuit->agent_id, f.circuit->session_id, {128, 128, 71}, {1, 0, 0}, {}, false)
              .has_value(),
          "current physics session updates viewer");
    auto update = f.packet("AgentUpdate", 5);
    auto &data = update.block("AgentData");
    data["BodyRotation"] = std::vector<double>{0, 0, 0, 1};
    data["HeadRotation"] = std::vector<double>{0, .6, 0, .8};
    data["CameraAtAxis"] = std::vector<double>{0, 1, 0};
    data["CameraLeftAxis"] = std::vector<double>{-1, 0, 0};
    data["CameraUpAxis"] = std::vector<double>{0, 0, 1};
    data["ControlFlags"] = std::uint64_t(0x40001);
    send(update);
    const auto head_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < head_deadline &&
           region.agent(f.circuit->agent_id)->head_rotation.y < .59f)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto head = region.agent(f.circuit->agent_id)->head_rotation;
    check(std::abs(head.y - .6f) < .0001f && std::abs(head.w - .8f) < .0001f &&
              region.agent(f.circuit->agent_id)->rotation == Quaternion{0, 0, 0, 1},
          "viewer head rotation is retained independently of body rotation");
    const auto id = f.circuit->agent_id;
    const auto session = f.circuit->session_id;
    const ViewerAgent::Seat seat{Uuid("01234567-89ab-cdef-0123-456789abcdef"), 42, {}, {0, 0, 0, 1}};
    const auto first_seat = region.update_seat(id, session, seat, {128, 128, 71});
    const auto next_seat = region.update_seat(id, session, seat, {129, 128, 71});
    check(first_seat && first_seat->seat && next_seat && next_seat->seat, "root avatar can claim a seat");
    if (first_seat && first_seat->seat && next_seat && next_seat->seat) {
        check(!region.update_seat(id, session, std::nullopt, {}, {}, first_seat->seat->order),
              "stale seat cleanup cannot clear a newer seat");
        check(region.update_seat(id, session, std::nullopt, {128, 128, 71}, {}, next_seat->seat->order).has_value(),
              "cleanup clears the matching seat");
    }
    auto log = f.packet("LogoutRequest", 6);
    send(log);
    auto reply = wait("LogoutReply");
    {
        std::unique_lock lock(mutex);
        check(changed.wait_for(lock, std::chrono::seconds(3), [&] { return logout == 1; }),
              "scene logout callback completed");
    }
    check(reply.blocks.at("InventoryData").size() == 1 &&
              std::get<Uuid>(reply.block("InventoryData").at("ItemID")).is_zero(),
          "source logout packet sentinel");
    state = region.agent(f.circuit->agent_id);
    check(state && state->logout_requested && state->controls == 0x40001 && state->look_at == Vector3{0, 1, 0} &&
              logout == 1,
          "agent update and logout reach scene owner");
    check(state && state->mouselook_rotation && state->mouselook_rotation->z > .707f &&
              state->mouselook_rotation->w > .707f,
          "mouselook axes produce a quarter-turn camera rotation");
    region.disconnect(f.circuit->agent_id);
    check(!region.agent(f.circuit->agent_id) && !f.circuits.get_agent_circuit_data(f.circuit->agent_id) && closed == 1,
          "disconnect tears down viewer state and circuit");
    client.stop();
    server.stop();
    region.close();
}
} // namespace

int main() {
    try {
        admission_test();
        background_admission_test();
        repeated_circuit_handshake_test();
        arrival_handshake_order_test();
        npc_presence_test();
        flight_policy_test();
        group_completion_cleanup_test(false);
        group_completion_cleanup_test(true);
        for (auto flags : {0u, 16u, 128u, 128u | (1u << 30)})
            for (bool zero_origin : {false, true})
                for (bool early : {false, true})
                    origin_arrival_test(flags, zero_origin, early);
        agent_update_order_test();
        frozen_teleport_test();
        root_agent_names_test();
        socket_test();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    if (failures)
        return 1;
    std::cout << "Native viewer circuit/handshake/movement UDP lifecycle passed\n";
}
