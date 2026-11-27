// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/keyframed_motion_nrbf.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace scene;

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

void near(Vector3 actual, Vector3 expected, const char *message) {
    check(std::abs(actual.x - expected.x) < .0002f && std::abs(actual.y - expected.y) < .0002f &&
              std::abs(actual.z - expected.z) < .0002f,
          message);
}

std::unique_ptr<network::UdpSocket> connect_wearer(RegionRuntime &runtime, services::GridRegion &region,
                                                   services::PresenceService &presence, Uuid owner) {
    auto peer = std::make_shared<network::UdpSocket *>(nullptr);
    auto socket = std::make_unique<network::UdpSocket>(
        network::UdpSocketOptions{}, [peer](const network::UdpEndpoint &sender, std::span<const std::uint8_t> bytes) {
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
    circuit->circuit_code = 42;
    circuit->caps_path = Uuid::random().str();
    circuit->first_name = "Attachment";
    circuit->last_name = "Wearer";
    circuit->start_position = {100, 100, 30};
    presence.login(owner.str(), circuit->session_id, circuit->secure_session_id);
    services::LoginTransferContext context;
    std::string reason;
    check(runtime.create_agent(nullptr, region, circuit, 128, context, reason), "Could not reserve attachment wearer");
    auto use = network::lludp::make_packet("UseCircuitCode");
    use.sequence = 1;
    use.block("CircuitCode")["Code"] = std::uint64_t{42};
    use.block("CircuitCode")["ID"] = owner;
    use.block("CircuitCode")["SessionID"] = circuit->session_id;
    runtime.viewers().receive(endpoint, network::lludp::encode_datagram(use));
    bool sent_movement = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.poll();
        if (const auto wearer = runtime.viewers().agent(owner)) {
            if (wearer->root)
                return socket;
            if (!sent_movement) {
                auto movement = network::lludp::make_packet("CompleteAgentMovement");
                movement.sequence = 2;
                movement.block("AgentData")["AgentID"] = owner;
                movement.block("AgentData")["SessionID"] = circuit->session_id;
                movement.block("AgentData")["CircuitCode"] = std::uint64_t{42};
                runtime.viewers().receive(endpoint, network::lludp::encode_datagram(movement));
                sent_movement = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Attachment wearer did not complete authenticated arrival");
}

void transfer_projections(RegionRuntime &source, services::PresenceService &presence, Uuid owner, Uuid root) {
    const auto wearer = source.viewers().agent(owner).value();
    AgentData snapshot;
    source.snapshot_attachments(wearer, snapshot);
    check(snapshot.attachment_objects && snapshot.attachment_objects->size() == 1,
          "Avatar snapshot did not contain its single attachment");
    const auto transfer = std::dynamic_pointer_cast<services::SimulationObject>(snapshot.attachment_objects->get(0));
    check(bool(transfer), "Avatar attachment snapshot is not a simulation transfer object");
    const auto xml = transfer->to_xml2();
    const auto asset = read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size()});
    check(asset && asset->linksets.size() == 1, "Attachment transfer XML could not be decoded");
    const auto &projected = asset->linksets.front().parts.front().object;
    near(projected.position, {1, 2, 3}, "Attachment copy XML did not project GroupPosition to AttachedPos");
    check(!projected.is_attachment, "Attachment copy XML retained attached group state");
    const auto encoded = decode_keyframed_motion(projected.keyframed_motion);
    check(bool(encoded), "Attachment transfer XML omitted its copied motion");
    const auto &copied = encoded->playback;
    near(copied.serialized_position, {1, 2, 3}, "XML serialization did not update copied motion origin to offset");
    // Original m_timerStopped is NonSerialized. Decoding the NRBF payload gives
    // its CLR default false, independently of the copied kernel's stopped timer.
    if (!(copied.running && !copied.timer_stopped && !copied.selected && !copied.crossing && !copied.waiting_crossing &&
          !copied.skip_loops && copied.current.time_ms == 150 && copied.next_position == Vector3{} &&
          copied.current_velocity == Vector3{}))
        throw std::runtime_error(
            "Attachment decoded copy transient state: running=" + std::to_string(copied.running) +
            " timer_stopped=" + std::to_string(copied.timer_stopped) + " selected=" + std::to_string(copied.selected) +
            " crossing=" + std::to_string(copied.crossing) + " waiting=" + std::to_string(copied.waiting_crossing) +
            " skip=" + std::to_string(copied.skip_loops) + " remaining=" + std::to_string(copied.current.time_ms) +
            " next=" + std::to_string(copied.next_position.x) + "," + std::to_string(copied.next_position.y) + "," +
            std::to_string(copied.next_position.z) + " velocity=" + std::to_string(copied.current_velocity.x) + "," +
            std::to_string(copied.current_velocity.y) + "," + std::to_string(copied.current_velocity.z));
    near(*copied.current.position, {24, 20, 30}, "Attachment copy changed current target before destination admission");
    check(copied.frames.size() == 1 && copied.frame_list_version == 0,
          "Attachment copied frame list did not use the original fresh List copy state");
    const auto live = source.keyframed_motion_state(root).value();
    check(live.running && !live.timer_stopped && live.current.time_ms == 150,
          "Attachment snapshot stopped or advanced its live source component");
    near(*live.current.position, {24, 20, 30}, "Attachment snapshot mutated its live source target");

    for (bool xml_transport : {false, true}) {
        services::GridRegion region;
        region.id = Uuid::random();
        region.name = xml_transport ? "XML attachment transfer" : "Typed attachment transfer";
        region.external_host = "127.0.0.1";
        region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
        region.size_x = region.size_y = 256;
        double now = 2000;
        auto parcel = std::make_shared<ScriptParcel>();
        parcel->id = Uuid::random();
        parcel->owner = owner;
        RegionPolicies policies;
        policies.access = [](Uuid, const auto &, auto &) { return true; };
        policies.authorize = [](const auto &, auto &) { return true; };
        policies.keyframed_motion_clock = [&] { return now; };
        policies.script_parcel_at = [parcel](auto) { return parcel; };
        RegionRuntime destination(region, presence, std::move(policies));
        destination.start(std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9022, 0, std::string("127.0.0.1")}));
        auto socket = connect_wearer(destination, region, presence, owner);
        std::shared_ptr<services::SimulationObject> incoming;
        if (xml_transport)
            incoming = std::dynamic_pointer_cast<services::SimulationObject>(
                destination.get_scene(region.id)->deserialize_object(xml));
        else
            incoming = transfer->clone_for_new_scene();
        check(incoming && destination.create_object(&region, {}, incoming, true),
              "Destination did not admit its copied attachment transfer");
        const auto state = destination.keyframed_motion_state(root);
        check(state && state->running && state->current.time_ms == 150,
              "Copied attachment admission did not retain its active cursor");
        near(*state->current.position, xml_transport ? Vector3{24, 20, 30} : Vector3{-75, -78, 3},
             "Typed/XML attachment admission did not preserve their distinct original copied origins");
        now += 50;
        destination.poll();
        const auto restored = destination.world().object(root).value();
        near(restored.position, {1, 2, 3}, "Copied attachment transfer changed visible offset");
        near(*restored.attached_group_position, xml_transport ? Vector3{62, 60, 30} : Vector3{12.5f, 11, 16.5f},
             "Copied attachment playback did not use its admitted target and wearer position");
        socket->stop();
        destination.stop();
    }
}

