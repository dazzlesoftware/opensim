// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/viewer_physics.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace scene;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
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

enum class Case { RunningEnable, PausedEnable, Disable, SizeRejected, CapacityRejected, FlagSizeRejected, FlagEnable };

void scenario(Case which) {
    const auto name = std::to_string(static_cast<int>(which));
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "KFM physics status " + name;
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto owner = Uuid::random();
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = owner;
    auto assets = std::make_shared<std::map<Uuid, std::shared_ptr<AssetBase>>>();
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
    policies.physical_prim_max = 10;
    policies.physical_linkset_capacity = which == Case::CapacityRejected ? 1 : 0;
    // Deliberately leave physics_library empty. Persisted PrimFlags.Physics is
    // insufficient to satisfy the original effective RootPart.PhysActor check.
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9018, 0, std::string("127.0.0.1")}));
    const bool oversized = which == Case::SizeRejected || which == Case::FlagSizeRejected;
    const bool flagged = which == Case::FlagSizeRejected || which == Case::FlagEnable;
    auto object =
        SceneObject::primitive(owner, {20, 20, 30}, oversized ? Vector3{11, .5f, .5f} : Vector3{.5f, .5f, .5f});
    if (flagged)
        object.flags |= 1;
    const auto root = runtime.world().add(object, false).value();
    auto controller = SceneObject::primitive(owner, {0, 0, 1});
    controller.parent_id = root.local_id;
    controller = runtime.world().add(controller, false).value();
    const auto add_script = [&](Uuid part, const std::string &source) {
        auto asset = std::make_shared<AssetBase>(Uuid::random(), "Physics motion lifecycle", 10, "");
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
        throw std::runtime_error("Physics status case " + name + ": " + message);
    };
    const auto root_item =
        add_script(root.id, "integer count; default {state_entry() {"
                            "llSetKeyframedMotion([<4,0,0>,0.2], [KFM_DATA,KFM_TRANSLATION,KFM_MODE,KFM_FORWARD]);"
                            "} moving_start() {count += 100;} moving_end() {count += 1000;}"
                            " changed(integer flags) {if(flags & CHANGED_POSITION) count += 1;}}");
    wait([&] { return counter(runtime, root.id, root_item) == 100; }, "Script did not start motion");
    now += 50;
    wait([&] { return counter(runtime, root.id, root_item) == 101; }, "Initial motion did not move");
    check(std::abs(runtime.world().object(root.id)->position.x - 21) < .0001f,
          "Physics status case " + name + ": incorrect initial motion position");
    if (which == Case::PausedEnable) {
        const auto pause_item = add_script(
            controller.id,
            "integer done; default {state_entry() {llSetKeyframedMotion([], [KFM_COMMAND,KFM_CMD_PAUSE]); done=1;}}");
        wait(
            [&] {
                return counter(runtime, controller.id, pause_item) == 1 && counter(runtime, root.id, root_item) == 1101;
            },
            "Pause did not post one moving_end");
        check(!runtime.keyframed_motion_state(root.id)->running, "Paused fixture still runs");
        // Use a separate part for the next command rather than reloading the
        // pause script, which would introduce an unrelated script reset.
        auto status_part = SceneObject::primitive(owner, {0, 0, 2});
        status_part.parent_id = root.local_id;
        controller = runtime.world().add(status_part, false).value();
    }
    const auto before = runtime.keyframed_motion_state(root.id).value();
    const auto flags_before = runtime.world().object(root.id)->flags;
    const bool enable = which != Case::Disable;
    const auto status_item =
        add_script(controller.id, std::string("integer done; default {state_entry() {llSetStatus(STATUS_PHYSICS,") +
                                      (enable ? "TRUE" : "FALSE") + "); done=1;}}");
    wait([&] { return counter(runtime, controller.id, status_item) == 1; }, "Status script did not complete");
    const bool rejected =
        which == Case::SizeRejected || which == Case::CapacityRejected || which == Case::FlagSizeRejected;
    if (rejected || !enable) {
        const auto after = runtime.keyframed_motion_state(root.id);
        check(after && after->running && after->current.time_ms == before.current.time_ms,
              "Physics status case " + name + ": rejected enable or disable changed the live motion");
        check(runtime.world().object(root.id)->flags == flags_before,
              "Physics status case " + name + ": rejected enable or disable changed object flags");
        now += 50;
        wait([&] { return counter(runtime, root.id, root_item) == 102; }, "Preserved motion did not continue");
        check(std::abs(runtime.world().object(root.id)->position.x - 22) < .0001f,
              "Physics status case " + name + ": preserved motion advanced incorrectly");
    } else {
        check(!runtime.keyframed_motion_state(root.id),
              "Physics status case " + name + ": accepted enable retained motion without a physics engine");
        wait([&] { return counter(runtime, root.id, root_item) == 1101; },
             "Accepted enable did not deliver the expected single moving_end");
        now += 50;
        runtime.poll();
        check(!runtime.keyframed_motion_state(root.id) && counter(runtime, root.id, root_item) == 1101 &&
                  std::abs(runtime.world().object(root.id)->position.x - 21) < .0001f,
              "Physics status case " + name + ": removed motion moved or posted a duplicate moving_end");
    }
    runtime.stop();
}

