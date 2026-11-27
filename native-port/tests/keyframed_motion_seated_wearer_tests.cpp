// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace scene;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void near(Vector3 actual, Vector3 expected, const char *message) {
    check(std::abs(actual.x - expected.x) < .0003f && std::abs(actual.y - expected.y) < .0003f &&
              std::abs(actual.z - expected.z) < .0003f,
          message);
}

Vector3 absolute(Vector3 position, Vector3 root_relative_offset, Quaternion rotation) {
    const auto offset = sdk_rotate_vector(root_relative_offset, rotation);
    return {position.x + offset.x, position.y + offset.y, position.z + offset.z};
}

std::unique_ptr<network::UdpSocket> connect(RegionRuntime &runtime, services::GridRegion &region,
                                            services::PresenceService &presence, Uuid owner) {
    auto peer = std::make_shared<network::UdpSocket *>(nullptr);
    auto socket = std::make_unique<network::UdpSocket>(
        network::UdpSocketOptions{}, [peer](const auto &sender, std::span<const std::uint8_t> bytes) {
            const auto packet = network::lludp::decode_datagram(bytes, 65536, false);
            if (packet.reliable) {
                auto ack = network::lludp::make_packet("PacketAck");
                ack.reliable = false;
                ack.blocks["Packets"].push_back({{"ID", std::uint64_t(packet.sequence)}});
                (*peer)->send_to(sender, network::lludp::encode_datagram(ack));
            }
        });
    *peer = socket.get();
    const auto endpoint = socket->start();
    auto circuit = std::make_shared<AgentCircuitData>();
    circuit->agent_id = owner;
    circuit->session_id = Uuid::random();
    circuit->secure_session_id = Uuid::random();
    circuit->circuit_code = 431;
    circuit->caps_path = Uuid::random().str();
    circuit->first_name = "Seated";
    circuit->last_name = "Wearer";
    circuit->start_position = {100, 100, 30};
    presence.login(owner.str(), circuit->session_id, circuit->secure_session_id);
    services::LoginTransferContext context;
    std::string reason;
    check(runtime.create_agent(nullptr, region, circuit, 128, context, reason), "Could not reserve wearer");
    auto use = network::lludp::make_packet("UseCircuitCode");
    use.sequence = 1;
    use.block("CircuitCode")["Code"] = std::uint64_t(circuit->circuit_code);
    use.block("CircuitCode")["ID"] = owner;
    use.block("CircuitCode")["SessionID"] = circuit->session_id;
    runtime.viewers().receive(endpoint, network::lludp::encode_datagram(use));
    bool sent_movement = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.poll();
        if (const auto avatar = runtime.viewers().agent(owner)) {
            if (avatar->root)
                return socket;
            if (!sent_movement) {
                auto movement = network::lludp::make_packet("CompleteAgentMovement");
                movement.sequence = 2;
                movement.block("AgentData")["AgentID"] = owner;
                movement.block("AgentData")["SessionID"] = circuit->session_id;
                movement.block("AgentData")["CircuitCode"] = std::uint64_t(circuit->circuit_code);
                runtime.viewers().receive(endpoint, network::lludp::encode_datagram(movement));
                sent_movement = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Wearer did not complete authenticated arrival");
}

void scenario() {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "Seated attachment wearer coordinates";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    double now = 0;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.background_admission = false;
    policies.keyframed_motion_clock = [&] { return now; };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9031, 0, std::string("127.0.0.1")}));
    const auto owner = Uuid::random();
    auto socket = connect(runtime, region, presence, owner);
    const auto wearer = runtime.viewers().agent(owner).value();
    const auto seat_root = runtime.world().add(SceneObject::primitive(owner, {40, 40, 30}), false).value();
    auto child = SceneObject::primitive(owner, {2, 3, 1});
    child.parent_id = seat_root.local_id;
    child.rotation = {0, 0, 1, 0};
    child = runtime.world().add(child, false).value();
    // This is already root-relative, as ScenePresence.m_pos and ViewerAgent.Seat
    // are after seating on a child. The getter must not apply the child twice.
    const auto rotated = sdk_rotate_vector({1, 2, 3}, child.rotation);
    const Vector3 seat_offset{child.position.x + rotated.x, child.position.y + rotated.y, child.position.z + rotated.z};
    const auto cached = absolute(seat_root.position, seat_offset, seat_root.rotation);
    check(bool(runtime.viewers().update_seat(
              owner, wearer.circuit->session_id,
              ViewerAgent::Seat{child.id, seat_root.local_id, seat_offset, {0, 0, 1, 0}, true}, cached)),
          "Could not seat authenticated wearer on child");

    const auto moving = runtime.world().add(SceneObject::primitive(owner, {20, 20, 30}), false).value();
    KeyframedMotionFrame frame;
    frame.position = Vector3{8, 0, 0};
    frame.rotation = Quaternion{0, 0, 0, 1};
    frame.time_ms = 400;
    check(runtime.install_keyframed_motion(moving, script::KeyframedMotionMode::Forward, {frame}),
          "Could not install ground motion");
    now += 50;
    runtime.poll();
    const Vector3 attachment_offset{.25f, .5f, .75f};
    check(runtime.world().attach_from_ground(moving.id, wearer.local_id, 5, attachment_offset, Uuid::random(),
                                             Uuid::random(), Uuid::random()),
          "Could not attach moving object to seated wearer");

    const auto tick = [&](Vector3 expected_wearer) {
        now += 50;
        runtime.poll();
        const auto state = runtime.keyframed_motion_state(moving.id).value();
        const auto target = state.current.position.value();
        const auto remaining = static_cast<float>(state.current.time_ms / 50.0);
        check(remaining > 1, "Fixture unexpectedly completed its frame");
        const Vector3 expected{expected_wearer.x + (target.x - expected_wearer.x) / remaining,
                               expected_wearer.y + (target.y - expected_wearer.y) / remaining,
                               expected_wearer.z + (target.z - expected_wearer.z) / remaining};
        const auto attached = runtime.world().object(moving.id).value();
        check(attached.is_attachment && attached.attached_group_position.has_value(), "Tick lost attachment state");
        near(attached.position, attachment_offset, "KFM overwrote visible attachment offset");
        near(*attached.attached_group_position, expected,
             "KFM used cached wearer position, double child transform, or appearance correction");
    };

    const Vector3 moved_pose{70, 80, 30};
    const Quaternion moved_rotation{0, 0, 1, 0};
    check(runtime.world().store_keyframed_position(seat_root, moved_pose) &&
              runtime.world().store_keyframed_properties(seat_root, moved_rotation, {}, {}),
          "Could not change current seat pose without refreshing wearer");
    near(runtime.viewers().agent(owner)->position, cached, "Fixture prematurely refreshed cached wearer position");
    tick(absolute(moved_pose, seat_offset, moved_rotation));

    // Relink the actual seat child while deliberately leaving parent_local stale.
    const auto other_root = runtime.world().add(SceneObject::primitive(owner, {90, 100, 40}), false).value();
    check(runtime.world().delink_child(child.id, seat_root.id) && runtime.world().link_prim(child.id, other_root.id),
          "Could not reparent seat child");
    check(runtime.viewers().agent(owner)->seat->parent_local == seat_root.local_id,
          "Fixture did not retain stale seat parent cache");
    tick(absolute(other_root.position, seat_offset, other_root.rotation));

    const auto last_cached = runtime.viewers().agent(owner)->position;
    check(runtime.world().remove(child.id, false), "Could not remove seat part without a viewer refresh");
    check(runtime.viewers().agent(owner)->seat.has_value(), "Seat cache unexpectedly cleared before fallback check");
    tick(last_cached);
    runtime.stop();
    socket->stop();
}
} // namespace

int main() {
    try {
        scenario();
        std::cout << "PASS: attached KFM reads current seated wearer pose, reparented seat and missing-seat fallback\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
