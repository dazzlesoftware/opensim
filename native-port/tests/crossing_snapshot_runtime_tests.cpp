// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/script/lsl_fixed_array.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace services;
using namespace scene;

namespace {
enum class RiderCase {
    None,
    Compatible,
    UnsupportedWearable,
    UnsupportedBake,
    ModernWearable,
    AppearanceOverride,
    StopRetained,
    LinkedChild,
    StopLinkedChild
};

std::optional<script::LslCheckpoint> transferred_checkpoint(std::string_view snapshot, Uuid item) {
    const auto state = snapshot.find("UUID=\"" + item.str() + "\"");
    if (state == std::string_view::npos)
        return std::nullopt;
    constexpr std::string_view tag = "<NativeCheckpoint>";
    const auto start = snapshot.find(tag, state);
    const auto state_end = snapshot.find("</State>", state);
    if (start == std::string_view::npos || start >= state_end)
        return std::nullopt;
    const auto end = snapshot.find("</NativeCheckpoint>", start + tag.size());
    if (end == std::string_view::npos || end >= state_end)
        return std::nullopt;
    network::Value binary;
    binary.kind = network::Value::Kind::Binary;
    binary.text = snapshot.substr(start + tag.size(), end - start - tag.size());
    return script::decode_lsl_checkpoint(*network::osd_binary(binary));
}

void check_crossing_snapshot(bool stop_during_lookup, RiderCase rider_case = RiderCase::None) {
    const bool seated = rider_case != RiderCase::None;
    const bool linked = rider_case == RiderCase::LinkedChild || rider_case == RiderCase::StopLinkedChild;
    const bool denied = rider_case == RiderCase::UnsupportedWearable || rider_case == RiderCase::UnsupportedBake;
    const float negotiated_version = rider_case == RiderCase::ModernWearable ? .8f : .61f;
    NullPresenceStore store;
    PresenceService presence(store);
    GridRegion source;
    source.id = Uuid::random();
    source.name = "Crossing snapshot test";
    source.external_host = "127.0.0.1";
    source.internal_endpoint = RegionEndpoint{"127.0.0.1", 0};
    source.size_x = source.size_y = 256;
    auto destination = source;
    destination.id = Uuid::random();
    destination.x = 256;
    auto root = SceneObject::primitive(Uuid::random(), {250, 120, 26});
    auto child = SceneObject::primitive(root.owner, {1, 0, 0});
    const auto root_script = Uuid::random(), child_script = Uuid::random();
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Crossing script", 10, "");
    const std::string script = R"LSL(integer checkpoint_value;
    default {
        state_entry() {
            llSetPos(<270,120,26>);
            checkpoint_value = 42;
            llSetObjectDesc("changed during lookup");
        }
    })LSL";
    asset->data = std::make_shared<std::vector<std::uint8_t>>(script.begin(), script.end());
    auto child_asset = std::make_shared<AssetBase>(Uuid::random(), "Child crossing script", 10, "");
    const std::string child_source = R"LSL(integer checkpoint_value = 43;
        default { state_entry() { llSetObjectDesc("child ready"); } })LSL";
    child_asset->data = std::make_shared<std::vector<std::uint8_t>>(child_source.begin(), child_source.end());
    std::atomic<bool> entered = false, release = false;
    std::atomic<unsigned> sends = 0;
    std::atomic<unsigned> access_queries = 0, rider_creations = 0;
    std::atomic<bool> rider_context_verified = false;
    std::atomic<bool> release_rider = false;
    RegionRuntime *runtime_ptr = nullptr;
    RegionPolicies policies;
    policies.background_admission = false;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.load_asset = [asset, child_asset](Uuid id) {
        return id == asset->full_id() ? asset : id == child_asset->full_id() ? child_asset : nullptr;
    };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    if (seated) {
        policies.teleport_access = [&](GridRegion &target, const ViewerAgent &rider, Vector3 position, bool teleport,
                                       LoginTransferContext &context) -> std::string {
            ++access_queries;
            if (target.id != destination.id || !rider.seat || rider.seat->part != root.id || teleport ||
                position != Vector3{4, 120, 26})
                return "Incorrect seated admission request";
            context.inbound_version = .37f;
            context.outbound_version = negotiated_version;
            context.wearables_count = 23;
            return {};
        };
        policies.teleport_arrived = [](const Uuid &, const Uuid &) { return false; };
        policies.teleport_create = [&](const GridRegion &, GridRegion &, GridRegion &target, const ViewerAgent &rider,
                                       Vector3 position, Vector3, LoginTransferContext &context, const Uuid &token,
                                       std::uint32_t flags, const AgentData *snapshot, std::function<bool()> &,
                                       std::string &reason) -> std::optional<network::lludp::Packet> {
            ++rider_creations;
            rider_context_verified = access_queries == 1 && sends == 1 && target.id == destination.id && rider.seat &&
                                     rider.seat->part == root.id && position == Vector3{4, 120, 26} &&
                                     token.is_zero() && flags == 0 && snapshot && context.inbound_version == .37f &&
                                     context.outbound_version == negotiated_version && context.wearables_count == 23;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!release_rider && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (!release_rider)
                throw std::runtime_error("Seated script retention test did not release rider creation");
            reason = "Controlled rider creation denial after accepted object handoff";
            return std::nullopt;
        };
    }
    policies.teleport_destination = [&](std::uint64_t) -> std::optional<GridRegion> {
        entered = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            if (stop_during_lookup) {
                if (release)
                    return destination;
            } else {
                const auto live = runtime_ptr->world().object(root.id);
                const auto live_child = linked ? runtime_ptr->world().object(child.id) : std::nullopt;
                if (live && live->description == "changed during lookup" &&
                    (!linked || (live_child && live_child->description == "child ready")))
                    return destination;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Crossing destination lookup did not make progress");
    };
    policies.send_crossing_object = [&](GridRegion &, Vector3, std::shared_ptr<SimulationObject> transfer) {
        ++sends;
        const auto xml = transfer->to_xml2();
        const auto parsed = read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size()});
        const auto snapshot = transfer->state_snapshot();
        if (!parsed || parsed->linksets.front().parts.front().object.description != "changed during lookup" ||
            !snapshot || snapshot->find("<NativeCheckpoint>") == std::string::npos)
            throw std::runtime_error("Crossing captured stale state before destination lookup");
        const auto checkpoint = transferred_checkpoint(*snapshot, root_script);
        if (!checkpoint || !checkpoint->running || checkpoint->globals.size() != 1 ||
            std::get<std::int32_t>(checkpoint->globals.front()) != 42)
            throw std::runtime_error("Crossing captured script globals before destination lookup completed");
        if (linked) {
            const auto saved_child = transferred_checkpoint(*snapshot, child_script);
            const auto &parts = parsed->linksets.front().parts;
            if (parts.size() != 2 || parts.back().object.id != child.id ||
                parts.back().object.position != Vector3{1, 0, 0} || !saved_child || !saved_child->running ||
                saved_child->globals.size() != 1 || std::get<std::int32_t>(saved_child->globals.front()) != 43)
                throw std::runtime_error("Crossing lost the linked child transform or running checkpoint");
        }
        return true;
    };
    RegionRuntime runtime(source, presence, std::move(policies));
    runtime_ptr = &runtime;
    auto listener = std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")});
    runtime.start(listener);
    const auto part = runtime.world().add(root, false);
    if (!part)
        throw std::runtime_error("Could not add crossing snapshot fixture");
    if (linked) {
        child.parent_id = part->local_id;
        if (!runtime.world().add(child, false))
            throw std::runtime_error("Could not add scripted crossing child");
    }
    if (seated) {
        namespace udp = network::lludp;
        auto circuit = std::make_shared<AgentCircuitData>();
        circuit->agent_id = root.owner;
        circuit->session_id = Uuid::random();
        circuit->secure_session_id = Uuid::random();
        circuit->circuit_code = 42;
        circuit->child = false;
        circuit->caps_path = Uuid::random().str();
        circuit->first_name = "Seated";
        circuit->last_name = "Context";
        circuit->start_position = root.position;
        circuit->appearance = std::make_shared<AvatarAppearance>();
        if (rider_case == RiderCase::UnsupportedWearable || rider_case == RiderCase::ModernWearable ||
            rider_case == RiderCase::AppearanceOverride)
            circuit->appearance->set_wearable(AvatarWearable::Universal,
                                              std::make_shared<AvatarWearable>(Uuid::random(), Uuid::random()));
        if (rider_case == RiderCase::UnsupportedBake) {
            circuit->appearance->set_wearable(AvatarWearable::Universal, std::make_shared<AvatarWearable>());
            circuit->appearance->texture->create_face(40)->texture_id(Uuid::random());
        }
        presence.login(root.owner.str(), circuit->session_id, circuit->secure_session_id);
        LoginTransferContext context;
        std::string reason;
        if (!runtime.create_agent(nullptr, source, circuit, 128, context, reason))
            throw std::runtime_error("Could not reserve seated viewer: " + reason);
        const network::UdpEndpoint viewer{"127.0.0.1", 43001};
        auto connect = udp::make_packet("UseCircuitCode");
        connect.sequence = 1;
        connect.block("CircuitCode")["Code"] = std::uint64_t{42};
        connect.block("CircuitCode")["ID"] = root.owner;
        connect.block("CircuitCode")["SessionID"] = circuit->session_id;
        runtime.viewers().receive(viewer, udp::encode_datagram(connect));
        const auto connect_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!runtime.viewers().agent(root.owner) && std::chrono::steady_clock::now() < connect_deadline) {
            runtime.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!runtime.viewers().agent(root.owner))
            throw std::runtime_error("Context viewer did not connect");
        auto movement = udp::make_packet("CompleteAgentMovement");
        movement.sequence = 2;
        movement.block("AgentData")["AgentID"] = root.owner;
        movement.block("AgentData")["SessionID"] = circuit->session_id;
        movement.block("AgentData")["CircuitCode"] = std::uint64_t{42};
        runtime.viewers().receive(viewer, udp::encode_datagram(movement));
        const auto arrival_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool arrived = false;
        do {
            runtime.poll();
            const auto rider = runtime.viewers().agent(root.owner);
            if (rider && rider->root) {
                arrived = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < arrival_deadline);
        if (!arrived ||
            !runtime.viewers().update_seat(root.owner, circuit->session_id,
                                           ViewerAgent::Seat{root.id, part->local_id, {}, {0, 0, 0, 1}}, root.position))
            throw std::runtime_error("Could not seat connected context viewer");
        if (rider_case == RiderCase::AppearanceOverride &&
            !runtime.viewers().update_appearance(root.owner, circuit->session_id, std::make_shared<AvatarAppearance>()))
            throw std::runtime_error("Could not apply the compatible live appearance override");
    }
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = root_script;
    item->parent_id = item->parent_part_id = root.id;
    item->asset_id = asset->full_id();
    item->owner_id = item->creator_id = root.owner;
    item->type = item->inventory_type = 10;
    item->script_running = true;
    if (linked) {
        auto child_item = std::make_shared<TaskInventoryItem>(*item);
        child_item->item_id = child_script;
        child_item->parent_id = child_item->parent_part_id = child.id;
        child_item->asset_id = child_asset->full_id();
        runtime.restore_task_inventory(child.id, {child_item});
    }
    runtime.restore_task_inventory(root.id, {item});
    unsigned retained_polls = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.poll();
        if (stop_during_lookup && entered) {
            runtime.prepare_stop();
            release = true;
            runtime.stop();
            if (sends != 0 || !runtime.world().object(root.id))
                throw std::runtime_error("Stopping during lookup sent or removed the source object");
            return;
        }
        if (!stop_during_lookup && !runtime.world().object(root.id) && (!seated || rider_creations != 0)) {
            if (denied)
                throw std::runtime_error("Incompatible seated appearance crossed instead of retaining its source seat");
            if (seated) {
                const auto inventory = runtime.snapshot_task_inventory(root.id);
                const auto states = runtime.snapshot_task_script_states(root.id);
                if (!release_rider) {
                    const auto state = states.find(item->item_id);
                    const auto checkpoint =
                        state == states.end() ? std::nullopt : script::decode_lsl_checkpoint(state->second);
                    if (!inventory || inventory->size() != 1 || inventory->front().script_running || !checkpoint ||
                        checkpoint->running || checkpoint->globals.size() != 1 ||
                        std::get<std::int32_t>(checkpoint->globals.front()) != 42)
                        throw std::runtime_error("Seated source script was not retained stopped during rider creation");
                    if (linked) {
                        const auto child_inventory = runtime.snapshot_task_inventory(child.id);
                        const auto child_states = runtime.snapshot_task_script_states(child.id);
                        const auto state = child_states.find(child_script);
                        const auto checkpoint =
                            state == child_states.end() ? std::nullopt : script::decode_lsl_checkpoint(state->second);
                        if (!child_inventory || child_inventory->size() != 1 ||
                            child_inventory->front().script_running || !checkpoint || checkpoint->running ||
                            checkpoint->globals.size() != 1 ||
                            std::get<std::int32_t>(checkpoint->globals.front()) != 43 ||
                            runtime.world().object(child.id))
                            throw std::runtime_error(
                                "Linked child script was not retained stopped after source removal");
                    }
                    if (++retained_polls >= 3) {
                        release_rider = true;
                        if (rider_case == RiderCase::StopRetained || rider_case == RiderCase::StopLinkedChild) {
                            runtime.stop();
                            if (runtime.snapshot_task_inventory(root.id) ||
                                !runtime.snapshot_task_script_states(root.id).empty() || access_queries != 1 ||
                                rider_creations != 1 || !rider_context_verified)
                                throw std::runtime_error("Shutdown did not dispose retained seated scripts");
                            if (linked && (runtime.snapshot_task_inventory(child.id) ||
                                           !runtime.snapshot_task_script_states(child.id).empty()))
                                throw std::runtime_error("Shutdown did not dispose retained child scripts");
                            return;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                if (inventory || !states.empty() ||
                    (linked && (runtime.snapshot_task_inventory(child.id) ||
                                !runtime.snapshot_task_script_states(child.id).empty()))) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
            }
            runtime.stop();
            if (sends != 1)
                throw std::runtime_error("Crossing did not send exactly one fresh snapshot");
            if (seated && (access_queries != 1 || rider_creations != 1 || !rider_context_verified))
                throw std::runtime_error("Seated crossing repeated admission or lost the negotiated context");
            return;
        }
        if (denied) {
            const auto retained = runtime.world().object(root.id);
            if (retained && retained->attached_to_backup) {
                const auto rider = runtime.viewers().agent(root.owner);
                if (access_queries != 1 || sends != 0 || rider_creations != 0 || !rider || !rider->seat ||
                    rider->seat->part != root.id)
                    throw std::runtime_error("Incompatible seated appearance was sent or lost its source seat");
                runtime.stop();
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    release = true;
    throw std::runtime_error("Crossing snapshot runtime test timed out");
}

void check_large_crossing_receiver_checkpoint() {
    NullPresenceStore store;
    PresenceService presence(store);
    GridRegion region;
    region.id = Uuid::random();
    region.name = "Large checkpoint crossing receiver";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const std::string source = R"LSL(YOption arrays;
        object input; object alias; integer after;
        default {
            state_entry() {}
            changed(integer change) {
                if (input == alias && xmrFixedArrayGetF(input,0) == 1.25 &&
                    xmrFixedArrayGetF(input,300000) == -7.5 &&
                    xmrFixedArrayGetF(input,599999) == 2.5) {
                    ++after;
                    llSetObjectDesc("capacity arrived");
                }
            }
        })LSL";
    const auto compiled = script::compile_lsl(source);
    if (!compiled)
        throw std::runtime_error("Large crossing receiver source did not compile");
    script::LslHost host;
    host.heap_limit_bytes = 32 * 1024 * 1024;
    script::LslInstance initial(compiled.program, {}, host);
    if (initial.run(10000).status != script::LslRunStatus::Idle)
        throw std::runtime_error("Large crossing receiver source did not initialize");
    auto checkpoint = initial.checkpoint();
    if (!checkpoint || checkpoint->globals.size() != 3)
        throw std::runtime_error("Large crossing receiver globals unavailable");
    const auto value = script::make_lsl_fixed_array(script::LslFixedArrayKind::Float, 600000);
    const auto expected = std::get<std::shared_ptr<const script::LslFixedArray>>(value);
    script::lsl_fixed_array_set(expected, 0, 1.25);
    script::lsl_fixed_array_set(expected, 300000, -7.5);
    script::lsl_fixed_array_set(expected, 599999, 2.5);
    checkpoint->globals = {value, value, std::int32_t{0}};
    // Recompute accounting on external restoration rather than retain the empty
    // source instance's cached global charge.
    checkpoint->arrays_heap_usage.reset();
    checkpoint->locals_heap_usage.reset();
    checkpoint->local_heap_base.reset();
    checkpoint->local_object_heap_usage.clear();
    checkpoint->heap_restore_pending = false;
    const auto wire = script::encode_lsl_checkpoint(*checkpoint);
    const auto encoded =
        network::binary_base64(network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(wire)));
    if (encoded.size() <= 6 * 1024 * 1024)
        throw std::runtime_error("Large crossing receiver fixture did not cross encoded-size boundary");
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Large crossing receiver script", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    ObjectAssetPart part;
    part.object = SceneObject::primitive(Uuid::random(), {20, 20, 30});
    TaskInventoryItem item;
    item.item_id = Uuid::random();
    item.parent_id = item.parent_part_id = part.object.id;
    item.asset_id = asset->full_id();
    item.owner_id = item.creator_id = part.object.owner;
    item.type = item.inventory_type = 10;
    item.script_running = true;
    part.inventory.push_back(item);
    std::u16string output;
    XmlTextWriterIo io;
    io.encoding = [] { return std::shared_ptr<NativeXmlTextEncoding>{}; };
    io.write = [&](auto, auto text) {
        if (text)
            output.append(*text);
    };
    io.write_array = [&](auto text, int offset, int count) { output.append(text->data() + offset, count); };
    io.write_whole_array = [&](auto text) { output.append(text->data(), text->size()); };
    NativeXmlTextWriter writer(std::move(io));
    write_object_asset_xml(writer, part, {});
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = part.object.owner;
    RegionPolicies policies;
    policies.script_parcel_at = [parcel](auto) { return parcel; };
    policies.background_admission = false;
    policies.script_heap_limit_bytes = 32 * 1024 * 1024;
    policies.load_asset = [asset](Uuid id) { return id == asset->full_id() ? asset : nullptr; };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    RegionRuntime receiver(region, presence, std::move(policies));
    receiver.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")}));
    const auto scene = receiver.get_scene(region.id);
    const auto transfer = std::dynamic_pointer_cast<SimulationObject>(scene->deserialize_object(utf8(output)));
    if (!transfer)
        throw std::runtime_error("Large crossing receiver could not deserialize object");
    const std::string state = "<ScriptData><ScriptStates><State Engine=\"NativeLSL\" UUID=\"" + item.item_id.str() +
                              "\" Asset=\"" + item.asset_id.str() +
                              "\"><ScriptState><Running>True</Running><NativeCheckpoint>" + encoded +
                              "</NativeCheckpoint></ScriptState></State></ScriptStates></ScriptData>";
    transfer->set_state(state, scene.get());
    if (!receiver.create_object(&region, part.object.position, transfer, false))
        throw std::runtime_error("Large crossing receiver rejected object admission");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool completed = false;
    do {
        receiver.poll();
        const auto object = receiver.world().object(part.object.id);
        if (object && object->description == "capacity arrived") {
            completed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    const auto inventory = receiver.snapshot_task_inventory(part.object.id);
    const auto states = receiver.snapshot_task_script_states(part.object.id);
    const auto saved = states.find(item.item_id);
    const auto restored = saved == states.end() ? std::nullopt : script::decode_lsl_checkpoint(saved->second);
    if (!completed || !inventory || inventory->size() != 1 || inventory->front().asset_id != item.asset_id ||
        !inventory->front().script_running || !restored || restored->globals.size() != 3 ||
        std::get<std::int32_t>(restored->globals[2]) != 1)
        throw std::runtime_error("Large crossing receiver did not resume accepted script state");
    const auto actual = std::get<std::shared_ptr<const script::LslFixedArray>>(restored->globals[0]);
    if (actual != std::get<std::shared_ptr<const script::LslFixedArray>>(restored->globals[1]) ||
        std::get<std::vector<double>>(actual->cells) != std::get<std::vector<double>>(expected->cells))
        throw std::runtime_error("Large crossing receiver lost array cells or alias identity");
    receiver.stop();
}

} // namespace

int main() {
    try {
        check_large_crossing_receiver_checkpoint();
        check_crossing_snapshot(false);
        check_crossing_snapshot(true);
        for (const auto rider : {RiderCase::Compatible, RiderCase::UnsupportedWearable, RiderCase::UnsupportedBake,
                                 RiderCase::ModernWearable, RiderCase::AppearanceOverride, RiderCase::StopRetained,
                                 RiderCase::LinkedChild, RiderCase::StopLinkedChild})
            check_crossing_snapshot(false, rider);
        std::cout << "Delayed snapshot, stop during lookup, seated context and appearance checks passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
