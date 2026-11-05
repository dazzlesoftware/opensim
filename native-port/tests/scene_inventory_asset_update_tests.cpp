// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/scene_inventory_asset_update.hpp"
#include <iostream>
#include <filesystem>
#include "opensim/scene/inventory_asset_update.hpp"
#include "opensim/scene/task_inventory_registry.hpp"
#include "opensim/services/asset_store.hpp"
using namespace opensim;
using namespace scene;
namespace {
framework::Uuid id(int n) {
    auto s = std::to_string(n);
    return framework::Uuid("11223344-5566-7788-99aa-" + std::string(12 - s.size(), '0') + s);
}
std::string hex(const std::optional<std::string> &s) {
    if (!s)
        return "NULL";
    std::string out;
    for (unsigned char c : *s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string bytes(framework::AssetBytes d) {
    return d ? hex(std::string(d->begin(), d->end())) : "NULL";
}
std::string dump(const std::shared_ptr<framework::TaskInventoryItem> &i) {
    return i ? i->item_id.str() + ":" + i->owner_id.str() + ":" + i->asset_id.str() + ":" + hex(i->name) +
                   ":" + hex(i->description) + ":" + std::to_string(i->type) + ":" +
                   std::to_string(i->inventory_type) + ":" + std::to_string(i->current_permissions)
             : "NULL";
}
std::string dump(const std::shared_ptr<framework::AssetBase> &a) {
    return a ? a->full_id().str() + ":" + std::to_string(a->type()) + ":" + bytes(a->data) : "NULL";
}
std::string probe(int agent, int mode, int inv, int type, int failure) {
    auto bit = [&](int n) { return (mode & n) != 0; };
    std::string trace;
    auto step = [&](int n, std::string s) {
        trace += '|' + s;
        if (n == failure)
            throw std::runtime_error("injected");
    };
    auto item = std::make_shared<framework::TaskInventoryItem>();
    item->item_id = id(2);
    item->owner_id = id(9);
    item->asset_id = id(4);
    item->name = bit(2097152) ? std::optional<std::string>{} : bit(4194304) ? "脚本 😃" : "Task item";
    item->description = bit(2097152) ? std::optional<std::string>{} : "Description";
    item->inventory_type = inv;
    item->type = type;
    item->current_permissions = bit(512) ? 0 : 0xffffffffu;
    std::shared_ptr<framework::AssetBase> asset;
    bool deleted = bit(32);
    int inventory_gets = 0;
    auto client = std::make_shared<SceneAssetClient>();
    client->agent = id(1);
    client->agent_alert = [&](const auto &s, bool modal) {
        step(12, "AGENT:" + hex(s) + ":" + std::to_string(modal));
    };
    client->alert = [&](const auto &s) {
        step(11, "ALERT:" + hex(s));
        if (bit(1048576)) {
            item->name = "Alert edit";
            item->type = 13;
            item->asset_id = id(6);
        }
    };
    auto presence = std::make_shared<SceneAssetPresence>();
    presence->client = [&] {
        step(3, "CLIENT");
        return bit(32768) ? std::shared_ptr<SceneAssetClient>{} : client;
    };
    auto part = std::make_shared<SceneAssetPart>();
    part->group_deleted = [&] {
        step(6, "GROUP");
        if (bit(16))
            throw std::runtime_error("null group");
        return deleted;
    };
    auto inventory = [&] {
        step(7, "INVENTORY");
        if (++inventory_gets == 2 && failure == 19)
            throw std::runtime_error("second inventory");
    };
    part->item = [&](auto key) {
        inventory();
        step(8, "ITEM:" + key.str());
        return bit(64) ? std::shared_ptr<framework::TaskInventoryItem>{} : item;
    };
    part->update = [&](auto current) {
        inventory();
        step(15, "UPDATE:" + dump(current));
        if (bit(16384)) {
            asset->full_id(id(11));
            current->asset_id = id(12);
        }
    };
    SceneInventoryAssetBackend b;
    b.presence = [&](auto key) -> std::optional<std::shared_ptr<SceneAssetPresence>> {
        step(1, "PRESENCE:" + key.str());
        if (bit(1))
            return {};
        return bit(2) ? std::shared_ptr<SceneAssetPresence>{} : presence;
    };
    b.inventory_access = [&]() -> std::optional<SceneInventoryAccessUpdate> {
        step(2, "LOOKUP");
        if (bit(4))
            return {};
        return [&](auto c, auto key, auto data) {
            step(4, "MODULE:" + (c ? c->agent.str() : "NULL") + ":" + key.str() + ":" + bytes(data));
            if (bit(131072))
                item->asset_id = id(5);
            return bit(65536) ? framework::Uuid{} : id(9);
        };
    };
    b.part = [&](auto key) {
        step(5, "PART:" + key.str());
        return bit(8) ? std::shared_ptr<SceneAssetPart>{} : part;
    };
    b.edit_inventory = [&](auto object, auto avatar) {
        step(9, "EDIT:" + object.str() + ":" + avatar.str());
        if (bit(262144)) {
            item->inventory_type = 26;
            item->current_permissions = 0;
        }
        return !bit(128);
    };
    b.edit_notecard = [&](auto key, auto object, auto avatar) {
        step(10, "NOTE:" + key.str() + ":" + object.str() + ":" + avatar.str());
        if (bit(524288)) {
            item->name = "Note edit";
            item->type = 255;
            item->inventory_type = 10;
            item->owner_id = id(9);
            item->current_permissions = 0;
            deleted = true;
        }
        return !bit(256);
    };
    b.asset_permissions = [&](auto a, auto key) {
        step(16, "PERM:" + a.str() + ":" + key.str());
        return 0;
    };
    b.create_asset = [&](const auto &name, const auto &description, std::int8_t type, auto data,
                         auto creator) {
        step(13, "CREATE:" + hex(name) + ":" + hex(description) + ":" + std::to_string(type) + ":" +
                     bytes(data) + ":" + creator.str());
        if (bit(131072)) {
            item->name = "Mutation";
            item->type = 3;
            item->asset_id = id(5);
        }
        if (bit(2048)) {
            asset.reset();
            return asset;
        }
        asset = std::make_shared<framework::AssetBase>(id(7), name, type, creator.str());
        asset->data = data ? data : std::make_shared<std::vector<std::uint8_t>>(1);
        return asset;
    };
    b.store = [&](auto a) {
        step(14, "STORE:" + dump(a) + ":" + item->asset_id.str());
        if (bit(8192)) {
            a->full_id(id(10));
            item->asset_id = id(8);
        }
    };
    b.error = [&](const auto &s) { step(17, "ERROR:" + hex(s)); };
    b.console = [&](const auto &s) { step(18, "CONSOLE:" + hex(s)); };
    std::string status = "OK", result = "NONE";
    try {
        result = SceneInventoryAssetController(std::move(b))
                     .update(id(1), id(2), agent ? framework::Uuid{} : id(3),
                             bit(1024) ? framework::AssetBytes{}
                                       : std::make_shared<std::vector<std::uint8_t>>(
                                             std::initializer_list<std::uint8_t>{65, 0, 255}))
                     .str();
    } catch (...) {
        status = "THROW";
    }
    return status + ":" + result + ":" + dump(item) + ":" + dump(asset) + trace;
}
void integration() {
    auto require = [](bool b, const char *s) {
        if (!b)
            throw std::runtime_error(s);
    };
    auto directory = std::filesystem::temp_directory_path() /
                     ("opensim-scene-inventory-asset-" + framework::Uuid::random().str());
    require(std::filesystem::create_directory(directory), "unique integration directory");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            if (path.parent_path() == std::filesystem::temp_directory_path() &&
                path.filename().string().starts_with("opensim-scene-inventory-asset-")) {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        }
    } cleanup{directory};
    auto path = [](const std::filesystem::path &p) {
        auto text = p.u8string();
        return std::string(reinterpret_cast<const char *>(text.data()), text.size());
    };
    framework::Uuid retained;
    std::vector<std::uint8_t> payload{'N', 0, 255, 'Z'};
    {
        services::StoredAssetService assets(
            std::make_shared<services::SqliteAssetStore>(path(directory / "assets.db"), true));
        services::InventoryFolderStore folders(path(directory / "inventory.db"), true);
        services::InventoryService inventory(folders);
        inventory.create_user_inventory(id(1));
        services::InventoryItem agent_item;
        agent_item.id = id(20);
        agent_item.owner = id(1);
        agent_item.creator_id = id(9).str();
        agent_item.name = "Agent note";
        agent_item.description = "Agent description";
        agent_item.parent = inventory.root(id(1))->id;
        agent_item.inventory_type = 263;
        agent_item.asset_type = 7;
        agent_item.current_permissions = 0;
        agent_item.asset_id = id(4);
        require(inventory.add_item(agent_item), "actual wrapped agent inventory type");
        std::vector<std::string> events;
        bool fail_store = false, module = true;
        int removals = 0, script_edits = 0;
        TaskInventoryRegistryHooks hooks;
        hooks.part = [] { return TaskInventoryPart{id(3), id(11), 77}; };
        hooks.changed = [](auto, auto) {};
        hooks.aggregate_permissions = hooks.deep_permissions = hooks.clear_scripted = hooks.schedule_full =
            hooks.group_changed = [] {};
        hooks.remove_script = [&](auto, auto) { ++removals; };
        TaskInventoryRegistry registry(hooks);
        auto note = std::make_shared<framework::TaskInventoryItem>();
        note->item_id = id(2);
        note->name = "Task note";
        note->description = "Task description";
        note->owner_id = id(9);
        note->type = 7;
        note->inventory_type = 263;
        note->current_permissions = 0;
        note->asset_id = id(4);
        require(registry.add(note), "real registry item");
        auto client = std::make_shared<SceneAssetClient>();
        client->agent = id(1);
        client->alert = [&](const auto &text) { events.push_back("ALERT:" + text); };
        client->agent_alert = [&](const auto &text, bool) { events.push_back("AGENT:" + text); };
        auto presence = std::make_shared<SceneAssetPresence>();
        presence->client = [&] { return client; };
        auto part = std::make_shared<SceneAssetPart>();
        part->group_deleted = [] { return false; };
        part->item = [&](auto key) { return registry.find(key); };
        part->update = [&](auto item) {
            events.push_back("TASKUPDATE");
            registry.update(item);
        };
        InventoryAssetUpdateBackend agent;
        agent.item = [&](auto owner, auto key) {
            auto value = inventory.item(owner, key);
            return value ? std::make_shared<services::InventoryItem>(*value) : nullptr;
        };
        agent.edit_notecard = [](auto, auto, auto) { return true; };
        agent.edit_script = [&](auto, auto, auto) {
            ++script_edits;
            return true;
        };
        agent.asset_permissions = [&](auto owner, auto key) {
            return inventory.asset_permissions(owner, key);
        };
        agent.create_asset = create_inventory_asset;
        agent.store = [&](auto asset) {
            events.push_back("STORE");
            return assets.store(asset);
        };
        agent.update = [&](auto value) {
            events.push_back("AGENTUPDATE");
            return inventory.update_item(*value);
        };
        agent.agent_alert = [&](auto, const auto &text, bool modal) { client->agent_alert(text, modal); };
        agent.alert = [&](auto, const auto &text) { client->alert(text); };
        agent.console = [](const auto &) {};
        InventoryAssetUpdateController access(agent);
        SceneInventoryAssetBackend backend;
        backend.presence = [&](auto) -> std::optional<std::shared_ptr<SceneAssetPresence>> {
            return presence;
        };
        backend.inventory_access = [&]() -> std::optional<SceneInventoryAccessUpdate> {
            if (!module)
                return {};
            return [&](auto current, auto key, auto data) {
                return access.caps_update(current->agent, key, data);
            };
        };
        backend.part = [&](auto key) { return key == id(3) ? part : nullptr; };
        backend.edit_inventory = [](auto, auto) { return true; };
        backend.edit_notecard = [](auto, auto, auto) { return true; };
        backend.asset_permissions = agent.asset_permissions;
        backend.create_asset = create_scene_inventory_asset;
        backend.store = [&](auto asset) {
            events.push_back("STORE");
            if (!fail_store)
                assets.store(asset);
        };
        backend.console = [](const auto &) {};
        SceneInventoryAssetController controller(backend);
        auto data = std::make_shared<std::vector<std::uint8_t>>(payload);
        auto serial = registry.serial();
        auto result = controller.update(id(1), id(2), id(3), data);
        require(!result.is_zero() &&
                    events == std::vector<std::string>{"ALERT:Notecard updated", "STORE", "TASKUPDATE"},
                "task success alert precedes actual store and registry update");
        require(registry.find(id(2)) == note && note->asset_id == result && registry.serial() == serial + 1 &&
                    note->inventory_type == 263,
                "actual live task reference and serial update without narrowing stored type");
        auto asset = assets.get_asset(result.str());
        require(asset && *asset->data == payload && asset->creator_id() == id(1).str() &&
                    note->owner_id == id(9),
                "task ownership does not replace editing actor as asset creator");
        events.clear();
        fail_store = true;
        serial = registry.serial();
        auto missing = controller.update(id(1), id(2), id(3), data);
        require(
            !missing.is_zero() && note->asset_id == missing && !assets.get_asset(missing.str()) &&
                registry.serial() == serial + 1 &&
                events == std::vector<std::string>{"ALERT:Notecard updated", "STORE", "TASKUPDATE"},
            "task store failure is ignored once and missing asset reference is retained exactly as source");
        fail_store = false;
        auto script = std::make_shared<framework::TaskInventoryItem>();
        script->item_id = id(30);
        script->name = "Task script";
        script->type = script->inventory_type = 10;
        script->current_permissions = 0;
        script->owner_id = id(9);
        require(registry.add(script), "actual script subset");
        events.clear();
        serial = registry.serial();
        bool threw = false;
        try {
            controller.update(id(1), id(30), id(3), data);
        } catch (const std::bad_optional_access &) {
            threw = true;
        }
        require(threw && events == std::vector<std::string>{"STORE"} && !script->asset_id.is_zero() &&
                    registry.serial() == serial && !assets.get_asset(script->asset_id.str()),
                "source SQLite null-description exception retains live asset mutation before task update");
        script->description = std::string{};
        events.clear();
        result = controller.update(id(1), id(30), id(3), data);
        require(
            !result.is_zero() && events == std::vector<std::string>{"STORE", "TASKUPDATE"} &&
                script_edits == 0 && removals == 0 && registry.scripts().size() == 1 &&
                registry.find(id(30))->asset_id == result,
            "generic task script update stores raw data without script permission check, engine removal or compilation");
        events.clear();
        retained = controller.update(id(1), id(20), {}, data);
        require(!retained.is_zero() &&
                    events == std::vector<std::string>{"STORE", "AGENTUPDATE", "ALERT:Notecard updated"},
                "agent dispatch uses real module and post-store success alert");
        require(inventory.item(id(1), id(20))->asset_id == retained &&
                    inventory.item(id(1), id(20))->inventory_type == 263 &&
                    *assets.get_data(retained.str()) == payload,
                "wrapped agent type preserves source permission branch and raw stored type");
        module = false;
        events.clear();
        require(controller.update(id(1), id(20), {}, data).is_zero() && events.empty(),
                "missing module returns zero without persistence");
    }
    {
        services::StoredAssetService assets(
            std::make_shared<services::SqliteAssetStore>(path(directory / "assets.db")));
        services::InventoryFolderStore folders(path(directory / "inventory.db"));
        services::InventoryService inventory(folders);
        require(inventory.item(id(1), id(20))->asset_id == retained &&
                    *assets.get_data(retained.str()) == payload,
                "scene-dispatched agent reference and raw bytes survive service reopen");
    }
}

}
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int a, m, i, t, f;
            while (std::cin >> a >> m >> i >> t >> f)
                std::cout << probe(a, m, i, t, f) << '\n';
            return 0;
        }
        int n = 0;
        auto check = [&](bool b, const char *s) {
            ++n;
            if (!b)
                throw std::runtime_error(s);
        };
        check(probe(1, 2 | 4, 7, 7, 0).starts_with("OK:00000000"),
              "missing module does not dereference null presence");
        check(probe(0, 2, 10, 10, 0).starts_with("OK:" + id(7).str()),
              "generic task script has no client alert or edit-script gate");
        check(probe(0, 4096, 7, 7, 0).starts_with("OK:" + id(7).str()), "task store result is ignored");
        check(probe(0, 0, 263, 7, 0).find("|NOTE:") != std::string::npos,
              "SDK InventoryType narrows to sbyte");
        auto asset = create_scene_inventory_asset({}, {}, 7, {}, id(1));
        check(!asset->full_id().is_zero() && !asset->name() && !asset->description() && asset->data &&
                  *asset->data == std::vector<std::uint8_t>{0},
              "real nullable task asset factory");
        integration();
        std::cout
            << "PASS: " << n
            << " scene inventory asset checks plus actual registry/SQLite/module/alert-order integration\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