void library_scenario(const std::filesystem::path &library, std::shared_ptr<const DefaultAnimations> animations,
                      bool already_physical) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = already_physical ? "Existing physical actor" : "Generic flags physical actor";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto owner = Uuid::random();
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = owner;
    auto assets = std::make_shared<std::map<Uuid, std::shared_ptr<AssetBase>>>();
    double now = 1000;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.physics_library = library;
    policies.animations = std::move(animations);
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
        server::CapabilityListenerInfo{false, 9019, 0, std::string("127.0.0.1")}));
    auto object = SceneObject::primitive(owner, {50, 50, 30});
    object.physics.gravity = 0;
    if (already_physical)
        object.flags |= 1;
    const auto root = runtime.world().add(object, false).value();
    auto child = SceneObject::primitive(owner, {0, 0, 1});
    child.parent_id = root.local_id;
    child = runtime.world().add(child, false).value();
    const auto add_script = [&](Uuid part, const std::string &source) {
        auto asset = std::make_shared<AssetBase>(Uuid::random(), "Actor status lifecycle", 10, "");
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
        throw std::runtime_error(region.name.value_or("KFM status") + ": " + message);
    };
    const auto root_item = add_script(root.id, "integer count; default {state_entry() {} moving_start() {count+=100;}"
                                               " moving_end() {count+=1000;}}");
    wait([&] { return counter(runtime, root.id, root_item) == 0; }, "Observer did not load");
    runtime.poll(); // Synchronize the real static/dynamic actor before installing the fixture component.
    KeyframedMotionFrame frame;
    frame.position = Vector3{4, 0, 0};
    frame.time_ms = 200;
    // A restored/native component may coexist with a physical actor even though
    // fresh llSetKeyframedMotion calls correctly refuse this eligibility state.
    check(runtime.install_keyframed_motion(root, script::KeyframedMotionMode::Forward, {frame}),
          "Could not install motion for the physical actor lifecycle fixture");
    wait([&] { return counter(runtime, root.id, root_item) == 100; }, "Motion did not post moving_start");
    if (already_physical) {
        const auto item =
            add_script(child.id, "integer done; default {state_entry() {llSetStatus(STATUS_PHYSICS,TRUE); done=1;}}");
        wait([&] { return counter(runtime, child.id, item) == 1; }, "Physical enable script did not complete");
        check(runtime.keyframed_motion_state(root.id) && runtime.keyframed_motion_state(root.id)->running,
              "Already physical effective actor did not skip script-enable cleanup");
        check(counter(runtime, root.id, root_item) == 100, "Already physical enable posted an unexpected moving_end");
    } else {
        runtime.world().set_group_flags(root.id, true, false, false);
        check(runtime.keyframed_motion_state(root.id)->running,
              "Generic persisted flags cleared motion before actor synchronization");
        wait([&] { return !runtime.keyframed_motion_state(root.id) && counter(runtime, root.id, root_item) == 1100; },
             "Generic flags did not stop motion when the real actor became physical");
        runtime.poll();
        check(counter(runtime, root.id, root_item) == 1100,
              "Repeated physical actor synchronization posted a duplicate moving_end");
    }
    runtime.stop();
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Physics library and animation catalog paths required");
        for (const auto which : {Case::RunningEnable, Case::PausedEnable, Case::Disable, Case::SizeRejected,
                                 Case::CapacityRejected, Case::FlagSizeRejected, Case::FlagEnable})
            scenario(which);
        auto animations = std::make_shared<DefaultAnimations>(argv[2]);
        library_scenario(argv[1], animations, false);
        library_scenario(argv[1], animations, true);
        std::cout << "PASS: actual scripted physics status motion cleanup, events, limits and actor-free flags\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
