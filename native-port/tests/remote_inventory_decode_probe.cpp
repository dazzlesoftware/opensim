// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/remote_inventory.hpp"
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>

using namespace opensim;

namespace {
network::Value empty_array() {
    network::Value result;
    result.kind = network::Value::Kind::Array;
    return result;
}

network::ServiceXmlValue xml(const network::Value &value) {
    if (value.kind != network::Value::Kind::Map)
        return network::ServiceXmlValue(network::osd_string(value));
    network::ServiceXmlValue result = network::ServiceXmlValue::object({});
    for (const auto &key : value.ordered_keys())
        result.fields.emplace_back(key, xml(value.get(key)));
    return result;
}

network::Value folder_value(const services::InventoryFolder &folder) {
    return network::Value::object({{"Name", network::Value(folder.name)},
                                   {"Type", network::Value(folder.type)},
                                   {"Version", network::Value(folder.version)},
                                   {"ID", network::Value(folder.id.str())},
                                   {"Owner", network::Value(folder.owner.str())},
                                   {"ParentID", network::Value(folder.parent.str())}});
}

network::Value collection_value(const services::InventoryCollection &collection) {
    auto folders = empty_array();
    auto items = empty_array();
    for (const auto &folder : collection.folders)
        folders.array.push_back(folder_value(folder));
    for (const auto &item : collection.items)
        items.array.push_back(
            network::Value::object({{"Name", item.name}, {"ID", item.id.str()}, {"Owner", item.owner.str()}}));
    return network::Value::object({{"Owner", collection.owner.str()},
                                   {"Folder", collection.folder.str()},
                                   {"Version", network::Value(collection.version)},
                                   {"Folders", folders},
                                   {"Items", items}});
}

network::Value item_value(const std::optional<services::InventoryItem> &item) {
    return item ? network::Value::object({{"Name", item->name}, {"ID", item->id.str()}, {"Owner", item->owner.str()}})
                : network::Value{};
}
} // namespace

