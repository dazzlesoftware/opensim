// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/framework/text.hpp"
#include <fstream>
#include <atomic>
#include <chrono>
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

std::optional<int> counter(RegionRuntime &runtime, Uuid part, Uuid item) {
    const auto states = runtime.snapshot_task_script_states(part);
    const auto found = states.find(item);
    if (found == states.end())
        return {};
    const auto state = script::decode_lsl_checkpoint(found->second);
    if (!state || !state->running || state->globals.size() != 1)
        return {};
    return std::get<std::int32_t>(state->globals.front());
}

void scenario(bool disable_transfer) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "Keyframed runtime binding";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto owner = Uuid::random();
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = owner;
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Position event", 10, "");
    const std::string source = "integer count; default { state_entry() {} changed(integer flags) { if (flags & "
                               "CHANGED_POSITION) count += 1; } }";
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    int taints = 0;
    std::atomic<unsigned> sends = 0;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.disable_object_transfer = disable_transfer;
    policies.disable_underground_movement = true;
    policies.can_object_entry = [](const auto &, auto) { return false; };
    policies.group_move = [](auto, auto) { return false; };
    policies.parcel_prim_count_tainted = [&] { ++taints; };
    policies.script_parcel_at = [parcel](auto) { return parcel; };
    policies.load_asset = [asset](Uuid id) { return id == asset->full_id() ? asset : nullptr; };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    auto destination = region;
    destination.id = Uuid::random();
    destination.x = 256;
    policies.teleport_destination = [destination](std::uint64_t) { return std::optional{destination}; };
    policies.send_crossing_object = [&](auto &, auto, auto) {
        ++sends;
        return false;
    };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9002, 0, std::string("127.0.0.1")}));
    auto root = runtime.world().add(SceneObject::primitive(owner, {20, 20, 30}), false).value();
    auto child = SceneObject::primitive(owner, {1, 2, 3});
    child.parent_id = root.local_id;
    child = runtime.world().add(child, false).value();
    const auto root_item = Uuid::random(), child_item = Uuid::random();
    for (const auto &[part, id] : {std::pair{root.id, root_item}, std::pair{child.id, child_item}}) {
        auto item = std::make_shared<TaskInventoryItem>();
        item->item_id = id;
        item->parent_id = item->parent_part_id = part;
        item->asset_id = asset->full_id();
        item->owner_id = item->creator_id = owner;
        item->type = item->inventory_type = 10;
        item->script_running = true;
        runtime.restore_task_inventory(part, {item});
    }
    const auto wait = [&](int count) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            runtime.poll();
            if (counter(runtime, root.id, root_item) == count && counter(runtime, child.id, child_item) == count)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Root/child changed(POSITION) script count did not match");
    };
    wait(0);
    auto rider = std::make_shared<AgentCircuitData>();
    rider->agent_id = Uuid::random();
    rider->session_id = Uuid::random();
    rider->first_name = "Keyframed";
    rider->last_name = "Rider";
    check(runtime.viewers().add_npc(rider, owner), "Could not add seated NPC fixture");
    check(bool(runtime.viewers().update_seat(rider->agent_id, rider->session_id,
                                             ViewerAgent::Seat{child.id, root.local_id, {1, 2, 3}, {0, 0, 0, 1}},
                                             root.position)),
          "Could not seat NPC on child");
    check(runtime.world().store_keyframed_properties(root, Quaternion{0, 0, 1, 0}, {}, {}),
          "Could not rotate seated fixture");
    auto other_root = runtime.world().add(SceneObject::primitive(owner, {100, 100, 30}), false).value();
    auto other_rider = std::make_shared<AgentCircuitData>();
    other_rider->agent_id = Uuid::random();
    other_rider->session_id = Uuid::random();
    other_rider->first_name = "Unrelated";
    other_rider->last_name = "Rider";
    check(runtime.viewers().add_npc(other_rider, owner), "Could not add unrelated sitter");
    check(bool(runtime.viewers().update_seat(other_rider->agent_id, other_rider->session_id,
                                             ViewerAgent::Seat{other_root.id, other_root.local_id, {}, {0, 0, 0, 1}},
                                             other_root.position)),
          "Could not seat unrelated NPC");
    check(runtime.world().store_keyframed_position(other_root, {120, 120, 30}), "Could not prepare unrelated group");
    taints = 0;
    check(runtime.move_keyframed_object(root, {40, 50, -5}), "Live group move was rejected by script/viewer limits");
    check(runtime.world().object(root.id)->position == Vector3{40, 50, -5}, "Live group target was clamped");
    check(runtime.world().object(child.id)->position == child.position, "Child offset changed");
    check(taints == 1, "Parcel counts were not tainted exactly once");
    check(runtime.viewers().agent(rider->agent_id)->position == Vector3{39, 48, -2},
          "Seated avatar did not follow new root position and rotated offset");
    check(runtime.viewers().agent(other_rider->agent_id)->position == other_root.position,
          "Group position setter refreshed an unrelated group's sitter");
    wait(1);
    taints = 0;
    check(runtime.move_keyframed_object(root, {40.00025f, 50.00025f, -5}), "Small group move failed");
    check(taints == 1, "Small move did not invalidate parcel counts");
    wait(1);
    check(runtime.viewers().remove_npc(rider->agent_id), "Could not remove seated NPC fixture");
    check(runtime.viewers().remove_npc(other_rider->agent_id), "Could not remove unrelated NPC fixture");
    if (disable_transfer) {
        check(runtime.move_keyframed_object(root, {300, 50, -5}), "Disabled-transfer group move failed");
        check(runtime.world().object(root.id)->position.x == 300 && sends == 0, "Disabled transfer queued crossing");
        wait(2);
    } else {
        const auto before = runtime.world().object(root.id)->position;
        check(!runtime.move_keyframed_object(root, {300, 50, -5}), "Crossing applied target locally");
        check(!runtime.move_keyframed_object(root, {301, 50, -5}), "Pending crossing applied target locally");
        check(runtime.world().object(root.id)->position == before, "Crossing changed source position early");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (sends == 0 && std::chrono::steady_clock::now() < deadline) {
            runtime.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        check(sends == 1, "Crossing was missing or queued more than once");
    }
    auto sandbox = SceneObject::primitive(owner, {80, 80, 30});
    sandbox.flags = 1;
    sandbox.sandboxed = true;
    sandbox.sandbox_origin = sandbox.position;
    sandbox = runtime.world().add(sandbox, false).value();
    check(!runtime.move_keyframed_object(sandbox, {91, 80, 30}), "Sandbox move was accepted");
    check(runtime.world().object(sandbox.id)->position == sandbox.position &&
              !(runtime.world().object(sandbox.id)->flags & 1u),
          "Sandbox did not stop physics and retain position");
    check(runtime.world().remove(sandbox.id), "Sandbox removal failed");
    auto replacement = sandbox;
    replacement.local_id = 0;
    runtime.world().add(replacement, false);
    check(!runtime.move_keyframed_object(sandbox, {90, 90, 30}), "Stale movement reached replacement");
    runtime.stop();
}

