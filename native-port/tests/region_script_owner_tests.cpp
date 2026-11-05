// Real region owner-thread dispatch and shutdown cancellation, without a viewer.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/inventory_new.hpp"
#include "opensim/services/grid_service.hpp"
#include <future>
#include <iostream>
#include <thread>
using namespace opensim;
using namespace scene;
using namespace services;
int main() {
    try {
        NullPresenceStore store;
        PresenceService presence(store);
        RegionPolicies policies;
        policies.access = [](auto, const auto&, auto&) { return true; };
        policies.authorize = [](const auto&, auto&) { return true; };
        policies.script_delay_factor = .25f;
        GridRegion region;
        region.id = framework::Uuid::random();
        region.name = "Owner dispatch";
        region.external_host = "127.0.0.1";
        region.internal_endpoint = RegionEndpoint{"127.0.0.1", 0};
        region.x = 256000;
        region.y = 256000;
        server::IniConfig grid_config;
        NullRegionStore grid_store;
        GridService grid(grid_store, grid_config, [] { return 1700000000; });
        const std::string linked_id = "10c9db03-0ee7-4e0c-9ad8-401ef9516758";
        const std::function<network::ServiceXmlValue(const OrderedRegionFields&)> grid_request =
            [&](const OrderedRegionFields& request) {
            using Xml = network::ServiceXmlValue;
            const RegionFields fields(request.begin(), request.end());
            if (fields.at("METHOD") == "get_region_flags") {
                if (fields.at("REGIONID") != linked_id)
                    throw std::runtime_error("Unexpected flags lookup");
                return Xml::object({{"result", std::to_string(RegionFlags::Hyperlink)}});
            }
            if (fields.at("METHOD") != "get_region_by_name")
                throw std::runtime_error("Unexpected grid method");
            if (fields.at("NAME") != "Linked Region")
                return Xml::object({{"result", "null"}});
            const auto handle = (std::uint64_t{300000} << 32) | 400000u;
            return Xml::object({{"result", Xml::object({
                {"uuid", linked_id}, {"regionName", "Linked Region"}, {"locX", "261376"},
                {"locY", "262400"}, {"access", "21"}, {"regionSecret", std::to_string(handle)}})}});
        };
        policies.script_grid_region = make_script_grid_lookup(grid, region.scope, grid_request);
        const auto make_asset = [](std::int8_t type, std::string_view text) {
            auto asset = std::make_shared<framework::AssetBase>(framework::Uuid::random(), "fixture", type,
                                                                std::nullopt);
            asset->data = std::make_shared<std::vector<std::uint8_t>>(text.begin(), text.end());
            return asset;
        };
        const auto landmark_handle = (std::uint64_t{256256} << 32) | 255488u;
        const auto landmark = make_asset(3, generate_inventory_landmark(
            region.id, {12.5f, 9.25f, 42.f}, landmark_handle).text.value());
        const auto program = make_asset(10, R"LSL(
            key found;
            key missing;
            key unknown;
            key online;
            key name;
            key born;
            key rating;
            key level;
            key pay;
            key here;
            key linked;
            key maturity;
            key unavailable;
            key release;
            integer replies;
            integer unexpected;
            default {
                state_entry() {
                    found = llRequestInventoryData("My Landmark");
                    missing = llRequestInventoryData("my landmark");
                    key agent = "ed944b60-1d13-41fd-91cf-75ec32e06e87";
                    unknown = llRequestAgentData("2d90215b-8efc-4151-b904-7f40dff93d52", DATA_NAME);
                    if (llRequestAgentData(agent, 5) != "" || llRequestAgentData(agent, 9) != "")
                        unexpected = 1;
                    online = llRequestAgentData(agent, DATA_ONLINE);
                    name = llRequestAgentData(agent, DATA_NAME);
                    born = llRequestAgentData(agent, DATA_BORN);
                    rating = llRequestAgentData(agent, DATA_RATING);
                    level = llRequestAgentData(agent, 7);
                    pay = llRequestAgentData(agent, DATA_PAYINFO);
                    here = llRequestSimulatorData("Owner dispatch", DATA_SIM_POS);
                    linked = llRequestSimulatorData("Linked Region", DATA_SIM_POS);
                    maturity = llRequestSimulatorData("Linked Region", DATA_SIM_RATING);
                    unavailable = llRequestSimulatorData("Missing Region", DATA_SIM_STATUS);
                    release = llRequestSimulatorData("Owner dispatch", DATA_SIM_RELEASE);
                    if (llRequestSimulatorData("Owner dispatch", DATA_ONLINE) != NULL_KEY)
                        unexpected = 1;
                }
                dataserver(key id, string data) {
                    if (id == found && data == "<268.500000, -502.750000, 42.000000>")
                        llSetObjectName("Landmark resolved");
                    if (id == missing && data == "")
                        llSetObjectDesc("Missing landmark replied");
                    if (id == unknown) unexpected = 1;
                    if (id == online && data == "0") replies = replies | 1;
                    if (id == name && data == "Offline Resident") replies = replies | 2;
                    if (id == born && data == "1970-01-02") replies = replies | 4;
                    if (id == rating && data == "0,0,0,0,0,0") replies = replies | 8;
                    if (id == level && data == "42") replies = replies | 16;
                    if (id == pay && data == "3") replies = replies | 32;
                    if (id == here && data == "<1000.000000, 1000.000000, 0.000000>") replies = replies | 64;
                    if (id == linked && data == "<300000.000000, 400000.000000, 0.000000>") replies = replies | 128;
                    if (id == maturity && data == "MATURE") replies = replies | 256;
                    if (id == unavailable && data == "unknown") replies = replies | 512;
                    if (id == release && data == "OpenSim") replies = replies | 1024;
                    if (replies == 2047 && !unexpected)
                        llSetText("Agent and simulator data resolved", <1,1,1>, 1);
                }
            }
        )LSL");
        policies.load_asset = [program, landmark](framework::Uuid id) -> std::shared_ptr<framework::AssetBase> {
            if (id == program->full_id())
                return program;
            if (id == landmark->full_id())
                return landmark;
            return {};
        };
        policies.script_account = [](framework::Uuid id) -> std::optional<UserAccount> {
            if (id != framework::Uuid("ed944b60-1d13-41fd-91cf-75ec32e06e87"))
                return {};
            UserAccount account(id);
            account.first_name = "Offline";
            account.last_name = "Resident";
            account.user_level = 42;
            account.user_flags = 12;
            account.created = 86400;
            return account;
        };
        RegionRuntime runtime(region, presence, std::move(policies));
        runtime.start(std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 19009, 0, {}}));
        const auto owner = framework::Uuid::random();
        const auto prim = runtime.world().add(SceneObject::primitive(owner, {128.f, 128.f, 25.f}));
        if (!prim)
            throw std::runtime_error("Could not add the landmark test prim");
        const auto make_item = [&](const auto& asset, std::string name) {
            auto entry = std::make_shared<framework::TaskInventoryItem>();
            entry->item_id = framework::Uuid::random();
            entry->asset_id = asset->full_id();
            entry->name = std::move(name);
            entry->type = entry->inventory_type = asset->type();
            entry->owner_id = entry->creator_id = owner;
            entry->parent_id = entry->parent_part_id = prim->id;
            return entry;
        };
        runtime.restore_task_inventory(prim->id, {make_item(program, "Script"), make_item(landmark, "My Landmark")});
        const auto landmark_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        for (;;) {
            runtime.poll();
            const auto state = runtime.world().object(prim->id);
            if (state && state->name == "Landmark resolved" && state->description == "Missing landmark replied" &&
                state->floating_text == "Agent and simulator data resolved")
                break;
            if (std::chrono::steady_clock::now() >= landmark_deadline)
                throw std::runtime_error("Landmark, agent and simulator data replies did not reach the restored script");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        auto agent = framework::Uuid::random(), item = framework::Uuid::random(),
             object = framework::Uuid::random();
        std::vector<std::uint8_t> bytes{'d', 'e', 'f', 'a', 'u', 'l', 't'};
        auto operation = std::async(
            std::launch::async, [&] { return runtime.update_task_script(agent, item, object, true, bytes); });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (operation.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
               std::chrono::steady_clock::now() < deadline) {
            runtime.poll();
            std::this_thread::yield();
        }
        if (operation.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            throw std::runtime_error("Owner did not complete queued update");
        if (!operation.get().empty())
            throw std::runtime_error("Missing source avatar did not return empty errors");
        std::vector<std::future<bool>> pending;
        for (int i = 0; i < 16; ++i)
            pending.push_back(std::async(std::launch::async, [&] {
                try {
                    runtime.update_task_script(agent, item, object, true, bytes);
                    return false;
                } catch (const std::exception&) {
                    return true;
                }
            }));
        runtime.quiesce_script_updates();
        for (auto& task : pending)
            if (task.wait_for(std::chrono::seconds(3)) != std::future_status::ready || !task.get())
                throw std::runtime_error("Quiesce stranded or accepted a pending script update");
        bool rejected = false;
        try {
            runtime.update_task_script(agent, item, object, true, bytes);
        } catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Quiesced owner accepted an update");
        runtime.stop();
        std::cout
            << "PASS: real owner-thread dispatch,16 concurrent cancellations and post-quiesce rejection\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
