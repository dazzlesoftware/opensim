// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/framework/task_inventory_item.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace opensim;

int main() {
    try {
        for (int scenario = 0; scenario != 9; ++scenario) {
            services::NullPresenceStore store;
            services::PresenceService presence(store);
            services::GridRegion region;
            region.id = framework::Uuid::random();
            region.name = "Console permissions fixture";
            const auto owner = framework::Uuid::random();
            region.estate_owner = owner;
            const auto asset_id = framework::Uuid::random();
            const std::string source = R"(default{state_entry(){
                llSetObjectDesc("before");
                integer result=osConsoleCommand("show regions");
                llSetObjectDesc("result " + (string)result);
            }})";
            auto asset = std::make_shared<framework::AssetBase>(asset_id, "Console script", 10, owner.str());
            asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
            scene::RegionPolicies policies;
            policies.access = [](auto, auto, auto &) { return true; };
            policies.authorize = [](const auto &, auto &) { return true; };
            policies.generate_map_on_startup = false;
            policies.script_ossl_threat_level = scenario == 0 ? "VeryLow" : "Severe";
            policies.estate_owner_is_god = scenario == 2 || scenario == 7;
            policies.bypass_object_permissions = scenario == 3;
            policies.estate_manager_is_god = scenario == 4;
            policies.estate_manager = [&](auto id) { return id == owner; };
            if (scenario == 5)
                policies.god_rights_level = [&](auto id) { return id == owner ? 200 : 0; };
            if (scenario == 6)
                policies.can_run_console_command = [&](auto id) { return id == owner; };
            if (scenario == 8) {
                policies.script_ossl_threat_level = "None";
                policies.script_ossl_rules["osConsoleCommand"].owners = "true";
            }
            policies.load_asset = [&](auto id) { return id == asset_id ? asset : nullptr; };
            int calls = 0;
            policies.script_run_console_command = [&](const auto &command) {
                if (command != "show regions")
                    throw std::runtime_error("Command changed");
                ++calls;
                if (scenario == 7)
                    throw std::runtime_error("Controlled command failure");
            };
            scene::RegionRuntime runtime(region, presence, policies);
            auto listener = std::make_shared<server::RegistryCapabilityListener>(
                server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")});
            runtime.start(listener);
            auto object = scene::SceneObject::primitive(owner, {128, 128, 30}, {1, 1, 1});
            if (!runtime.world().add(object))
                throw std::runtime_error("Object creation failed");
            auto item = std::make_shared<framework::TaskInventoryItem>();
            item->item_id = framework::Uuid::random();
            item->parent_id = item->parent_part_id = object.id;
            item->asset_id = asset_id;
            item->owner_id = item->creator_id = owner;
            item->type = item->inventory_type = 10;
            item->script_running = true;
            item->name = "Console test";
            item->base_permissions = item->current_permissions = 0x8e000u;
            runtime.restore_task_inventory(object.id, {item});
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            std::string description;
            do {
                runtime.poll();
                description = runtime.world().object(object.id)->description;
                if (description.starts_with("result "))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } while (std::chrono::steady_clock::now() < deadline);
            const bool executes = scenario >= 2 && scenario <= 7;
            const auto expected = scenario == 0 || scenario == 7 ? "before" : executes ? "result 1" : "result 0";
            if (description != expected || calls != int(executes))
                throw std::runtime_error("Console scenario " + std::to_string(scenario) + ": " + description +
                                         ", calls=" + std::to_string(calls));
            runtime.stop();
        }
        std::cout << "PASS: nine production console permission/execution scenarios\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