int main(int argc, char **argv) {
    try {
        services::InventoryFolderStore store(":memory:", true);
        server::IniConfig config;
        config.set("InventoryService", "InventoryServerURI", "http://inventory.invalid");
        network::Value response;
        const bool items = argc == 2 && std::string_view(argv[1]) == "--items";
        const bool skeleton = argc == 2 && std::string_view(argv[1]) == "--skeleton";
        const bool single = argc == 2 && std::string_view(argv[1]) == "--single";
        const bool collections = argc == 2 && std::string_view(argv[1]) == "--collections";
        const bool multiple_items = argc == 2 && std::string_view(argv[1]) == "--multiple-items";
        bool transport_failure = false;
        int requests = 0;
        auto cache = std::make_shared<services::RemoteInventoryItemCache>(15000);
        auto remote = services::remote_inventory_service(
            store, config,
            [&](const auto &, const auto &) {
                ++requests;
                if (transport_failure)
                    throw std::runtime_error("Controlled request failure");
                if (skeleton || single || collections || multiple_items)
                    return network::service_xml(xml(response));
                return network::service_xml(
                    network::ServiceXmlValue::object({{items ? "item" : "folder", xml(response)}}));
            },
            {}, cache);
        std::string line;
        while (std::getline(std::cin, line)) {
            response = network::parse_osd_json(line);
            if (multiple_items) {
                const auto frame = response;
                const framework::Uuid owner("22222222-2222-4222-8222-222222222222");
                cache->clear();
                for (const auto &seed : frame.get("cache").array) {
                    const framework::Uuid id(seed.get("id").text);
                    std::optional<services::InventoryItem> item;
                    if (!network::osd_boolean(seed.get("null"))) {
                        item.emplace();
                        item->id = id;
                        item->owner = owner;
                        item->name = seed.get("name").text;
                    }
                    cache->add_or_update(id, item, 30.0);
                }
                auto steps = empty_array();
                for (const auto &step : frame.get("steps").array) {
                    response = step.get("reply");
                    transport_failure = network::osd_boolean(step.get("failure"));
                    requests = 0;
                    std::vector<framework::Uuid> ids;
                    for (const auto &id : step.get("ids").array)
                        ids.emplace_back(id.text);
                    network::Value result;
                    if (step.get("method").text == "single") {
                        result = item_value(remote->item(owner, ids.at(0)));
                    } else {
                        const auto values = remote->multiple_items(owner, ids);
                        if (values) {
                            result = empty_array();
                            for (const auto &item : *values)
                                result.array.push_back(item_value(item));
                        }
                    }
                    steps.array.push_back(
                        network::Value::object({{"result", result}, {"requests", network::Value(requests)}}));
                }
                auto inspected = empty_array();
                for (const auto &id : frame.get("inspect").array) {
                    std::optional<services::InventoryItem> item;
                    const bool hit = cache->try_get_value(framework::Uuid(id.text), item);
                    inspected.array.push_back(
                        network::Value::object({{"hit", network::Value(hit)}, {"item", item_value(item)}}));
                }
                std::cout << network::osd_json(network::Value::object({{"steps", steps}, {"cache", inspected}}), true)
                          << '\n';
                continue;
            }
            if (collections) {
                const auto frame = response;
                response = frame.get("reply");
                transport_failure = network::osd_boolean(frame.get("transport_failure"));
                std::vector<framework::Uuid> ids;
                for (const auto &id : frame.get("ids").array)
                    ids.emplace_back(id.text);
                const framework::Uuid owner("22222222-2222-4222-8222-222222222222");
                network::Value result;
                if (frame.get("method").text == "single") {
                    const auto collection = remote->content(owner, ids.at(0));
                    if (collection)
                        result = collection_value(*collection);
                } else {
                    const auto values = remote->multiple_content(owner, ids);
                    if (values) {
                        result = empty_array();
                        for (const auto &collection : *values)
                            result.array.push_back(collection ? collection_value(*collection) : network::Value{});
                    }
                }
                std::cout << network::osd_json(network::Value::object({{"result", result}}), true) << '\n';
                continue;
            }
            if (skeleton || single) {
                const auto frame = response;
                response = frame.get("reply");
                transport_failure = network::osd_boolean(frame.get("transport_failure"));
                network::Value result;
                try {
                    if (single) {
                        const auto method = frame.get("method").text;
                        const auto folder = method == "GetRootFolder"      ? remote->root({})
                                            : method == "GetFolderForType" ? remote->for_type({}, 6)
                                                                           : remote->folder({}, {});
                        result = network::Value::object(
                            {{"outcome", "return"}, {"folder", folder ? folder_value(*folder) : network::Value{}}});
                    } else {
                        const auto folders = remote->skeleton({});
                        network::Value rows;
                        if (folders) {
                            rows.kind = network::Value::Kind::Array;
                            for (const auto &folder : *folders)
                                rows.array.push_back(folder_value(folder));
                        }
                        result = network::Value::object({{"outcome", "return"}, {"folders", rows}});
                    }
                } catch (const std::out_of_range &) {
                    result = network::Value::object({{"outcome", "missing collection"}});
                } catch (const std::invalid_argument &) {
                    result = network::Value::object({{"outcome", "non-dictionary collection"}});
                } catch (const std::exception &) {
                    result = network::Value::object({{"outcome", "request failure"}});
                }
                std::cout << network::osd_json(result, true) << '\n';
                continue;
            }
            if (items) {
                const auto item = remote->item({}, framework::Uuid::random());
                if (!item)
                    throw std::runtime_error("Successful dictionary reply did not return an item");
                const auto mask = [](int bits) {
                    network::Value value;
                    value.kind = network::Value::Kind::Long;
                    value.long_value = static_cast<std::uint32_t>(bits);
                    return value;
                };
                const auto result = network::Value::object(
                    {{"Name", network::Value(item->name)},
                     {"Description", network::Value(item->description)},
                     {"AssetType", network::Value(item->asset_type)},
                     {"AssetID", network::Value(item->asset_id.str())},
                     {"InvType", network::Value(item->inventory_type)},
                     {"Folder", network::Value(item->parent.str())},
                     {"CreatorIdentification", item->creator_id_is_null && item->creator_id.empty()
                                                   ? network::Value{}
                                                   : network::Value(item->creator_id)},
                     {"NextPermissions", mask(item->next_permissions)},
                     {"CurrentPermissions", mask(item->current_permissions)},
                     {"BasePermissions", mask(item->base_permissions)},
                     {"EveryOnePermissions", mask(item->everyone_permissions)},
                     {"GroupPermissions", mask(item->group_permissions)},
                     {"GroupID", network::Value(item->group_id.str())},
                     {"GroupOwned", network::Value(item->group_owned != 0)},
                     {"SalePrice", network::Value(item->sale_price)},
                     {"SaleType", network::Value(item->sale_type)},
                     {"Flags", mask(item->flags)},
                     {"CreationDate", network::Value(item->creation_date)},
                     {"ID", network::Value(item->id.str())},
                     {"Owner", network::Value(item->owner.str())}});
                std::cout << network::osd_json(result, true) << '\n';
                continue;
            }
            const auto folder = remote->folder({}, {});
            if (!folder)
                throw std::runtime_error("Successful dictionary reply did not return a folder");
            const auto result = folder_value(*folder);
            std::cout << network::osd_json(result, true) << '\n';
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