void scenario(bool present_wearer) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = present_wearer ? "Present attachment wearer" : "Absent attachment wearer";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto owner = Uuid::random();
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = owner;
    const std::string source = "integer done; default {state_entry() {"
                               "llSetKeyframedMotion([\"bad\",0.2], [KFM_MODE,<1,2,3>]); done=1;}}";
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Attached motion eligibility", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    double now = 1000;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.keyframed_motion_clock = [&] { return now; };
    policies.script_parcel_at = [parcel](auto) { return parcel; };
    policies.load_asset = [asset](Uuid id) { return id == asset->full_id() ? asset : nullptr; };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9021, 0, std::string("127.0.0.1")}));
    std::unique_ptr<network::UdpSocket> socket;
    std::uint32_t wearer_local_id = runtime.world().allocate_local_id();
    if (present_wearer) {
        socket = connect_wearer(runtime, region, presence, owner);
        const auto wearer = runtime.viewers().agent(owner).value();
        wearer_local_id = wearer.local_id;
        near(wearer.position, {100, 100, 30}, "Authenticated wearer has an unexpected start position");
    }
    auto ground = SceneObject::primitive(owner, {20, 20, 30});
    ground.root_offset_position = Vector3{7, 8, 9};
    const auto root = runtime.world().add(ground, false).value();
    KeyframedMotionFrame frame;
    frame.position = Vector3{4, 0, 0};
    frame.rotation = Quaternion{0, 0, 1, 0};
    frame.time_ms = 200;
    check(runtime.install_keyframed_motion(root, script::KeyframedMotionMode::Forward, {frame}),
          "Could not install ground motion");
    now += 50;
    runtime.poll();
    near(runtime.world().object(root.id)->position, {21, 20, 30}, "Ground motion did not advance");
    const Vector3 offset{1, 2, 3};
    check(runtime.world().attach_from_ground(root.id, wearer_local_id, 5, offset, Uuid::random(), Uuid::random(),
                                             Uuid::random()),
          "Could not attach the moving ground object");
    check(runtime.keyframed_motion_state(root.id)->running, "Attachment transition removed running motion");
    near(*runtime.world().object(root.id)->attached_group_position, offset,
         "Attach did not initialize stored GroupPosition from the attachment offset");
    now += 50;
    runtime.poll();
    const auto attached = runtime.world().object(root.id).value();
    check(attached.is_attachment && attached.parent_id == wearer_local_id, "Timer detached the worn object");
    near(attached.position, offset, "Attachment timer changed the visible attachment offset");
    near(*attached.attached_group_position,
         present_wearer ? Vector3{100.f + (24.f - 100.f) / 3.f, 100.f + (20.f - 100.f) / 3.f, 30}
                        : Vector3{1.f + 23.f / 3.f, 8, 12},
         "Attachment GroupPosition did not distinguish wearer reads and hidden writes");
    near(attached.velocity, {20, 0, 0}, "Attachment timer lost ordinary root velocity writes");
    check(std::abs(attached.rotation.z - .38268343f) < .0001f && std::abs(attached.rotation.w - .9238795f) < .0001f,
          "Attachment timer did not write ordinary root rotation");
    check(runtime.keyframed_motion_state(root.id)->current.time_ms == 150,
          "Attachment transition rebased or stopped the timer cursor");

    std::optional<Uuid> restored_id;
    if (present_wearer) {
        transfer_projections(runtime, presence, owner, root.id);
        const auto snapshots = runtime.snapshot_scene_objects();
        const auto found =
            std::find_if(snapshots.begin(), snapshots.end(), [&](const auto &part) { return part.id == root.id; });
        check(found != snapshots.end() && !found->keyframed_motion.empty(), "Attachment snapshot omitted live motion");
        auto incoming = *found;
        incoming.id = Uuid::random();
        incoming.local_id = 0;
        // Imported raw GroupPosition is distinct from both AttachedPos and the
        // serialized origin. AttachToScene rebases before assigning the wearer.
        incoming.attached_group_position = Vector3{7, 8, 9};
        restored_id = incoming.id;
        check(runtime.world().add_crossing_linkset({incoming}, wearer_local_id),
              "Could not admit serialized moving attachment");
        runtime.poll();
        const auto restored = runtime.keyframed_motion_state(*restored_id);
        check(restored && restored->running && restored->current.time_ms == 150,
              "Attachment admission did not restore its running frame cursor");
        near(restored->serialized_position, {7, 8, 9}, "Attachment admission used wearer position before rebasing");
        near(*restored->current.position, {-69, -72, 9}, "Imported raw GroupPosition did not rebase the saved target");
    }

    const auto script_part = restored_id.value_or(root.id);
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = item->parent_part_id = script_part;
    item->asset_id = asset->full_id();
    item->owner_id = item->creator_id = owner;
    item->type = item->inventory_type = 10;
    item->script_running = true;
    runtime.restore_task_inventory(script_part, {item});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool completed = false;
    do {
        runtime.poll();
        const auto snapshots = runtime.snapshot_task_script_states(script_part);
        if (const auto found = snapshots.find(item->item_id); found != snapshots.end()) {
            const auto checkpoint = script::decode_lsl_checkpoint(found->second);
            completed = checkpoint && checkpoint->running && checkpoint->globals.size() == 1 &&
                        std::get<std::int32_t>(checkpoint->globals.front()) == 1;
            if (completed)
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    check(completed, "Malformed new frame call on an attached root did not short-circuit before parsing");
    check(runtime.keyframed_motion_state(script_part)->running &&
              runtime.keyframed_motion_state(script_part)->current.time_ms == 150,
          "Ineligible attachment script replaced or stopped existing motion");
    check(runtime.world().drop_attachment(root.id, wearer_local_id, {40, 50, 30}), "Could not drop moving attachment");
    check(runtime.keyframed_motion_state(root.id)->current.time_ms == 150,
          "Drop unexpectedly rebased the existing motion cursor");
    now += 50;
    runtime.poll();
    const auto dropped = runtime.world().object(root.id).value();
    check(!dropped.is_attachment && !dropped.parent_id && !dropped.attached_group_position,
          "Drop retained attachment identity or hidden position");
    near(dropped.saved_attachment_position, {1, 2, 3},
         "Active attachment drop changed AttachedPos to the independent root OffsetPosition");
    near(dropped.position, {32, 35, 30}, "Dropped motion was explicitly rebased instead of retaining its prior target");
    if (restored_id) {
        const auto restored = runtime.world().object(*restored_id).value();
        near(restored.position, offset, "Restored attachment motion changed its visible offset");
        near(*restored.attached_group_position, {15.5f, 14, 19.5f},
             "Restored attachment did not continue its saved frame after raw-position admission");
    }
    now += 50;
    runtime.poll();
    near(runtime.world().object(root.id)->position, {24, 20, 30}, "Dropped motion did not reach its retained target");
    now += 50;
    runtime.poll();
    check(!runtime.keyframed_motion_state(root.id)->running, "Dropped motion did not complete normally");
    if (restored_id)
        check(!runtime.keyframed_motion_state(*restored_id)->running, "Restored attachment motion did not complete");
    if (socket)
        socket->stop();
    runtime.stop();
}
} // namespace

int main() {
    try {
        scenario(false);
        scenario(true);
        std::cout << "PASS: attached motion wearer reads, hidden writes, drop, admission and script eligibility\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