void scheduled_playback(bool real_clock, bool selection = false) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "Scheduled keyframes";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto owner = Uuid::random();
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = owner;
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Motion events", 10, "");
    const std::string source = "integer count; default { moving_start() {count += 100;} moving_end() {count += 1000;} "
                               "changed(integer flags) {if(flags & CHANGED_POSITION) count += 1;} }";
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    double now = 1000;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    if (!real_clock)
        policies.keyframed_motion_clock = [&] { return now; };
    policies.script_parcel_at = [parcel](auto) { return parcel; };
    policies.load_asset = [asset](Uuid id) { return id == asset->full_id() ? asset : nullptr; };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9003, 0, std::string("127.0.0.1")}));
    auto root = runtime.world().add(SceneObject::primitive(owner, {20, 20, 30}), false).value();
    auto child = SceneObject::primitive(owner, {1, 2, 3});
    child.parent_id = root.local_id;
    child = runtime.world().add(child, false).value();
    const auto root_item = Uuid::random(), child_item = Uuid::random();
    for (const auto &[part, id] : {std::pair{root.id, root_item}, std::pair{child.id, child_item}}) {
        auto item = std::make_shared<TaskInventoryItem>();
        item->item_id = id;
        item->parent_id = item->parent_part_id = part;
        item->owner_id = item->creator_id = owner;
        item->asset_id = asset->full_id();
        item->type = item->inventory_type = 10;
        item->script_running = true;
        runtime.restore_task_inventory(part, {item});
    }
    const auto wait = [&](int root_count, int child_count) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            runtime.poll();
            if (counter(runtime, root.id, root_item) == root_count &&
                counter(runtime, child.id, child_item) == child_count)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Scheduled motion script events did not match");
    };
    wait(0, 0);
    KeyframedMotionFrame frame;
    frame.position = Vector3{4, 0, 0};
    frame.rotation = Quaternion{0, 0, 0, 1};
    frame.time_ms = 200;
    check(runtime.install_keyframed_motion(root, script::KeyframedMotionMode::Forward, {frame}),
          "Could not install native playback");
    check(runtime.teleport_object(root.id, root.id, {80, 80, 30}, {0, 0, 0, 1}, 0) == -1,
          "Object teleport accepted a configured keyframed motion");
    if (selection) {
        wait(100, 0);
        now += 50;
        wait(101, 1);
        check(runtime.set_object_selected(child, true) && runtime.set_object_selected(root, true),
              "Could not select linkset parts");
        now += 50;
        runtime.poll();
        check(runtime.keyframed_motion_state(root.id)->selected &&
                  runtime.keyframed_motion_state(root.id)->timer_stopped &&
                  runtime.world().object(root.id)->position == Vector3{21, 20, 30},
              "Selected motion kept advancing");
        check(runtime.move_keyframed_object(root, {40, 20, 30}), "Could not move selected linkset");
        wait(102, 2);
        check(runtime.set_object_selected(child, false) && runtime.keyframed_motion_state(root.id)->selected,
              "Partial deselection resumed group motion");
        now += 50;
        runtime.poll();
        check(runtime.set_object_selected(root, false), "Could not release final selected part");
        wait(202, 2);
        for (int tick = 1; tick <= 3; ++tick) {
            now += 50;
            wait(202 + tick, 2 + tick);
            check(runtime.world().object(root.id)->position == Vector3{40.f + tick, 20, 30},
                  "Deselected frames were not rebased");
        }
        now += 50;
        wait(1205, 5);
    } else if (!real_clock) {
        wait(100, 0);
        for (int tick = 1; tick <= 4; ++tick) {
            now += 50;
            wait(100 + tick, tick);
            check(runtime.world().object(root.id)->position == Vector3{20.f + tick, 20, 30},
                  "Scheduled playback position differs");
            if (tick < 4)
                check(runtime.world().object(root.id)->velocity == Vector3{20, 0, 0}, "Scheduled velocity missing");
        }
        now += 50;
        wait(1104, 4);
    } else {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool finished = false;
        do {
            runtime.poll();
            const auto state = runtime.keyframed_motion_state(root.id);
            const auto root_count = counter(runtime, root.id, root_item),
                       child_count = counter(runtime, child.id, child_item);
            if (state && !state->running && root_count && child_count && *child_count > 0 &&
                *root_count - *child_count == 1100) {
                finished = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        check(finished && runtime.world().object(root.id)->position == Vector3{24, 20, 30},
              "Real clock playback did not finish at its target with root-only moving events");
    }
    const auto completed = runtime.keyframed_motion_state(root.id);
    check(completed && !completed->running && completed->timer_stopped &&
              runtime.world().object(root.id)->velocity == Vector3{},
          "Completed motion did not stop");
    check(runtime.teleport_object(root.id, root.id, {80, 80, 30}, {0, 0, 0, 1}, 0) == -1,
          "Completed but retained motion did not block object teleport");
    now = 2000;
    check(runtime.install_keyframed_motion(root, script::KeyframedMotionMode::Loop, {frame}),
          "Could not replace motion");
    check(runtime.world().remove(root.id), "Could not remove animated root");
    auto replacement = root;
    replacement.local_id = 0;
    replacement.position = {80, 80, 30};
    runtime.world().add(replacement, false);
    now += 50;
    runtime.poll();
    check(!runtime.keyframed_motion_state(root.id) && runtime.world().object(root.id)->position == Vector3{80, 80, 30},
          "Stale registered motion reached a replacement instance");
    runtime.stop();
}

void crossing_playback(bool service) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "KFM rejected crossing";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    double now = 1000;
    std::atomic<bool> sending = false, release = false, stopped_snapshot = false;
    std::atomic<unsigned> sends = 0;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.keyframed_motion_clock = [&] { return now; };
    if (service) {
        auto destination = region;
        destination.id = Uuid::random();
        destination.x = 256;
        policies.teleport_destination = [destination](std::uint64_t) { return std::optional{destination}; };
        policies.send_crossing_object = [&](auto &, auto, std::shared_ptr<services::SimulationObject> object) {
            const auto xml = object->to_xml2();
            const auto asset = read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size()});
            stopped_snapshot = asset && asset->linksets.front().parts.front().object.velocity == Vector3{};
            ++sends;
            sending = true;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!release && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return false;
        };
    }
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9004, 0, std::string("127.0.0.1")}));
    auto root = runtime.world().add(SceneObject::primitive(Uuid::random(), {254, 20, 30}), false).value();
    KeyframedMotionFrame frame;
    frame.position = Vector3{4, 0, 0};
    frame.time_ms = 200;
    check(runtime.install_keyframed_motion(root, script::KeyframedMotionMode::Forward, {frame}),
          "Could not install crossing motion");
    now += 50;
    runtime.poll();
    check(runtime.world().object(root.id)->position.x == 255, "Crossing fixture first step differs");
    now += 50;
    runtime.poll();
    if (service) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!sending && std::chrono::steady_clock::now() < deadline) {
            runtime.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto waiting = runtime.keyframed_motion_state(root.id);
        check(sending && waiting && waiting->crossing && waiting->waiting_crossing && waiting->timer_stopped &&
                  runtime.world().object(root.id)->velocity == Vector3{} && stopped_snapshot,
              "Motion was not stopped before crossing snapshot");
        now += 50;
        runtime.poll();
        release = true;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool recovered = false;
    do {
        runtime.poll();
        const auto state = runtime.keyframed_motion_state(root.id);
        if (state && state->crossing && !state->waiting_crossing && state->running && !state->timer_stopped &&
            state->skip_loops == 1200) {
            recovered = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    check(recovered, "Failed crossing did not resume with original retry delay");
    check(runtime.world().object(root.id)->position.x == 255 && runtime.world().object(root.id)->velocity == Vector3{},
          "Crossing recovery altered source pose or velocity");
    for (int tick = 0; tick < 3; ++tick) {
        now += 50;
        runtime.poll();
    }
    check(runtime.keyframed_motion_state(root.id)->skip_loops == 1197 && sends == (service ? 1u : 0u),
          "Crossing retry delay was not retained");
    runtime.stop();
}

void persisted_playback() {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "KFM persistence";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    double now = 1000;
    const auto policies = [&] {
        RegionPolicies p;
        p.access = [](Uuid, const auto &, auto &) { return true; };
        p.authorize = [](const auto &, auto &) { return true; };
        p.keyframed_motion_clock = [&] { return now; };
        return p;
    };
    const auto listener = [] {
        return std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9005, 0, std::string("127.0.0.1")});
    };
    const auto path = std::filesystem::temp_directory_path() / ("opensim-kfm-" + Uuid::random().str() + ".xml");

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};

    Uuid id;
    {
        RegionRuntime runtime(region, presence, policies());
        runtime.start(listener());
        auto root = runtime.world().add(SceneObject::primitive(Uuid::random(), {20, 20, 30}), true).value();
        id = root.id;
        KeyframedMotionFrame frame;
        frame.position = Vector3{4, 0, 0};
        frame.time_ms = 200;
        check(runtime.install_keyframed_motion(root, script::KeyframedMotionMode::Forward, {frame}),
              "Could not start saved motion");
        now += 50;
        runtime.poll();
        const auto objects = runtime.snapshot_scene_objects();
        check(objects.size() == 1 && !objects.front().keyframed_motion.empty() && objects.front().position.x == 21,
              "Persistence snapshot did not include current pose and motion");
        std::u16string text;
        XmlTextWriterIo io;
        io.encoding = [] { return std::shared_ptr<NativeXmlTextEncoding>{}; };
        io.write = [&](auto, auto value) {
            if (value)
                text.append(*value);
        };
        io.write_array = [&](auto data, int offset, int count) { text.append(data->data() + offset, count); };
        io.write_whole_array = [&](auto data) { text.append(data->data(), data->size()); };
        NativeXmlTextWriter writer(std::move(io));
        write_object_asset_xml(writer, ObjectAssetPart{objects.front()}, {});
        std::ofstream file(path, std::ios::binary);
        file << utf8(text);
        file.close();
        check(bool(file), "Could not write saved object XML");
        runtime.stop();
    }
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    const auto saved = read_object_asset_xml(bytes);
    check(bool(saved), "Could not load saved motion XML");
    now = 5000;
    RegionRuntime restored(region, presence, policies());
    restored.start(listener());
    auto object = saved->linksets.front().parts.front().object;
    object.local_id = 0;
    check(bool(restored.world().add(object, true)), "Could not restore saved object");
    restored.poll();
    auto state = restored.keyframed_motion_state(id);
    check(state && state->running && state->current.time_ms == 200 && restored.world().object(id)->position.x == 21,
          "Restart did not retain current motion cursor and pose");
    for (int tick = 0; tick < 4; ++tick) {
        now += 50;
        restored.poll();
    }
    state = restored.keyframed_motion_state(id);
    check(state && !state->running && restored.world().object(id)->position == Vector3{24, 20, 30},
          "Saved motion did not resume to its original target after clean runtime restart");
    restored.stop();
}

