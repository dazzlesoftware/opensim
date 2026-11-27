// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace services;
using namespace scene;
namespace udp = network::lludp;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <class Predicate> void wait_for(RegionRuntime &runtime, Predicate ready, const char *message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        runtime.poll();
        if (ready())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error(message);
}

void same_frame(const KeyframedMotionFrame &actual, const KeyframedMotionFrame &expected) {
    check(actual.position == expected.position && actual.rotation == expected.rotation &&
              actual.start_position == expected.start_position && actual.start_rotation == expected.start_rotation &&
              actual.angular_velocity == expected.angular_velocity && actual.time_ms == expected.time_ms &&
              actual.time_total == expected.time_total,
          "Duplicate changed a copied frame");
}

void scenario(bool selected) {
    NullPresenceStore store;
    PresenceService presence(store);
    GridRegion region;
    region.id = Uuid::random();
    region.name = "Viewer keyframed duplication";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    double now = 0;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.background_admission = false;
    policies.bypass_object_permissions = true;
    policies.keyframed_motion_clock = [&] { return now; };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9008, 0, std::string("127.0.0.1")}));

    // Complete normal circuit authentication before sending the viewer command.
    std::unique_ptr<network::UdpSocket> socket;
    socket = std::make_unique<network::UdpSocket>(
        network::UdpSocketOptions{}, [&](const network::UdpEndpoint &sender, std::span<const std::uint8_t> bytes) {
            const auto packet = udp::decode_datagram(bytes, 65536, false);
            if (packet.reliable) {
                auto ack = udp::make_packet("PacketAck");
                ack.blocks["Packets"].push_back({{"ID", std::uint64_t(packet.sequence)}});
                socket->send_to(sender, udp::encode_datagram(ack));
            }
        });
    const auto endpoint = socket->start();
    auto circuit = std::make_shared<AgentCircuitData>();
    circuit->agent_id = Uuid::random();
    circuit->session_id = Uuid::random();
    circuit->secure_session_id = Uuid::random();
    circuit->circuit_code = 9301;
    circuit->caps_path = Uuid::random().str();
    circuit->first_name = "Duplicate";
    circuit->last_name = "Viewer";
    circuit->start_position = {45, 45, 30};
    presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id);
    LoginTransferContext context;
    std::string reason;
    check(runtime.create_agent(nullptr, region, circuit, 128, context, reason), "Could not reserve viewer");
    auto use = udp::make_packet("UseCircuitCode");
    use.sequence = 1;
    use.block("CircuitCode")["Code"] = std::uint64_t(circuit->circuit_code);
    use.block("CircuitCode")["ID"] = circuit->agent_id;
    use.block("CircuitCode")["SessionID"] = circuit->session_id;
    runtime.viewers().receive(endpoint, udp::encode_datagram(use));
    bool sent_movement = false;
    wait_for(
        runtime,
        [&] {
            const auto agent = runtime.viewers().agent(circuit->agent_id);
            if (!agent)
                return false;
            if (agent->root)
                return true;
            if (!sent_movement) {
                auto movement = udp::make_packet("CompleteAgentMovement");
                movement.sequence = 2;
                movement.block("AgentData")["AgentID"] = circuit->agent_id;
                movement.block("AgentData")["SessionID"] = circuit->session_id;
                movement.block("AgentData")["CircuitCode"] = std::uint64_t(circuit->circuit_code);
                runtime.viewers().receive(endpoint, udp::encode_datagram(movement));
                sent_movement = true;
            }
            return false;
        },
        "Viewer did not complete login");

    const auto root = runtime.world().add(SceneObject::primitive(circuit->agent_id, {20, 20, 30}), false).value();
    KeyframedMotionFrame frame;
    frame.position = Vector3{8, 0, 0};
    frame.rotation = Quaternion{0, 0, 0, 1};
    frame.time_ms = 400;
    check(runtime.install_keyframed_motion(root, script::KeyframedMotionMode::Forward, {frame}),
          "Could not install source motion");
    now += 50;
    runtime.poll();
    now += 50;
    runtime.poll();
    const auto source_before = runtime.keyframed_motion_state(root.id).value();
    const auto source_position = runtime.world().object(root.id)->position;
    check(source_before.running && !source_before.timer_stopped && source_position.x > root.position.x,
          "Source motion did not advance before duplication");

    auto duplicate = udp::make_packet("ObjectDuplicate");
    duplicate.sequence = 3;
    duplicate.block("AgentData")["AgentID"] = circuit->agent_id;
    duplicate.block("AgentData")["SessionID"] = circuit->session_id;
    duplicate.block("SharedData")["Offset"] = std::vector<double>{0, 10, 0};
    duplicate.block("SharedData")["DuplicateFlags"] = std::uint64_t(selected ? 2 : 0);
    duplicate.blocks["ObjectData"] = {{{"ObjectLocalID", std::uint64_t(root.local_id)}}};
    runtime.viewers().receive(endpoint, udp::encode_datagram(duplicate));
    std::optional<SceneObject> copied;
    wait_for(
        runtime,
        [&] {
            for (const auto &part : runtime.world().objects())
                if (!part.parent_id && part.id != root.id) {
                    copied = part;
                    return true;
                }
            return false;
        },
        "Authenticated ObjectDuplicate did not create a destination");
    check(copied->position == Vector3{source_position.x, source_position.y + 10, source_position.z},
          "Viewer duplicate offset was not applied");
    const auto destination = runtime.keyframed_motion_state(copied->id);
    check(destination.has_value(), "Viewer duplicate lost its runtime motion");
    check(destination->timer_stopped && destination->running && destination->selected == selected,
          "Duplicate eagerly started playback or lost selection");
    check(destination->base_position == source_before.base_position &&
              destination->base_rotation == source_before.base_rotation &&
              destination->iterations == source_before.iterations && destination->mode == source_before.mode &&
              destination->data == source_before.data,
          "Duplicate rebased or changed playback state");
    same_frame(destination->current, source_before.current);
    check(destination->keyframes.has_value() && source_before.keyframes.has_value() &&
              destination->keyframes->size() == source_before.keyframes->size() &&
              destination->frames.size() == source_before.frames.size(),
          "Duplicate lost configured or pending frames");
    for (std::size_t i = 0; i < destination->keyframes->size(); ++i)
        same_frame(destination->keyframes->at(i), source_before.keyframes->at(i));
    for (std::size_t i = 0; i < destination->frames.size(); ++i)
        same_frame(destination->frames[i], source_before.frames[i]);
    check(destination->serialized_position == (selected ? copied->position : source_position),
          "Duplicate selection recorded the wrong origin");
    check(!runtime.keyframed_motion_state(root.id)->timer_stopped, "Copy did not restart the active source timer");

    now += 50;
    runtime.poll();
    check(runtime.world().object(root.id)->position.x > source_position.x,
          "Source stopped advancing after duplication");
    check(runtime.world().object(copied->id)->position == copied->position &&
              runtime.keyframed_motion_state(copied->id)->current.time_ms == destination->current.time_ms,
          "Stopped duplicate advanced with the source timer");
    if (selected) {
        check(runtime.set_object_selected(*copied, false), "Could not release selected duplicate");
        check(!runtime.keyframed_motion_state(copied->id)->timer_stopped,
              "Releasing selected duplicate did not start its own motion");
        const auto source_before_release_tick = runtime.world().object(root.id)->position;
        now += 50;
        runtime.poll();
        check(runtime.world().object(copied->id)->position != copied->position,
              "Released duplicate did not move its destination object");
        check(runtime.world().object(root.id)->position.y == source_before_release_tick.y &&
                  runtime.world().object(root.id)->position.x > source_before_release_tick.x,
              "Destination motion host changed the source binding");
    }
    runtime.stop();
    socket->stop();
}
} // namespace

int main() {
    try {
        scenario(false);
        scenario(true);
        std::cout << "Viewer keyframed duplication checks passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
