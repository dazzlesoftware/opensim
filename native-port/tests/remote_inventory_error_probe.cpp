// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/remote_inventory.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>

using namespace opensim;

int main() {
    try {
        services::InventoryFolderStore store(":memory:", true);
        server::IniConfig config;
        config.set("InventoryService", "InventoryServerURI", "http://inventory.invalid");
        bool failure = false;
        int requests = 0;
        auto remote = services::remote_inventory_service(store, config, [&](const auto &, const auto &) {
            ++requests;
            if (failure)
                throw std::runtime_error("Controlled request failure");
            return network::service_xml(network::ServiceXmlValue::object({{"RESULT", "False"}}));
        });
        services::InventoryFolder folder{"folder", -1, 1, {}, {}, {}};
        services::InventoryItem item;
        item.name = "item";
        item.creator_id = framework::Uuid{}.str();
        std::string line;
        while (std::getline(std::cin, line)) {
            const auto frame = network::parse_osd_json(line);
            const auto method = frame.get("method").text;
            failure = network::osd_boolean(frame.get("failure"));
            requests = 0;
            network::Value result;
            try {
                network::Value value;
                if (method == "CreateUserInventory")
                    value = network::Value(remote->create_user_inventory({}));
                else if (method == "GetRootFolder")
                    remote->root({});
                else if (method == "GetFolderForType")
                    remote->for_type({}, 6);
                else if (method == "AddFolder")
                    value = network::Value(remote->add_folder(folder));
                else if (method == "UpdateFolder")
                    value = network::Value(remote->update_folder(folder));
                else if (method == "MoveFolder")
                    value = network::Value(remote->move_folder(folder));
                else if (method == "DeleteFolders")
                    value = network::Value(remote->delete_folders({}, {folder.id}));
                else if (method == "PurgeFolder")
                    value = network::Value(remote->purge_folder(folder));
                else if (method == "AddItem")
                    value = network::Value(remote->add_item(item));
                else if (method == "UpdateItem")
                    value = network::Value(remote->update_item(item));
                else if (method == "MoveItems")
                    value = network::Value(remote->move_items({}, {item}));
                else if (method == "DeleteItems")
                    value = network::Value(remote->delete_items({}, {item.id}));
                else if (method == "GetAssetPermissions")
                    value = network::Value(remote->asset_permissions({}, {}));
                else
                    throw std::invalid_argument("Unknown test operation");
                result = network::Value::object(
                    {{"outcome", "return"}, {"result", value}, {"requests", network::Value(requests)}});
            } catch (const std::exception &) {
                result = network::Value::object({{"outcome", "throw"}, {"requests", network::Value(requests)}});
            }
            std::cout << network::osd_json(result, true) << '\n';
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