void accepted_crossing_playback() {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion source;
    source.id = Uuid::random();
    source.name = "KFM source";
    source.external_host = "127.0.0.1";
    source.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    source.size_x = source.size_y = 256;
    auto destination = source;
    destination.id = Uuid::random();
    destination.name = "KFM destination";
    destination.x = 256;
    const auto owner = Uuid::random();
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = owner;
    double source_now = 1000, destination_now = 5000;
    const auto policies = [parcel] {
        RegionPolicies p;
        p.access = [](Uuid, const auto &, auto &) { return true; };
        p.authorize = [](const auto &, auto &) { return true; };
        p.script_parcel_at = [parcel](auto) { return parcel; };
        return p;
    };
    auto receiving = policies();
    receiving.keyframed_motion_clock = [&] { return destination_now; };
    RegionRuntime receiver(destination, presence, std::move(receiving));
    auto listener = std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9006, 0, std::string("127.0.0.1")});
    receiver.start(listener);
    auto sending = policies();
    sending.keyframed_motion_clock = [&] { return source_now; };
    sending.teleport_destination = [destination](std::uint64_t) { return std::optional{destination}; };
    std::atomic<bool> payload = false;
    sending.send_crossing_object = [&](auto &, Vector3 point, std::shared_ptr<services::SimulationObject> object) {
        const auto xml = object->to_xml2();
        const auto decoded = read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size()});
        payload = decoded && !decoded->linksets.front().parts.front().object.keyframed_motion.empty();
        auto target = destination;
        return receiver.create_object(&target, point, object->clone_for_new_scene(), true);
    };
    RegionRuntime sender(source, presence, std::move(sending));
    sender.start(listener);
    const auto root = sender.world().add(SceneObject::primitive(owner, {254, 20, 30}), true).value();
    KeyframedMotionFrame frame;
    frame.position = Vector3{4, 0, 0};
    frame.time_ms = 200;
    check(sender.install_keyframed_motion(root, script::KeyframedMotionMode::Forward, {frame}),
          "Could not install accepted crossing motion");
    source_now += 50;
    sender.poll();
    source_now += 50;
    sender.poll();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        sender.poll();
        receiver.poll();
        if (!sender.world().object(root.id) && receiver.keyframed_motion_state(root.id))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    check(payload && !sender.world().object(root.id) && receiver.world().object(root.id),
          "Motion crossing payload or destination object missing");
    sender.poll();
    const auto arrived = receiver.keyframed_motion_state(root.id);
    // Original GetObjectDestination clamps entry to .2 metres. Preserve the
    // same float subtraction/addition when rebasing from serialized origin255.
    const float delta = .2f - 255.f;
    const float target = 258.f + delta;
    check(!sender.keyframed_motion_state(root.id) && arrived && arrived->running && !arrived->crossing &&
              arrived->current.time_ms == 150 && arrived->current.position->x == target &&
              receiver.world().object(root.id)->position.x == .2f,
          "Destination did not retain and rebase crossing motion");
    for (int tick = 0; tick < 3; ++tick) {
        destination_now += 50;
        receiver.poll();
    }
    check(receiver.world().object(root.id)->position == Vector3{target, 20, 30} &&
              !receiver.keyframed_motion_state(root.id)->running,
          "Accepted crossing motion did not finish at its destination target");
    sender.stop();
    receiver.stop();
}
} // namespace

int main() {
    try {
        scenario(false);
        scenario(true);
        scheduled_playback(false);
        scheduled_playback(true);
        scheduled_playback(false, true);
        crossing_playback(false);
        crossing_playback(true);
        persisted_playback();
        accepted_crossing_playback();
        std::cout << "PASS: live keyframed group movement, root/child script events, crossing and sandbox\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
