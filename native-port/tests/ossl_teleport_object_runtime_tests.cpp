// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
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

void scenario(bool allowed, bool selected) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "Script object teleport";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto owner = Uuid::random();
    auto assets = std::make_shared<std::map<Uuid, std::shared_ptr<AssetBase>>>();
    std::mutex log_mutex;
    std::vector<std::string> logs;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.script_ossl_threat_level = allowed ? "Severe" : "VeryLow";
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
    const auto host = runtime.world().add(SceneObject::primitive(owner, {20, 20, 30}), false).value();
    auto script_part = SceneObject::primitive(owner, {1, 2, 3});
    script_part.parent_id = host.local_id;
    script_part = runtime.world().add(script_part, false).value();
    const auto target = runtime.world().add(SceneObject::primitive(owner, {40, 40, 30}), false).value();
    auto target_child = SceneObject::primitive(owner, {2, 0, 0});
    target_child.parent_id = target.local_id;
    target_child = runtime.world().add(target_child, false).value();
    if (selected)
        runtime.world().set_selected(target.id, true);
    const auto call = [](Uuid id) {
        return "osTeleportObject((key)\"" + id.str() +
               "\", <80,81,40>, <0,0,0,1>, "
               "OSTPOBJ_STOPATTARGET | OSTPOBJ_SETROT)";
    };
    const auto source = "integer childResult = 99; integer missingResult = 99; integer rootResult = 99; "
                        "default { state_entry() { childResult = " +
                        call(target_child.id) + "; missingResult = " + call(Uuid{}) +
                        "; rootResult = " + call(target.id) + "; } }";
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Teleport script", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    assets->emplace(asset->full_id(), asset);
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = item->parent_part_id = script_part.id;
    item->asset_id = asset->full_id();
    item->owner_id = item->creator_id = owner;
    item->type = item->inventory_type = 10;
    item->script_running = true;
    runtime.restore_task_inventory(script_part.id, {item});
    bool observed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    do {
        runtime.poll();
        if (allowed) {
            const auto snapshots = runtime.snapshot_task_script_states(script_part.id);
            if (const auto found = snapshots.find(item->item_id); found != snapshots.end()) {
                const auto state = script::decode_lsl_checkpoint(found->second);
                if (state && state->globals.size() == 3 && std::get<std::int32_t>(state->globals[2]) != 99) {
                    check(std::get<std::int32_t>(state->globals[0]) == -1,
                          "Script teleport accepted a child target UUID");
                    check(std::get<std::int32_t>(state->globals[1]) == -1, "Script teleport accepted a missing target");
                    check(std::get<std::int32_t>(state->globals[2]) == (selected ? -1 : 1),
                          "Script teleport returned incorrect root-target status");
                    observed = true;
                }
            }
        } else {
            std::lock_guard lock(log_mutex);
            for (const auto &message : logs)
                if (message.find("osTeleportObject") != std::string::npos &&
                    message.find("permission") != std::string::npos)
                    observed = true;
        }
        if (observed)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    check(observed, "Compiled child-host teleport script did not produce its expected result");
    check(runtime.world().object(target.id)->position == (allowed && !selected ? Vector3{80, 81, 40} : target.position),
          "Script teleport moved the wrong target or ignored permission/selection");
    check(runtime.world().object(target_child.id)->position == target_child.position,
          "Script teleport changed a child local offset");
    check(runtime.world().object(host.id)->position == host.position, "Script teleport moved the calling host");
    runtime.stop();
}
} // namespace

int main() {
    try {
        scenario(true, false);
        scenario(true, true);
        scenario(false, false);
        std::cout << "PASS: compiled osTeleportObject runtime binding, root lookup, selection and Severe permission\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
