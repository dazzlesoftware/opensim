// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <chrono>
#include <iostream>
#include <map>
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

std::optional<int> counter(RegionRuntime &runtime, Uuid part, Uuid item) {
    const auto snapshots = runtime.snapshot_task_script_states(part);
    const auto found = snapshots.find(item);
    if (found == snapshots.end())
        return {};
    const auto state = script::decode_lsl_checkpoint(found->second);
    if (!state || !state->running || state->globals.size() != 1)
        return {};
    return std::get<std::int32_t>(state->globals.front());
}

void scenario(const std::filesystem::path &library, std::shared_ptr<const DefaultAnimations> animations,
              bool collidable) {
    NullPresenceStore store;
    PresenceService presence(store);
    GridRegion region;
    region.id = Uuid::random();
    region.name = collidable ? "Viewer physical actor cleanup" : "Viewer actor-free flags";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    auto assets = std::make_shared<std::map<Uuid, std::shared_ptr<AssetBase>>>();
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    double now = 1000;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.can_edit = [](Uuid, const SceneObject &) { return true; };
    policies.background_admission = false;
    policies.physics_library = library;
    policies.animations = std::move(animations);
    policies.collidable_prims = collidable;
    policies.keyframed_motion_clock = [&] { return now; };
    policies.script_parcel_at = [parcel](auto) { return parcel; };
    policies.load_asset = [assets](Uuid id) {
        const auto found = assets->find(id);
        return found == assets->end() ? nullptr : found->second;
    };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.world().terrain().clear_land(0);
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9021, 0, std::string("127.0.0.1")}));

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
    circuit->first_name = "Flags";
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

    parcel->owner = circuit->agent_id;
    auto object = SceneObject::primitive(circuit->agent_id, {20, 20, 30});
    object.physics.gravity = 0;
    const auto root = runtime.world().add(object, false).value();
    const std::string source = "integer count; default {state_entry() {} moving_start() {count+=100;}"
                               " moving_end() {count+=1000;}}";
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Viewer flag observer", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    assets->emplace(asset->full_id(), asset);
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = item->parent_part_id = root.id;
    item->asset_id = asset->full_id();
    item->owner_id = item->creator_id = circuit->agent_id;
    item->type = item->inventory_type = 10;
    item->script_running = true;
    runtime.restore_task_inventory(root.id, {item});
    wait_for(runtime, [&] { return counter(runtime, root.id, item->item_id) == 0; }, "Observer script did not load");
    runtime.poll();
    KeyframedMotionFrame frame;
    frame.position = Vector3{8, 0, 0};
    frame.time_ms = 400;
    check(runtime.install_keyframed_motion(root, script::KeyframedMotionMode::Forward, {frame}),
          "Could not install motion before viewer flags");
    wait_for(runtime, [&] { return counter(runtime, root.id, item->item_id) == 100; }, "Observer missed moving_start");
    now += 50;
    runtime.poll();
    check(runtime.keyframed_motion_state(root.id)->running, "Motion stopped before viewer command");
    auto flags = udp::make_packet("ObjectFlagUpdate");
    flags.sequence = 3;
    auto &data = flags.block("AgentData");
    data["AgentID"] = circuit->agent_id;
    data["SessionID"] = circuit->session_id;
    data["ObjectLocalID"] = std::uint64_t(root.local_id);
    data["UsePhysics"] = true;
    data["IsTemporary"] = false;
    data["IsPhantom"] = false;
    data["CastsShadows"] = false;
    runtime.viewers().receive(endpoint, udp::encode_datagram(flags));
    wait_for(
        runtime, [&] { return (runtime.world().object(root.id)->flags & 1u) != 0; },
        "Authenticated ObjectFlagUpdate did not enable physics flags");
    if (collidable) {
        wait_for(
            runtime,
            [&] {
                return !runtime.keyframed_motion_state(root.id) && counter(runtime, root.id, item->item_id) == 1100;
            },
            "Viewer physical actor creation did not stop and clear motion with moving_end");
    } else {
        const auto motion = runtime.keyframed_motion_state(root.id);
        check(motion && motion->running && counter(runtime, root.id, item->item_id) == 100,
              "Viewer flags without an actor cleared motion or posted moving_end");
    }
    const auto before = runtime.world().object(root.id)->position;
    now += 50;
    runtime.poll();
    runtime.poll();
    check(counter(runtime, root.id, item->item_id) == (collidable ? 1100 : 100),
          "Viewer physical synchronization posted an unexpected moving_end");
    check(collidable ? runtime.world().object(root.id)->position == before
                     : runtime.world().object(root.id)->position.x > before.x,
          "Motion did not stop with an actor or continue without one");
    bool found = false;
    for (const auto &part : runtime.snapshot_scene_objects())
        if (part.id == root.id) {
            found = true;
            check(part.keyframed_motion.empty() == collidable,
                  "Persistence snapshot lost retained motion or retained stopped motion bytes");
        }
    check(found, "Persistence snapshot omitted viewer object");
    runtime.stop();
    socket->stop();
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Physics library and animation catalog paths required");
        auto animations = std::make_shared<DefaultAnimations>(argv[2]);
        scenario(argv[1], animations, true);
        scenario(argv[1], animations, false);
        std::cout << "PASS: authenticated viewer physics flags, actor-dependent motion cleanup and persistence\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
