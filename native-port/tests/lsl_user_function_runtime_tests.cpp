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

void exercise() {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "Suspended function restoration";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto owner = Uuid::random();
    auto object = SceneObject::primitive(owner, {20, 20, 30});
    const std::string source = "integer calls; integer outer(integer value) {return inner(value)+1;} "
                               "integer inner(integer value) {calls += 1; llSleep(0.2); return value*2;} "
                               "default {state_entry() {integer result=outer(5); "
                               "llSetObjectDesc((string)result+\":\"+(string)calls);}}";
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Nested function restoration", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = item->parent_part_id = object.id;
    item->asset_id = asset->full_id();
    item->owner_id = item->creator_id = owner;
    item->type = item->inventory_type = 10;
    item->script_running = true;
    const auto make_runtime = [&] {
        RegionPolicies policies;
        policies.access = [](Uuid, const auto &, auto &) { return true; };
        policies.authorize = [](const auto &, auto &) { return true; };
        policies.load_asset = [asset](Uuid id) { return id == asset->full_id() ? asset : nullptr; };
        policies.schedule_asset_fetch = [](auto action) { action(); };
        auto runtime = std::make_unique<RegionRuntime>(region, presence, std::move(policies));
        runtime->start(std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9017, 0, std::string("127.0.0.1")}));
        return runtime;
    };
    auto original = make_runtime();
    object = original->world().add(object, false).value();
    original->restore_task_inventory(object.id, {item});
    RegionRuntime::TaskScriptStates saved;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    do {
        original->poll();
        const auto current = original->snapshot_task_script_states(object.id);
        if (const auto found = current.find(item->item_id); found != current.end()) {
            const auto state = script::decode_lsl_checkpoint(found->second);
            if (state && state->sleep_remaining > 0 && state->globals.size() == 1 &&
                std::get<std::int32_t>(state->globals[0]) == 1) {
                saved = current;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    check(!saved.empty(), "Region script did not suspend inside its nested user function");
    object = original->world().object(object.id).value();
    check(object.description.empty(), "Calling event continued before the nested function returned");
    original->stop();
    original.reset();
    auto restored = make_runtime();
    check(restored->world().add(object, false).has_value(), "Could not restore the script object");
    restored->restore_task_inventory(object.id, {item}, saved, RegionRuntime::TaskInventoryRestoreSource::RegionStart);
    const auto resume_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    do {
        restored->poll();
        if (!restored->world().object(object.id)->description.empty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < resume_deadline);
    check(restored->world().object(object.id)->description == "11:1",
          "Region restoration lost nested return state or replayed function side effects");
    restored->stop();
}

void configured_stack(std::int32_t limit, bool completes) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "Configured script stack";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    const auto owner = Uuid::random();
    auto object = SceneObject::primitive(owner, {20, 20, 30});
    const std::string source = "integer calls; descend(integer left){calls++;if(left>0)descend(left-1);} "
                               "default{state_entry(){descend(200);llSetObjectDesc((string)calls);}}";
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Configured stack script", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    std::mutex log_mutex;
    bool stack_fault = false;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.script_stack_limit_bytes = limit;
    policies.load_asset = [asset](Uuid id) { return id == asset->full_id() ? asset : nullptr; };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    policies.log = [&](const std::string &message) {
        std::lock_guard lock(log_mutex);
        if (message.find("Native script") != std::string::npos &&
            (message.find("stack") != std::string::npos || message.find("Stack") != std::string::npos))
            stack_fault = true;
    };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9017, 0, std::string("127.0.0.1")}));
    object = runtime.world().add(object, false).value();
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = item->parent_part_id = object.id;
    item->asset_id = asset->full_id();
    item->owner_id = item->creator_id = owner;
    item->type = item->inventory_type = 10;
    item->script_running = true;
    runtime.restore_task_inventory(object.id, {item});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    bool fault = false;
    do {
        runtime.poll();
        {
            std::lock_guard lock(log_mutex);
            fault = stack_fault;
        }
        if (fault || !runtime.world().object(object.id)->description.empty())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    check(completes ? !fault && runtime.world().object(object.id)->description == "201"
                    : fault && runtime.world().object(object.id)->description.empty(),
          "Region stack policy did not reach the script host or respect its minimum allocation");
    runtime.stop();
}
} // namespace

int main() {
    try {
        exercise();
        configured_stack(0, false);
        configured_stack(16384, false);
        configured_stack(65536, true);
        std::cout << "PASS: region script nested user-function sleep, checkpoint and clean-runtime restoration\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
