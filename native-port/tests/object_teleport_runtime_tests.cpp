// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/scene/object_update.hpp"
#include "opensim/network/rpc.hpp"
#include "opensim/scene/default_animations.hpp"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace services;
using namespace scene;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct Observer {
    std::mutex mutex;
    std::vector<std::uint32_t> kills;
    std::vector<std::uint32_t> updates;
    std::unique_ptr<network::UdpSocket> socket;
};

std::unique_ptr<Observer> connect_observer(RegionRuntime &runtime, GridRegion &region, PresenceService &presence,
                                           Uuid id, std::uint32_t code) {
    auto observer = std::make_unique<Observer>();
    auto *capture = observer.get();
    observer->socket = std::make_unique<network::UdpSocket>(
        network::UdpSocketOptions{},
        [capture](const network::UdpEndpoint &sender, std::span<const std::uint8_t> bytes) {
            const auto packet = network::lludp::decode_datagram(bytes, 65536, false);
            if (packet.reliable) {
                auto ack = network::lludp::make_packet("PacketAck");
                ack.sequence = 0;
                ack.blocks["Packets"].push_back({{"ID", std::uint64_t(packet.sequence)}});
                capture->socket->send_to(sender, network::lludp::encode_datagram(ack));
            }
            if (packet.spec->name == "KillObject") {
                std::lock_guard lock(capture->mutex);
                for (const auto &row : packet.blocks.at("ObjectData"))
                    capture->kills.push_back(static_cast<std::uint32_t>(std::get<std::uint64_t>(row.at("ID"))));
            }
            if (packet.spec->name == "ObjectUpdate") {
                std::lock_guard lock(capture->mutex);
                for (const auto &row : packet.blocks.at("ObjectData"))
                    capture->updates.push_back(static_cast<std::uint32_t>(std::get<std::uint64_t>(row.at("ID"))));
            }
        });
    const auto endpoint = observer->socket->start();
    auto circuit = std::make_shared<AgentCircuitData>();
    circuit->agent_id = id;
    circuit->session_id = Uuid::random();
    circuit->secure_session_id = Uuid::random();
    circuit->circuit_code = code;
    circuit->caps_path = Uuid::random().str();
    circuit->first_name = "Teleport";
    circuit->last_name = "Observer";
    circuit->start_position = {45, 45, 30};
    presence.login(id.str(), circuit->session_id, circuit->secure_session_id);
    LoginTransferContext context;
    std::string reason;
    check(runtime.create_agent(nullptr, region, circuit, 128, context, reason), "Could not reserve observer");
    auto use = network::lludp::make_packet("UseCircuitCode");
    use.sequence = 1;
    use.block("CircuitCode")["Code"] = std::uint64_t(code);
    use.block("CircuitCode")["ID"] = id;
    use.block("CircuitCode")["SessionID"] = circuit->session_id;
    runtime.viewers().receive(endpoint, network::lludp::encode_datagram(use));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool sent_movement = false;
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.poll();
        if (const auto agent = runtime.viewers().agent(id)) {
            if (agent->root)
                return observer;
            if (!sent_movement) {
                auto movement = network::lludp::make_packet("CompleteAgentMovement");
                movement.sequence = 2;
                movement.block("AgentData")["AgentID"] = id;
                movement.block("AgentData")["SessionID"] = circuit->session_id;
                movement.block("AgentData")["CircuitCode"] = std::uint64_t(code);
                runtime.viewers().receive(endpoint, network::lludp::encode_datagram(movement));
                sent_movement = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Observer did not become a root viewer");
}

void exercise(bool accept_remote, bool seated = false, bool replace_during_send = false,
              const std::filesystem::path &physics_library = {},
              std::shared_ptr<const DefaultAnimations> animations = {}) {
    const bool live_destination = accept_remote && seated;
    const bool physical = !physics_library.empty();
    NullPresenceStore store;
    PresenceService presence(store);
    GridRegion region;
    region.id = Uuid::random();
    region.name = "Object teleport runtime";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    auto destination = region;
    destination.id = Uuid::random();
    destination.x = 256;
    const auto owner = Uuid::random();
    const auto known_observer = Uuid::random(), unknown_observer = Uuid::random();
    auto script_asset = std::make_shared<AssetBase>(Uuid::random(), "Seated teleport checkpoint", 10, "");
    const std::string script_source = "integer value = 41; default { state_entry() { value += 1; } }";
    script_asset->data = std::make_shared<std::vector<std::uint8_t>>(script_source.begin(), script_source.end());
    const auto root_script = Uuid::random(), child_script = Uuid::random();
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = owner;
    auto full_parcel = std::make_shared<ScriptParcel>();
    full_parcel->id = Uuid::random();
    full_parcel->owner = Uuid::random();
    full_parcel->local_id = 2;
    std::atomic<unsigned> sends = 0;
    std::atomic<bool> release_send = false;
    SceneObject transferred;
    std::mutex received_mutex;
    std::vector<std::vector<std::uint8_t>> received_terse;
    std::atomic<unsigned> decode_failures = 0;
    std::vector<std::uint32_t> rider_kills;
    std::atomic<bool> crossed_notice = false;
    std::unique_ptr<network::UdpSocket> viewer_socket;
    network::UdpEndpoint viewer_endpoint;
    if (seated) {
        viewer_socket = std::make_unique<network::UdpSocket>(
            network::UdpSocketOptions{}, [&](const network::UdpEndpoint &sender, std::span<const std::uint8_t> bytes) {
                try {
                    const auto packet = network::lludp::decode_datagram(bytes, 65536, false);
                    if (packet.reliable) {
                        auto ack = network::lludp::make_packet("PacketAck");
                        ack.sequence = 0;
                        ack.blocks["Packets"].push_back({{"ID", std::uint64_t(packet.sequence)}});
                        viewer_socket->send_to(sender, network::lludp::encode_datagram(ack));
                    }
                    if (packet.spec->name == "ImprovedTerseObjectUpdate") {
                        std::lock_guard lock(received_mutex);
                        for (const auto &row : packet.blocks.at("ObjectData"))
                            received_terse.push_back(std::get<std::vector<std::uint8_t>>(row.at("Data")));
                    }
                    if (packet.spec->name == "CrossedRegion")
                        crossed_notice = true;
                    if (packet.spec->name == "KillObject") {
                        std::lock_guard lock(received_mutex);
                        for (const auto &row : packet.blocks.at("ObjectData"))
                            rider_kills.push_back(static_cast<std::uint32_t>(std::get<std::uint64_t>(row.at("ID"))));
                    }
                } catch (const std::exception &failure) {
                    if (decode_failures.fetch_add(1) < 3)
                        std::cerr << "Viewer packet handling failed: " << failure.what() << '\n';
                    throw;
                }
            });
        viewer_endpoint = viewer_socket->start();
    }
    std::unique_ptr<RegionRuntime> destination_runtime;
    network::UdpEndpoint destination_endpoint;
    std::atomic<bool> rider_prepared = false;
    std::shared_ptr<AgentCircuitData> seated_circuit;
    if (live_destination) {
        RegionPolicies receiving;
        receiving.access = [](Uuid, const auto &, auto &) { return true; };
        receiving.authorize = [](const auto &, auto &) { return true; };
        receiving.background_admission = false;
        receiving.physics_library = physics_library;
        receiving.animations = animations;
        receiving.script_parcel_at = [parcel](Vector3) { return parcel; };
        receiving.load_asset = [script_asset](Uuid id) {
            return id == script_asset->full_id() ? script_asset : nullptr;
        };
        receiving.schedule_asset_fetch = [](auto action) { action(); };
        destination_runtime = std::make_unique<RegionRuntime>(destination, presence, std::move(receiving));
        auto receiving_listener = std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9001, 0, std::string("127.0.0.1")});
        destination_endpoint = destination_runtime->start(receiving_listener);
        destination = destination_runtime->descriptor();
        destination_runtime->world().add(SceneObject::primitive(owner, {100, 100, 30}), false);
    }
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.background_admission = false;
    policies.physics_library = physics_library;
    policies.animations = animations;
    policies.load_asset = [script_asset](Uuid id) { return id == script_asset->full_id() ? script_asset : nullptr; };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    policies.teleport_access = [](GridRegion &, const ViewerAgent &, Vector3, bool, LoginTransferContext &) {
        return std::string{};
    };
    policies.prim_limits_enabled = true;
    policies.script_parcel_at = [parcel, full_parcel](Vector3 p) { return p.x >= 80 ? full_parcel : parcel; };
    policies.teleport_destination = [destination](std::uint64_t) { return std::optional{destination}; };
    if (live_destination) {
        policies.min_region_view_distance = policies.max_region_view_distance = 512;
        policies.schedule_region_lookup = [](auto action) { action(); };
        policies.neighbour_region_range = [destination](int, int, int, int) {
            return std::optional{std::vector<GridRegion>{destination}};
        };
        policies.create_neighbour_child = [&](GridRegion &target, const std::shared_ptr<AgentCircuitData> &circuit) {
            if (circuit->agent_id == unknown_observer)
                return false;
            LoginTransferContext context;
            std::string reason;
            return destination_runtime->create_agent(&region, target, circuit, 0, context, reason);
        };
        policies.teleport_arrived = [&](Uuid session, Uuid target) {
            const auto incoming = destination_runtime->viewers().agent(owner);
            return target == destination.id && incoming && incoming->root && incoming->circuit->session_id == session;
        };
        policies.teleport_create = [&](const GridRegion &source, GridRegion &, GridRegion &target,
                                       const ViewerAgent &rider, Vector3 position, Vector3,
                                       LoginTransferContext &context, const Uuid &token, std::uint32_t flags,
                                       const AgentData *snapshot, std::function<bool()> &,
                                       std::string &reason) -> std::optional<network::lludp::Packet> {
            check(token.is_zero() && flags == 0 && rider.seat && snapshot,
                  "Seated transfer did not use the crossing rider snapshot");
            auto circuit = std::make_shared<AgentCircuitData>(*rider.circuit);
            circuit->child = true;
            circuit->caps_path = Uuid::random().str();
            if (!destination_runtime->create_agent(&source, target, circuit, 0, context, reason))
                return std::nullopt;
            auto data = std::make_shared<AgentData>(*snapshot);
            data->agent_id = circuit->agent_id;
            data->session_id = circuit->session_id;
            data->circuit_code = circuit->circuit_code;
            data->region_id = source.id;
            data->position = position;
            data->parent_part = rider.seat->part;
            data->sit_offset = rider.seat->offset;
            data->body_rotation = rider.seat->rotation;
            data->crossing_flags = rider.seat->explicit_target ? 7 : 3;
            data->appearance = rider.circuit->appearance;
            if (!destination_runtime->update_agent(target, data, context))
                return std::nullopt;
            auto notice = network::lludp::make_packet("CrossedRegion");
            notice.block("AgentData")["AgentID"] = circuit->agent_id;
            notice.block("AgentData")["SessionID"] = circuit->session_id;
            auto &region_data = notice.block("RegionData");
            region_data["RegionHandle"] = target.handle();
            region_data["SimIP"] = std::uint64_t{0x0100007f};
            region_data["SimPort"] = std::uint64_t(destination_endpoint.port);
            const auto seed = target.server_uri() + "CAPS/" + circuit->caps_path.value() + "0000/";
            region_data["SeedCapability"] = std::vector<std::uint8_t>(seed.c_str(), seed.c_str() + seed.size() + 1);
            notice.block("Info")["Position"] = std::vector<double>{position.x, position.y, position.z};
            notice.block("Info")["LookAt"] = std::vector<double>{1, 0, 0};
            rider_prepared = true;
            return notice;
        };
    }
    policies.send_crossing_object = [&](GridRegion &, Vector3 position, std::shared_ptr<SimulationObject> object) {
        check(position.x == 14 && position.y == 45, "Remote destination coordinates differ");
        const auto xml = object->to_xml2();
        const auto asset = read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size()});
        check(bool(asset), "Remote object serialization failed");
        transferred = asset->linksets.front().parts.front().object;
        ++sends;
        if (replace_during_send) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!release_send && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            check(release_send, "Replacement fixture did not release remote send");
        }
        if (live_destination)
            return destination_runtime->create_object(&destination, position, object->clone_for_new_scene(), true);
        return accept_remote;
    };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.world().terrain().clear_land(21);
    auto listener = std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")});
    runtime.start(listener);
    auto object = SceneObject::primitive(owner, {12, 13, 30}, {1, 2, 4});
    if (physical) {
        object.flags = 1;
        object.physics.gravity = 0;
    }
    object.velocity = {1, 2, 3};
    const auto root = runtime.world().add(object, false).value();
    auto child = SceneObject::primitive(owner, {1, 0, 0});
    child.parent_id = root.local_id;
    child = runtime.world().add(child, false).value();
    const auto running_checkpoint = [](RegionRuntime &scene, Uuid part, Uuid item) {
        const auto states = scene.snapshot_task_script_states(part);
        const auto entry = states.find(item);
        const auto checkpoint = entry == states.end() ? std::nullopt : script::decode_lsl_checkpoint(entry->second);
        return checkpoint && checkpoint->running && checkpoint->globals.size() == 1 &&
               std::get<std::int32_t>(checkpoint->globals.front()) == 42;
    };
    if (live_destination) {
        for (const auto &[part, id] : {std::pair{root.id, root_script}, std::pair{child.id, child_script}}) {
            auto item = std::make_shared<TaskInventoryItem>();
            item->item_id = id;
            item->parent_id = item->parent_part_id = part;
            item->asset_id = script_asset->full_id();
            item->owner_id = item->creator_id = owner;
            item->type = item->inventory_type = 10;
            item->script_running = true;
            runtime.restore_task_inventory(part, {item});
        }
        const auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < ready_deadline) {
            runtime.poll();
            if (running_checkpoint(runtime, root.id, root_script) &&
                running_checkpoint(runtime, child.id, child_script))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(running_checkpoint(runtime, root.id, root_script) && running_checkpoint(runtime, child.id, child_script),
              "Source scripts did not initialize their live checkpoint globals");
    }
    if (physical) {
        const auto moving_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (runtime.world().object(root.id)->position.x <= object.position.x &&
               std::chrono::steady_clock::now() < moving_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            runtime.poll();
        }
        check(runtime.world().object(root.id)->position.x > object.position.x,
              "Configured source region did not move the physical actor");
    }
    runtime.world().set_selected(child.id, true);
    check(runtime.teleport_object(child.id, root.id, {40, 45, 0}, {0, 0, 0, 1}, 1) == -1,
          "Selected linkset accepted teleport");
    runtime.world().set_selected(child.id, false);
    check(runtime.teleport_object(child.id, child.id, {40, 45, 0}, {0, 0, 0, 1}, 1) == -1,
          "Child UUID accepted as teleport group");
    check(runtime.teleport_object(child.id, root.id, {80, 45, 30}, {0, 0, 0, 1}, 0) == -3,
          "Teleport ignored target parcel prim capacity");
    auto foreign = SceneObject::primitive(Uuid::random(), {20, 20, 30});
    foreign = runtime.world().add(foreign, false).value();
    parcel->owner = Uuid::random();
    check(runtime.teleport_object(child.id, foreign.id, {30, 30, 30}, {0, 0, 0, 1}, 0) == -1,
          "Foreign object source land ownership was ignored");
    parcel->owner = owner;
    check(runtime.teleport_object(child.id, foreign.id, {30, 30, 30}, {0, 0, 0, 1}, 0) == 1,
          "Source land owner could not teleport a foreign object");
    check(runtime.teleport_object(child.id, root.id, {40, 45, 0}, {0, 0, 0, 1}, 1) == 1,
          "Live local teleport failed or selected rejection leaked transit");
    const auto moved = runtime.world().object(root.id).value();
    check(moved.position == Vector3{40, 45, 23.01f} && moved.velocity == Vector3{},
          "Local teleport did not store terrain clearance and stopped motion");
    std::unique_ptr<Observer> known_capture, unknown_capture;
    std::uint32_t rider_source_local = 0;
    if (seated) {
        namespace udp = network::lludp;
        auto circuit = std::make_shared<AgentCircuitData>();
        seated_circuit = circuit;
        circuit->agent_id = owner;
        circuit->session_id = Uuid::random();
        circuit->secure_session_id = Uuid::random();
        circuit->circuit_code = 42;
        circuit->child = false;
        circuit->caps_path = Uuid::random().str();
        circuit->first_name = "Teleport";
        circuit->last_name = "Rider";
        circuit->start_position = moved.position;
        presence.login(owner.str(), circuit->session_id, circuit->secure_session_id);
        LoginTransferContext context;
        std::string reason;
        check(runtime.create_agent(nullptr, region, circuit, 128, context, reason), "Could not reserve teleport rider");
        const auto endpoint = viewer_endpoint;
        auto connect = udp::make_packet("UseCircuitCode");
        connect.sequence = 1;
        connect.block("CircuitCode")["Code"] = std::uint64_t{42};
        connect.block("CircuitCode")["ID"] = owner;
        connect.block("CircuitCode")["SessionID"] = circuit->session_id;
        runtime.viewers().receive(endpoint, udp::encode_datagram(connect));
        const auto connect_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!runtime.viewers().agent(owner) && std::chrono::steady_clock::now() < connect_deadline) {
            runtime.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(bool(runtime.viewers().agent(owner)), "Teleport rider did not connect");
        auto movement = udp::make_packet("CompleteAgentMovement");
        movement.sequence = 2;
        movement.block("AgentData")["AgentID"] = owner;
        movement.block("AgentData")["SessionID"] = circuit->session_id;
        movement.block("AgentData")["CircuitCode"] = std::uint64_t{42};
        runtime.viewers().receive(endpoint, udp::encode_datagram(movement));
        const auto arrival_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < arrival_deadline) {
            runtime.poll();
            if (const auto rider = runtime.viewers().agent(owner); rider && rider->root)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(runtime.viewers().agent(owner)->root &&
                  runtime.viewers().update_seat(
                      owner, circuit->session_id,
                      ViewerAgent::Seat{
                          root.id, root.local_id, physical ? Vector3{.5f, .25f, 1.f} : Vector3{}, {0, 0, 0, 1}},
                      moved.position),
              "Could not seat teleport rider");
        rider_source_local = runtime.viewers().agent(owner)->local_id;
        check(bool(runtime.world().store_teleport_motion(root, {{1, 2, 3}, {}, {}, {0, 0, 0, 1}})),
              "Could not prepare moving teleport fixture");
        check(runtime.world().patch_transform(root.id, Vector3{41, 46, 32}, {}, {}),
              "Could not prepare distinct pre-teleport transform");
        if (live_destination) {
            known_capture = connect_observer(runtime, region, presence, known_observer, 43);
            unknown_capture = connect_observer(runtime, region, presence, unknown_observer, 44);
            const auto known_seeds = runtime.capabilities()->capabilities().copy_children_seeds(known_observer);
            const auto unknown_seeds = runtime.capabilities()->capabilities().copy_children_seeds(unknown_observer);
            check(known_seeds->find(destination.handle()) != known_seeds->end() &&
                      unknown_seeds->find(destination.handle()) == unknown_seeds->end(),
                  "Observers did not establish distinct destination visibility");
        }
    }
    // Original unseated CrossAsync ignores the prepared teleport rotation.
    check(runtime.teleport_object(child.id, root.id, {270, 45, 30}, {0, 0, 1, 0}, physical ? 5 : 4) == 0,
          "Live remote teleport did not queue");
    check(runtime.teleport_object(child.id, root.id, {50, 45, 30}, {0, 0, 0, 1}, 0) == -1,
          "Queued teleport did not protect its in-transit group");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    if (replace_during_send) {
        while (!sends && std::chrono::steady_clock::now() < deadline) {
            runtime.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(sends == 1, "Replacement fixture did not enter remote send");
        runtime.world().remove(root.id, false);
        auto replacement = SceneObject::primitive(owner, {100, 100, 40});
        replacement.id = root.id;
        replacement.velocity = {9, 8, 7};
        const auto added = runtime.world().add(replacement, false).value();
        check(added.local_id != root.local_id, "Replacement reused the original local ID");
        release_send = true;
        bool completed = false;
        while (std::chrono::steady_clock::now() < deadline) {
            runtime.poll();
            if (runtime.teleport_object(added.id, added.id, added.position, added.rotation, 0) == 1) {
                completed = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto preserved = runtime.world().object(added.id);
        check(completed && preserved && preserved->local_id == added.local_id &&
                  preserved->position == added.position && preserved->velocity == added.velocity,
              "Stale crossing completion deleted or stopped a replacement group");
        runtime.stop();
        return;
    }
    bool destination_connected = false;
    bool destination_movement_sent = false, enabled_notice = false;
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.poll();
        if (live_destination) {
            destination_runtime->poll();
            const auto events = runtime.capabilities()->event_queue();
            if (events && events->has_events({}, owner)) {
                const auto response = events->get_events({}, owner);
                if (std::any_cast<std::int32_t>(response->get("int_response_code")) == 200) {
                    const auto body = std::any_cast<server::PollBytes>(response->get("bin_response_data"));
                    const auto decoded = network::parse_llsd_xml(std::string(body->begin(), body->end()));
                    for (const auto &event : decoded.map.at("events").array)
                        if (event.map.at("message").text == "EnableSimulator")
                            enabled_notice = true;
                        else if (event.map.at("message").text == "CrossedRegion")
                            crossed_notice = true;
                }
            }
            if (enabled_notice && !destination_connected) {
                namespace udp = network::lludp;
                auto use = udp::make_packet("UseCircuitCode");
                use.sequence = 1;
                use.block("CircuitCode")["Code"] = std::uint64_t(seated_circuit->circuit_code);
                use.block("CircuitCode")["ID"] = owner;
                use.block("CircuitCode")["SessionID"] = seated_circuit->session_id;
                destination_runtime->viewers().receive(viewer_endpoint, udp::encode_datagram(use));
                destination_connected = true;
            }
            if (rider_prepared && crossed_notice && destination_connected && !destination_movement_sent) {
                namespace udp = network::lludp;
                auto movement = udp::make_packet("CompleteAgentMovement");
                movement.sequence = 2;
                movement.block("AgentData")["AgentID"] = owner;
                movement.block("AgentData")["SessionID"] = seated_circuit->session_id;
                movement.block("AgentData")["CircuitCode"] = std::uint64_t(seated_circuit->circuit_code);
                destination_runtime->viewers().receive(viewer_endpoint, udp::encode_datagram(movement));
                destination_movement_sent = true;
            }
            const auto incoming = destination_runtime->viewers().agent(owner);
            if (incoming && incoming->root && incoming->seat)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (sends && (accept_remote ? !runtime.world().object(root.id)
                                    : runtime.teleport_object(child.id, root.id, {40, 45, 30}, {0, 0, 0, 1}, 0) == 1))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(sends == 1, "Remote teleport did not reach the send adapter exactly once");
    if (seated) {
        const auto expected_velocity = physical ? Vector3{} : Vector3{-1, -2, 3};
        check(transferred.rotation.w == 0 && transferred.rotation.z == 1 && transferred.velocity == expected_velocity,
              "Seated remote teleport did not apply prepared rotation and velocity");
        if (live_destination) {
            const auto incoming = destination_runtime->viewers().agent(owner);
            const auto received_root = destination_runtime->world().object(root.id);
            const auto received_child = destination_runtime->world().object(child.id);
            if (!incoming || !incoming->root || !incoming->seat)
                std::cerr << "Destination rider prepared=" << rider_prepared << " notice=" << crossed_notice
                          << " connected=" << destination_connected << " present=" << bool(incoming)
                          << " root=" << (incoming && incoming->root)
                          << " seated=" << (incoming && bool(incoming->seat)) << '\n';
            check(incoming && incoming->root && incoming->seat && received_root && received_child &&
                      incoming->seat->part == root.id && incoming->seat->parent_local == received_root->local_id &&
                      received_root->local_id != root.local_id && received_root->position == Vector3{14, 45, 30} &&
                      received_root->rotation.z == 1 && received_root->velocity == expected_velocity &&
                      received_child->parent_id == received_root->local_id &&
                      received_child->position == child.position,
                  "Live destination lost the object transform, linked child, or seated rider");
            const auto cleanup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < cleanup_deadline) {
                runtime.poll();
                destination_runtime->poll();
                const auto source_rider = runtime.viewers().agent(owner);
                if (source_rider && !source_rider->root && !runtime.agent_transfer_state(owner) &&
                    !runtime.snapshot_task_inventory(root.id) && !runtime.snapshot_task_inventory(child.id) &&
                    running_checkpoint(*destination_runtime, root.id, root_script) &&
                    running_checkpoint(*destination_runtime, child.id, child_script))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            const auto source_rider = runtime.viewers().agent(owner);
            check(source_rider && !source_rider->root && !runtime.agent_transfer_state(owner) &&
                      !runtime.snapshot_task_inventory(root.id) && !runtime.snapshot_task_inventory(child.id) &&
                      runtime.snapshot_task_script_states(root.id).empty() &&
                      runtime.snapshot_task_script_states(child.id).empty() &&
                      running_checkpoint(*destination_runtime, root.id, root_script) &&
                      running_checkpoint(*destination_runtime, child.id, child_script),
                  "Successful seated teleport reset destination scripts or failed source script/transfer cleanup");
            if (physical) {
                const auto before_motion = destination_runtime->world().object(root.id).value();
                check(bool(destination_runtime->world().store_teleport_motion(
                          before_motion, {{2, 0, 0}, {}, {}, before_motion.rotation})),
                      "Could not set destination actor momentum");
                const auto motion_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                while (std::chrono::steady_clock::now() < motion_deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                    destination_runtime->poll();
                    if (destination_runtime->world().object(root.id)->position.x > before_motion.position.x + .01f)
                        break;
                }
                const auto moving_root = destination_runtime->world().object(root.id).value();
                const auto moving_rider = destination_runtime->viewers().agent(owner);
                check(moving_root.position.x > before_motion.position.x + .01f && moving_rider && moving_rider->seat &&
                          moving_rider->seat->parent_local == moving_root.local_id &&
                          std::abs(moving_rider->position.x - (moving_root.position.x - .5f)) < .002f &&
                          std::abs(moving_rider->position.y - (moving_root.position.y - .25f)) < .002f &&
                          std::abs(moving_rider->position.z - (moving_root.position.z + 1.f)) < .002f,
                      "Destination actor stopped simulating or the seated rider did not follow it");
            }
            const auto receive_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            while (std::chrono::steady_clock::now() < receive_deadline) {
                runtime.poll();
                destination_runtime->poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            {
                std::scoped_lock lock(known_capture->mutex, unknown_capture->mutex, received_mutex);
                const auto has = [](const auto &rows, std::uint32_t id) {
                    return std::find(rows.begin(), rows.end(), id) != rows.end();
                };
                check(has(known_capture->updates, root.local_id) && has(unknown_capture->updates, root.local_id) &&
                          has(unknown_capture->updates, rider_source_local) &&
                          has(unknown_capture->kills, root.local_id) &&
                          has(unknown_capture->kills, rider_source_local) &&
                          !has(known_capture->kills, root.local_id) && !has(rider_kills, root.local_id),
                      "Seated teleport did not hide departing objects/riders only from observers lacking the "
                      "destination");
            }
        } else {
            const auto recovered = runtime.world().object(root.id).value();
            check(recovered.rotation.w == 1 && recovered.rotation.z == 0 && recovered.velocity == Vector3{},
                  "Rejected seated teleport did not restore orientation and stop motion");
            const auto rider = runtime.viewers().agent(owner);
            check(rider && rider->seat && rider->seat->part == root.id, "Rejected teleport lost the source rider seat");
        }
        auto stopped = moved;
        stopped.position = physical ? transferred.position : Vector3{41, 46, 32};
        stopped.velocity = stopped.angular_velocity = stopped.acceleration = {};
        const auto expected_packet = object_terse_update(stopped, region.handle());
        const auto expected = std::get<std::vector<std::uint8_t>>(expected_packet.block("ObjectData").at("Data"));
        bool received = false;
        const auto receive_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!received && std::chrono::steady_clock::now() < receive_deadline) {
            {
                std::lock_guard lock(received_mutex);
                received = std::find(received_terse.begin(), received_terse.end(), expected) != received_terse.end();
            }
            if (!received) {
                runtime.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        if (!received) {
            const auto stats = viewer_socket->statistics();
            std::cerr << "Viewer UDP received=" << stats.received << " decode errors=" << stats.handler_errors;
            std::lock_guard lock(received_mutex);
            std::cerr << " terse rows=" << received_terse.size() << " expected:";
            for (auto byte : expected)
                std::cerr << ' ' << unsigned(byte);
            for (const auto &row : received_terse) {
                std::cerr << "\nreceived:";
                for (auto byte : row)
                    std::cerr << ' ' << unsigned(byte);
            }
            std::cerr << '\n';
            throw std::runtime_error("Seated teleport did not send the stopped-motion immediate terse packet over UDP");
        }
    } else
        check(transferred.rotation.w == moved.rotation.w && transferred.rotation.z == moved.rotation.z,
              "Unseated remote teleport incorrectly applied prepared rotation");
    check(bool(runtime.world().object(root.id)) != accept_remote,
          "Remote acceptance/rejection did not remove/recover the source object");
    runtime.stop();
    if (destination_runtime)
        destination_runtime->stop();
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--physical") {
            exercise(true, true, false, argv[2], std::make_shared<DefaultAnimations>(argv[3]));
            std::cout << "PASS: physical seated teleport between configured native regions\n";
            return 0;
        }
        check(argc == 1, "Unknown runtime test arguments");
        exercise(true);
        exercise(false);
        exercise(false, true);
        exercise(true, true);
        exercise(true, false, true);
        exercise(false, false, true);
        std::cout
            << "PASS: live local teleport, parcel capacity/identity, source land ownership, selection, child UUID, "
               "in-transit rejection, remote snapshot and recovery\n";
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
