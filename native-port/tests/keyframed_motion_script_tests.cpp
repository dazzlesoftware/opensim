// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <mutex>
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

void script_scenario(bool child_starts) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = child_starts ? "Child scripted motion" : "Root scripted motion";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto owner = Uuid::random();
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = owner;
    auto assets = std::make_shared<std::map<Uuid, std::shared_ptr<AssetBase>>>();
    std::mutex log_mutex;
    std::vector<std::string> logs;
    double now = 1000;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.keyframed_motion_clock = [&] { return now; };
    policies.script_parcel_at = [parcel](auto) { return parcel; };
    policies.load_asset = [assets](Uuid id) {
        const auto found = assets->find(id);
        return found == assets->end() ? nullptr : found->second;
    };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    policies.log = [&](const std::string &message) {
        std::lock_guard lock(log_mutex);
        logs.push_back(message);
    };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9017, 0, std::string("127.0.0.1")}));
    const auto root = runtime.world().add(SceneObject::primitive(owner, {20, 20, 30}), false).value();
    auto child = SceneObject::primitive(owner, {1, 2, 3});
    child.parent_id = root.local_id;
    child = runtime.world().add(child, false).value();

    const auto add_script = [&](Uuid part, const std::string &source) {
        auto asset = std::make_shared<AssetBase>(Uuid::random(), "Scripted keyframes", 10, "");
        asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
        assets->emplace(asset->full_id(), asset);
        auto item = std::make_shared<TaskInventoryItem>();
        item->item_id = Uuid::random();
        item->parent_id = item->parent_part_id = part;
        item->asset_id = asset->full_id();
        item->owner_id = item->creator_id = owner;
        item->type = item->inventory_type = 10;
        item->script_running = true;
        runtime.restore_task_inventory(part, {item});
        return item->item_id;
    };
    const auto wait = [&](auto predicate, const char *message) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            runtime.poll();
            if (predicate())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error(message);
    };
    const std::string initial =
        "llSetKeyframedMotion([], [KFM_COMMAND, KFM_CMD_PLAY]);"
        "llSetKeyframedMotion([<4,0,0>, 0.2], [KFM_DATA, KFM_TRANSLATION, KFM_MODE, KFM_FORWARD]);";
    const auto observer = [&](bool starts) {
        return std::string("integer count; default { state_entry() {") + (starts ? initial : "") +
               "} moving_start() {count += 100;} moving_end() {count += 1000;} "
               "changed(integer flags) {if(flags & CHANGED_POSITION) count += 1;} }";
    };
    const auto root_item = add_script(root.id, observer(!child_starts));
    const auto child_item = add_script(child.id, observer(child_starts));
    wait([&] { return counter(runtime, root.id, root_item) == 100 && counter(runtime, child.id, child_item) == 0; },
         "LSL frame submission did not start root motion and moving_start");
    check(runtime.keyframed_motion_state(root.id)->running, "Submitted script motion is not running");
    check(!runtime.keyframed_motion_state(child.id), "Child script installed a child-local motion");

    const auto tick = [&](float x) {
        now += 50;
        runtime.poll();
        const auto actual = runtime.world().object(root.id)->position.x;
        if (std::abs(actual - x) >= .0001f)
            throw std::runtime_error(std::string(child_starts ? "Child" : "Root") + " scripted motion at clock " +
                                     std::to_string(now) + ": expected x=" + std::to_string(x) +
                                     ", actual x=" + std::to_string(actual));
        check(runtime.world().object(child.id)->position == child.position, "Scripted motion changed child offset");
    };
    const auto command = [&](const std::string &body, const std::string &expected_error = "") {
        // A new script-bearing child makes each control call through actual
        // production script loading, without resetting the event observers.
        auto controller = SceneObject::primitive(owner, {0, 0, 1});
        controller.parent_id = root.local_id;
        controller = runtime.world().add(controller, false).value();
        const auto item = add_script(controller.id, "integer done; default {state_entry() {" + body + "; done = 1;}}");
        if (expected_error.empty()) {
            wait([&] { return counter(runtime, controller.id, item) == 1; },
                 "Child control script did not complete through the live LSL binding");
        } else {
            const auto prefix = "Native script " + item.str() + ": ";
            wait(
                [&] {
                    std::lock_guard lock(log_mutex);
                    for (const auto &message : logs)
                        if (message.find(prefix) != std::string::npos &&
                            message.find(expected_error) != std::string::npos)
                            return true;
                    return false;
                },
                "Malformed script call did not report its expected runtime error");
            check(counter(runtime, controller.id, item) != 1, "Malformed script call continued past its error");
        }
    };
    tick(21);
    command("llSetKeyframedMotion([], [KFM_COMMAND, KFM_CMD_PAUSE])");
    auto state = runtime.keyframed_motion_state(root.id).value();
    check(!state.running && state.timer_stopped && !state.frames.empty(),
          "Script pause did not preserve the active frame");
    const auto paused_ms = state.current.time_ms;
    tick(21);
    check(runtime.keyframed_motion_state(root.id)->current.time_ms == paused_ms,
          "Paused script motion advanced its frame cursor");
    command("llSetKeyframedMotion([], [KFM_MODE, KFM_LOOP])");
    check(!runtime.keyframed_motion_state(root.id)->running, "Non-command empty-frame options started playback");
    command("llSetKeyframedMotion([], [KFM_COMMAND, KFM_CMD_PLAY])");
    check(runtime.keyframed_motion_state(root.id)->running, "Script play did not resume paused playback");
    tick(22);
    command("llSetKeyframedMotion([], [KFM_COMMAND, KFM_CMD_STOP])");
    state = runtime.keyframed_motion_state(root.id).value();
    check(!state.running && state.frames.empty() && state.keyframes && !state.keyframes->empty(),
          "Script stop did not clear the cursor while retaining the configured keyframes");
    tick(22);
    command("llSetKeyframedMotion([], [KFM_COMMAND, KFM_CMD_PLAY])");
    // The original Stop() keeps m_iterations. Forward playback cannot regenerate
    // its exhausted/cleared list until a new nonempty frame call replaces it.
    tick(22);
    check(!runtime.keyframed_motion_state(root.id)->running, "Stopped forward frames replayed unexpectedly");
    command("llSetKeyframedMotion([<4,0,0>,0.2], [KFM_DATA,KFM_TRANSLATION,KFM_MODE,KFM_FORWARD])");
    tick(23);
    command("llSetKeyframedMotion([], [])");
    state = runtime.keyframed_motion_state(root.id).value();
    check(!state.running && state.timer_stopped && state.frames.empty(),
          "Empty frames and empty options did not stop existing motion");
    tick(23);
    command("llSetKeyframedMotion([], [KFM_COMMAND, KFM_CMD_PLAY])");
    tick(23);
    check(!runtime.keyframed_motion_state(root.id)->running, "Empty-stop forward frames replayed unexpectedly");
    command("llSetKeyframedMotion([<4,0,0>,0.2], [KFM_DATA,KFM_TRANSLATION,KFM_MODE,KFM_FORWARD])");
    tick(24);
    tick(25);
    tick(26);
    tick(27);
    tick(27); // The following timer pulse observes the exhausted frame list.
    check(!runtime.keyframed_motion_state(root.id)->running, "Forward script playback did not complete");
    wait([&] { return counter(runtime, root.id, root_item) == 6607 && counter(runtime, child.id, child_item) == 7; },
         "Root/child scripts did not receive the expected movement lifecycle events");

    command("llSetKeyframedMotion([<1,0,0>,0.2], [KFM_MODE,<1,2,3>])", "LSLInteger expected");
    check(!runtime.keyframed_motion_state(root.id), "Invalid replacement options retained the old component");
    command("llSetKeyframedMotion([], [KFM_COMMAND])");
    check(!runtime.keyframed_motion_state(root.id), "Malformed empty command created an absent component");
    command("llSetKeyframedMotion([\"bad\",0.2], [KFM_DATA,KFM_TRANSLATION])", "Vector3 expected");
    state = runtime.keyframed_motion_state(root.id).value();
    check(!state.running && state.timer_stopped && state.frames.empty() && !state.keyframes,
          "Invalid frame data did not leave a fresh stopped component");
    command("llSetKeyframedMotion([], [KFM_COMMAND])", "Index was outside");
    state = runtime.keyframed_motion_state(root.id).value();
    check(!state.running && state.timer_stopped && state.frames.empty() && !state.keyframes,
          "Malformed empty command changed the existing stopped component");
    runtime.stop();
}
} // namespace

int main() {
    try {
        script_scenario(false);
        script_scenario(true);
        std::cout << "PASS: actual root/child LSL keyframe submission, controls, movement and events\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
