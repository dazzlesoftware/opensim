// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/hg_inventory_broker.hpp"
#include "opensim/services/remote_inventory.hpp"
#include <iostream>
#include <stdexcept>

using namespace opensim;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        using framework::Uuid;
        using network::ServiceXmlValue;
        const auto visitor = Uuid::random(), resident = Uuid::random(), folder_id = Uuid::random();
        services::InventoryFolderStore store(":memory:", true);
        std::vector<std::pair<std::string, std::string>> calls;
        std::vector<std::string> creations;
        int lookups = 0;
        std::string visitor_url = "/http://foreign.invalid///";
        bool offline = false;
        auto connector = [&](const std::string &url) -> std::shared_ptr<services::InventoryService> {
            creations.push_back(url);
            server::IniConfig config;
            config.set("InventoryService", "InventoryServerURI", url);
            return services::remote_inventory_service(store, config, [&](const auto &uri, const auto &body) {
                const auto form = network::parse_service_form(body);
                const auto method = std::get<std::string>(form.at("METHOD"));
                calls.emplace_back(uri, method);
                if (offline)
                    throw std::runtime_error("Controlled home inventory outage");
                if (method == "GETINVENTORYSKELETON")
                    return network::service_xml(ServiceXmlValue::object({{"FOLDERS", ServiceXmlValue::object({})}}));
                if (method == "GETFOLDERITEMS")
                    return network::service_xml(ServiceXmlValue::object({{"ITEMS", ServiceXmlValue::object({})}}));
                if (method == "GETROOTFOLDER" || method == "GETFOLDERFORTYPE" || method == "GETFOLDER")
                    return network::service_xml(
                        ServiceXmlValue::object({{"folder", ServiceXmlValue::object({{"ID", folder_id.str()},
                                                                                     {"Owner", visitor.str()},
                                                                                     {"Name", "Home root"},
                                                                                     {"Type", "8"},
                                                                                     {"Version", "1"},
                                                                                     {"ParentID", Uuid{}.str()}})}}));
                return network::service_xml(ServiceXmlValue::object({{"RESULT", "True"}}));
            });
        };
        // Both inner services are real native form connectors, letting the test
        // distinguish routing from inherited local SQLite behavior.
        auto local_shared = connector("http://local.invalid");

        class Local final : public services::InventoryService {
          public:
            Local(services::InventoryFolderStore &store, std::shared_ptr<services::InventoryService> target)
                : InventoryService(store), target(std::move(target)) {}

            bool create_user_inventory(const Uuid &id) override {
                return target->create_user_inventory(id);
            }

            std::optional<services::InventoryFolder> root(const Uuid &id) override {
                return target->root(id);
            }

            std::optional<std::vector<services::InventoryFolder>> skeleton(const Uuid &id) override {
                return target->skeleton(id);
            }

            services::InventoryCollections multiple_content(const Uuid &id, const std::vector<Uuid> &ids) override {
                return target->multiple_content(id, ids);
            }

            std::shared_ptr<services::InventoryService> target;
        };

        services::HGInventoryDependencies dependencies;
        dependencies.inventory_url = [&](Uuid user) -> std::optional<std::string> {
            ++lookups;
            return user == visitor ? std::optional(visitor_url) : std::nullopt;
        };
        dependencies.connector = connector;
        services::HGInventoryBroker broker(store, std::make_unique<Local>(store, local_shared), dependencies);
        require(broker.create_user_inventory(visitor) && lookups == 0 &&
                    calls.back().first == "http://local.invalid/xinventory",
                "Inventory creation left local grid");
        require(broker.root(visitor)->id == folder_id && creations.back() == "http://foreign.invalid",
                "Foreign URL trim/root routing failed");
        auto before = calls.size();
        require(broker.root(visitor)->id == folder_id && calls.size() == before && lookups == 1,
                "Foreign root/URL caches were bypassed");
        broker.for_type(visitor, 6);
        before = calls.size();
        broker.for_type(visitor, 6);
        require(calls.size() == before, "System folder cache was bypassed");
        broker.root(resident);
        require(calls.back().first == "http://local.invalid/xinventory", "Resident routed to foreign service");
        broker.multiple_content(resident, {folder_id, folder_id});
        require(calls.back().second == "GETMULTIPLEFOLDERSCONTENT", "Local folder batch split unexpectedly");
        before = calls.size();
        broker.multiple_content(visitor, {folder_id, folder_id});
        require(calls.size() == before + 2 && calls.back().second == "GETFOLDERCONTENT",
                "Foreign folders did not use original per-folder requests");
        services::InventoryFolder folder{"child", -1, 1, folder_id, visitor, Uuid{}};
        services::InventoryItem item;
        item.owner = visitor;
        item.id = Uuid::random();
        item.parent = folder_id;
        const auto expect_foreign = [&](const char *method) {
            require(calls.back() == std::pair(std::string("http://foreign.invalid/xinventory"), std::string(method)),
                    "Inventory operation used wrong owner/service or method");
        };
        broker.skeleton(visitor);
        expect_foreign("GETINVENTORYSKELETON");
        broker.folder(visitor, folder_id);
        expect_foreign("GETFOLDER");
        broker.item(visitor, item.id);
        expect_foreign("GETITEM");
        broker.multiple_items(visitor, {Uuid::random()});
        expect_foreign("GETMULTIPLEITEMS");
        broker.folder_items(visitor, folder_id);
        expect_foreign("GETFOLDERITEMS");
        broker.add_folder(folder);
        expect_foreign("ADDFOLDER");
        broker.update_folder(folder);
        expect_foreign("UPDATEFOLDER");
        broker.move_folder(folder);
        expect_foreign("MOVEFOLDER");
        broker.add_item(item);
        expect_foreign("ADDITEM");
        broker.update_item(item);
        expect_foreign("UPDATEITEM");
        broker.move_items(visitor, {item});
        expect_foreign("MOVEITEMS");
        broker.delete_items(visitor, {item.id});
        expect_foreign("DELETEITEMS");
        broker.delete_folders(visitor, {folder_id});
        expect_foreign("DELETEFOLDERS");
        services::InventoryService &base = broker;
        base.purge_folder(folder);
        expect_foreign("PURGEFOLDER");
        broker.asset_permissions(visitor, item.asset_id);
        expect_foreign("GETASSETPERMISSIONS");
        before = calls.size();
        require(broker.active_gestures(visitor) == std::optional(std::vector<services::InventoryItem>{}) &&
                    broker.move_items(visitor, {}) && broker.delete_items(visitor, {}) &&
                    !broker.delete_folders(visitor, {}) && broker.multiple_items(visitor, {})->empty() &&
                    calls.size() == before,
                "Broker special/empty operations made unexpected requests");
        visitor_url = "http://other.invalid";
        broker.client_closed(visitor, true);
        require(broker.inventory_url(visitor) == "http://foreign.invalid", "Live root lost cached home URL");
        broker.client_closed(visitor, false);
        require(broker.inventory_url(visitor) == visitor_url, "Final close did not clear home URL");
        broker.root(visitor);
        require(calls.back().first == "http://other.invalid/xinventory", "Final close retained old root cache");
        offline = true;
        before = calls.size();
        bool outage_threw = false;
        try {
            broker.add_item(item);
        } catch (const std::exception &) {
            outage_threw = true;
        }
        require(outage_threw && calls.size() == before + 1 && calls.back().first == "http://other.invalid/xinventory",
                "Home outage fell back to resident inventory");
        for (const auto &owner : {visitor, resident})
            require(store.folders({{"agentID", owner.str()}}).empty() &&
                        store.items({{"avatarID", owner.str()}}).empty(),
                    "Broker mutated local inventory tables");
        broker.client_closed(visitor, false);
        std::cout << "PASS: Hypergrid inventory owner routing, local exceptions, caches, close and outage isolation\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
