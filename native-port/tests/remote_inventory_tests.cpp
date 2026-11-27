// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/remote_inventory.hpp"
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/network/web_http_clients.hpp"
#include <iostream>
#include <stdexcept>

using namespace opensim;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

template <class Call> void require_throw(Call call, const char *message) {
    bool threw = false;
    try {
        call();
    } catch (const std::exception &) {
        threw = true;
    }
    require(threw, message);
}

class Timer final : public framework::ExpiringCacheTimer {
  public:
    void change(std::int32_t) override {}

    void dispose() override {}
};
} // namespace

int main(int argc, char **argv) {
    try {
        using framework::Uuid;
        using network::ServiceXmlValue;
        const auto owner = Uuid::random(), root = Uuid::random(), child = Uuid::random(), item_id = Uuid::random();
        services::InventoryFolderStore store(":memory:", true);
        services::InventoryService local(store);
        require(local.create_user_inventory(owner), "Local control inventory creation failed");
        server::IniConfig config;
        config.set("InventoryService", "InventoryServerURI", "http://inventory.invalid/base/");
        if (argc == 2 && std::string_view(argv[1]) == "--permissions-probe") {
            std::string text;
            auto probe = services::remote_inventory_service(store, config, [&](const auto &, const auto &) {
                return network::service_xml(ServiceXmlValue::object({{"RESULT", text}}));
            });
            while (std::getline(std::cin, text))
                std::cout << probe->asset_permissions(owner, item_id) << '\n';
            return 0;
        }
        std::vector<network::Form> calls;
        const auto folder = ServiceXmlValue::object({{"ID", root.str()},
                                                     {"Owner", owner.str()},
                                                     {"ParentID", Uuid{}.str()},
                                                     {"Type", "8"},
                                                     {"Version", "17"},
                                                     {"Name", "Remote & root"}});
        const auto item = ServiceXmlValue::object({{"ID", item_id.str()},
                                                   {"Owner", owner.str()},
                                                   {"Folder", root.str()},
                                                   {"AssetID", child.str()},
                                                   {"AssetType", "6"},
                                                   {"InvType", "6"},
                                                   {"Name", "Remote item"},
                                                   {"Description", "description & details"},
                                                   {"CreatorId", owner.str()},
                                                   {"CreatorData", "http://home.invalid/;Creator Name"},
                                                   {"NextPermissions", "2147483648"},
                                                   {"CurrentPermissions", "4294967295"},
                                                   {"BasePermissions", "4294967295"},
                                                   {"GroupPermissions", "7"},
                                                   {"EveryOnePermissions", "8"},
                                                   {"GroupID", child.str()},
                                                   {"GroupOwned", "True"},
                                                   {"SalePrice", "12"},
                                                   {"SaleType", "2"},
                                                   {"Flags", "4294967295"},
                                                   {"CreationDate", "123"}});
        bool offline = false;
        bool item_failure = false;
        std::optional<ServiceXmlValue> list_reply;
        std::optional<ServiceXmlValue> content_reply;
        std::optional<ServiceXmlValue> batch_reply;
        std::optional<ServiceXmlValue> item_batch_reply;
        std::optional<std::string> wire_reply;
        int attempts = 0;
        double cache_time = 0;
        std::function<void()> purge;
        framework::ExpiringCacheBackend clock;
        clock.timestamp = [&] { return cache_time; };
        clock.timer = [&](auto callback, auto interval) {
            require(interval == 15000, "Remote item cache changed its purge interval");
            purge = std::move(callback);
            return std::make_shared<Timer>();
        };
        auto cache = std::make_shared<services::RemoteInventoryItemCache>(15000, clock);
        auto remote = services::remote_inventory_service(
            store, config,
            [&](const auto &url, const auto &body) {
                require(url == "http://inventory.invalid/base/xinventory", "Wrong remote inventory endpoint");
                ++attempts;
                if (offline)
                    throw std::runtime_error("Controlled remote outage");
                auto form = network::parse_service_form(body);
                calls.push_back(form);
                if (wire_reply)
                    return *wire_reply;
                const auto method = std::get<std::string>(form.at("METHOD"));
                ServiceXmlValue reply;
                if (method == "GETINVENTORYSKELETON")
                    reply = ServiceXmlValue::object({{"FOLDERS", ServiceXmlValue::object({{"folder_0", folder}})}});
                else if (method == "GETROOTFOLDER" || method == "GETFOLDER" || method == "GETFOLDERFORTYPE")
                    reply = ServiceXmlValue::object({{"folder", folder}});
                else if (method == "GETITEM")
                    reply = item_failure ? ServiceXmlValue::object({{"RESULT", "False"}})
                                         : ServiceXmlValue::object({{"item", item}});
                else if (method == "GETMULTIPLEITEMS") {
                    if (item_batch_reply)
                        reply = *item_batch_reply;
                    else if (std::get<std::string>(form.at("ITEMS")) == child.str())
                        reply = ServiceXmlValue::object({{"item_0", "null"}});
                    else
                        reply = ServiceXmlValue::object({{"item_0", item}, {"item_1", "null"}});
                } else if (method == "GETMULTIPLEFOLDERSCONTENT")
                    reply = batch_reply.value_or(ServiceXmlValue::object(
                        {{"F_" + root.str(),
                          ServiceXmlValue::object({{"FID", root.str()},
                                                   {"OWNER", owner.str()},
                                                   {"VERSION", "17"},
                                                   {"FOLDERS", ServiceXmlValue::object({{"folder_0", folder}})},
                                                   {"ITEMS", ServiceXmlValue::object({{"item_0", item}})}})}}));
                else if (method == "GETFOLDERCONTENT")
                    reply = content_reply.value_or(
                        ServiceXmlValue::object({{"FOLDERS", ServiceXmlValue::object({{"folder_0", folder}})},
                                                 {"ITEMS", ServiceXmlValue::object({{"item_0", item}})}}));
                else if (method == "GETFOLDERITEMS" || method == "GETACTIVEGESTURES")
                    reply = list_reply.value_or(
                        ServiceXmlValue::object({{"ITEMS", ServiceXmlValue::object({{"item_0", item}})}}));
                else if (method == "GETASSETPERMISSIONS")
                    reply = ServiceXmlValue::object({{"RESULT", "123"}});
                else
                    reply = ServiceXmlValue::object({{"RESULT", "True"}});
                return network::service_xml(reply);
            },
            {}, cache);
        services::InventoryFolderService &folders = *remote;
        require(folders.root(owner)->id == root && folders.for_type(owner, 8)->version == 17,
                "Folder base interface did not dispatch remotely");
        require(folders.skeleton(owner)->at(0).name == "Remote & root", "Remote skeleton changed metadata");
        require(remote->folder(owner, root)->id == root, "Remote folder lookup failed");
        auto value = remote->item(owner, item_id);
        require(value && value->current_permissions == -1 && value->flags == -1 && value->group_owned == 1 &&
                    value->creator_id == owner.str() + ";http://home.invalid/;Creator Name",
                "Remote item lost permissions, creator data or ownership");
        const auto before_cached_read = calls.size();
        require(remote->item(owner, item_id) == value && calls.size() == before_cached_read,
                "Repeated item lookup bypassed the shared cache");
        cache_time = 30000;
        purge();
        remote->item(owner, item_id);
        require(calls.size() == before_cached_read, "Item cache expired at the source's strict equality boundary");
        cache_time = 30001;
        purge();
        remote->item(owner, item_id);
        require(calls.size() == before_cached_read + 1, "Expired item was not fetched again");
        require(remote->multiple_items(owner, {item_id, child})->size() == 2 &&
                    !remote->multiple_items(owner, {item_id, child})->at(1),
                "Missing remote item slot was lost");
        const auto mixed = remote->multiple_items(owner, {child, item_id}).value();
        require(mixed.size() == 2 && mixed[0] == value && !mixed[1] &&
                    std::get<std::string>(calls.back().at("ITEMS")) == child.str() &&
                    std::get<std::string>(calls.back().at("COUNT")) == "1",
                "Mixed cached/pending items did not preserve the original cached-prefix ordering");
        const auto uncached = Uuid::random();
        item_batch_reply = ServiceXmlValue::object({{"RESULT", "False"}});
        require(!remote->multiple_items(owner, {uncached}), "Uncached explicit item batch failure did not return null");
        const auto cached_failure = remote->multiple_items(owner, {uncached, item_id});
        require(cached_failure && cached_failure->size() == 2 && cached_failure->at(0) && !cached_failure->at(1),
                "Explicit item batch failure did not retain its cached prefix");
        offline = true;
        const auto transport_failure = remote->multiple_items(owner, {uncached, Uuid::random()});
        offline = false;
        require(transport_failure && transport_failure->size() == 2 && !transport_failure->at(0) &&
                    !transport_failure->at(1),
                "Request exception did not retain allocated null item slots");
        const auto extra_id = Uuid::random();
        auto extra_item = item;
        for (auto &[key, field] : extra_item.fields)
            if (key == "ID")
                field.text = extra_id.str();
        item_batch_reply = ServiceXmlValue::object({{"item_0", item}, {"item_1", extra_item}});
        const auto excess = remote->multiple_items(owner, {uncached});
        require(excess && excess->size() == 1 && excess->at(0), "Excess item reply lost the populated result");
        const auto before_extra = attempts;
        require(remote->item(owner, extra_id)->id == extra_id && attempts == before_extra,
                "Excess response row did not enter cache before the caught bounds error");
        item_batch_reply.reset();
        require(remote->multiple_content(owner, {root})->at(0)->version == 17 &&
                    remote->content(owner, root).value().items.size() == 1 &&
                    remote->folder_items(owner, root)->size() == 1 && remote->active_gestures(owner)->size() == 1,
                "Remote collections were not decoded");
        require(remote->content(owner, root).value().folder.is_zero(), "Remote content synthesized a folder ID");
        content_reply = ServiceXmlValue::object(
            {{"FOLDERS", ServiceXmlValue::object({{"first", folder}, {"bad", "scalar"}, {"last", folder}})},
             {"ITEMS", ServiceXmlValue::object({{"item", item}})}});
        const auto folder_prefix = remote->content(owner, root).value();
        require(folder_prefix.owner == owner && folder_prefix.folders.size() == 1 && folder_prefix.items.empty(),
                "Folder row failure did not preserve prefix and stop before items");
        content_reply = ServiceXmlValue::object(
            {{"FOLDERS", ServiceXmlValue::object({{"folder", folder}})},
             {"ITEMS", ServiceXmlValue::object({{"first", item}, {"bad", "scalar"}, {"last", item}})}});
        const auto item_prefix = remote->content(owner, root).value();
        require(item_prefix.folders.size() == 1 && item_prefix.items.size() == 1,
                "Item row failure did not preserve both decoded prefixes");
        content_reply =
            ServiceXmlValue::object({{"FOLDERS", "scalar"}, {"ITEMS", ServiceXmlValue::object({{"item", item}})}});
        require(remote->content(owner, root).value().items.empty(),
                "Invalid folder container did not stop before items");
        content_reply = ServiceXmlValue::object({{"ITEMS", ServiceXmlValue::object({{"item", item}})}});
        require(remote->content(owner, root).value().items.size() == 1,
                "Absent folders incorrectly prevented item decode");
        content_reply.reset();
        content_reply = ServiceXmlValue::object({{"RESULT", "False"}});
        require(!remote->content(owner, root), "Explicit content failure did not return null");
        content_reply = ServiceXmlValue::object({{"RESULT", "True"}});
        require(remote->content(owner, root).has_value(), "Empty successful content became null");
        content_reply.reset();
        const auto valid_collection =
            ServiceXmlValue::object({{"FID", root.str()}, {"OWNER", owner.str()}, {"VERSION", "17"}});
        batch_reply = ServiceXmlValue::object({{"F_" + root.str(), valid_collection}});
        const auto missing_slot = remote->multiple_content(owner, {child, root});
        require(missing_slot && missing_slot->size() == 2 && !missing_slot->at(0) && missing_slot->at(1),
                "Missing batch dictionary did not retain its null slot");
        batch_reply = ServiceXmlValue::object({{"F_" + child.str(), ServiceXmlValue::object({{"OWNER", owner.str()}})},
                                               {"F_" + root.str(), valid_collection}});
        const auto invalid_id = remote->multiple_content(owner, {child, root});
        require(invalid_id && invalid_id->at(0) && !invalid_id->at(1),
                "Invalid folder ID did not preserve original non-advancing output index");
        batch_reply = ServiceXmlValue::object({{"RESULT", "False"}});
        require(!remote->multiple_content(owner, {root}), "Explicit batch failure did not return null");
        batch_reply.reset();
        wire_reply = "<ServerResponse><item></ServerResponse>";
        require_throw([&] { remote->root(owner); }, "Root read suppressed malformed stream XML");
        const auto malformed_content = remote->content(owner, root);
        require(malformed_content && malformed_content->owner == owner && malformed_content->items.empty(),
                "Malformed stream XML must return the allocated content after its catch");
        const auto malformed_batch = remote->multiple_content(owner, {root, child});
        require(malformed_batch && malformed_batch->size() == 2 && !malformed_batch->at(0) && !malformed_batch->at(1),
                "Malformed stream XML must retain allocated folder batch slots");
        const auto missing_item = Uuid::random();
        const auto malformed_items = remote->multiple_items(owner, {missing_item});
        require(malformed_items && malformed_items->size() == 1 && !malformed_items->at(0),
                "Malformed stream XML must retain allocated item batch slots");
        require(!remote->item(owner, missing_item), "Malformed stream item read returned a value");
        wire_reply.reset();
        const auto after_malformed_item = attempts;
        require(!remote->item(owner, missing_item) && attempts == after_malformed_item,
                "Malformed stream item read did not populate the original null cache entry");
        require(remote->asset_permissions(owner, child) == 123, "Remote asset permissions failed");
        require(remote->add_item(*value) && remote->update_item(*value), "Remote item writes failed");
        require(std::get<std::string>(calls.back().at("CurrentPermissions")) == "4294967295" &&
                    std::get<std::string>(calls.back().at("CreatorData")) == "http://home.invalid/;Creator Name",
                "Item write did not preserve unsigned permissions and creator data");
        const auto before_updated_read = calls.size();
        require(remote->item(owner, item_id) == value && calls.size() == before_updated_read,
                "Successful update did not refresh the item cache");
        require(remote->move_items(owner, {*value}), "Remote item move failed");
        require(std::get<std::vector<std::string>>(calls.back().at("IDLIST")) ==
                    std::vector<std::string>{item_id.str()},
                "Move list was not encoded as a service list");
        require(remote->delete_items(owner, {item_id, child}), "Remote item delete failed");
        require(std::get<std::vector<std::string>>(calls.back().at("ITEMS")).size() == 2,
                "Delete list was not encoded as a service list");
        const services::InventoryFolder new_folder{"new & folder", -1, 1, child, owner, root};
        require(remote->add_folder(new_folder) && remote->update_folder(new_folder) &&
                    remote->move_folder(new_folder) && remote->delete_folders(owner, {child}) &&
                    remote->purge_folder(child) && remote->create_user_inventory(owner),
                "Remote folder writes failed");
        require(remote->item(owner, item_id).has_value(), "Could not populate cache before failed move");
        offline = true;
        require_throw([&] { remote->move_items(owner, {*value}); }, "Move request failure was suppressed");
        require(!remote->item(owner, item_id), "Failed move did not invalidate the old cached item");
        offline = false;
        const auto after_outage = attempts;
        require(!remote->item(owner, item_id) && attempts == after_outage,
                "Caught request failure did not cache null after transport recovered");
        cache_time += 30000;
        purge();
        require(!remote->item(owner, item_id) && attempts == after_outage,
                "Cached null expired at the strict equality boundary");
        cache_time += 1;
        purge();
        item_failure = true;
        require(!remote->item(owner, item_id) && attempts == after_outage + 1,
                "Expired null did not retry the request");
        require(!remote->item(owner, item_id) && attempts == after_outage + 2,
                "Explicit failure reply was incorrectly cached");
        item_failure = false;
        require(remote->item(owner, item_id).has_value(), "Could not populate cache before failed delete");
        offline = true;
        require_throw([&] { remote->delete_items(owner, {item_id}); }, "Delete request failure was suppressed");
        require(!remote->item(owner, item_id), "Failed delete did not invalidate the old cached item");
        bool skeleton_request_threw = false;
        try {
            remote->skeleton(owner);
        } catch (const std::exception &) {
            skeleton_request_threw = true;
        }
        require(skeleton_request_threw, "Original skeleton request failure did not propagate");
        require_throw([&] { remote->root(owner); }, "Root request failure was suppressed");
        require_throw([&] { remote->folder_items(owner, root); }, "FolderItems request failure was suppressed");
        require_throw([&] { remote->active_gestures(owner); }, "ActiveGestures request failure was suppressed");
        require_throw([&] { remote->add_item(*value); }, "Add item request failure was suppressed");
        require(!remote->item(owner, item_id), "Remote outage fell back to local inventory");
        require(local.root(owner)->id != root && !local.item(owner, item_id),
                "Remote operations wrote local inventory");
        auto null_creator = *value;
        null_creator.id = Uuid::random();
        null_creator.creator_id.clear();
        null_creator.creator_id_is_null = true;
        const auto before_null_update = calls.size();
        bool null_update_threw = false;
        try {
            remote->update_item(null_creator);
        } catch (const std::exception &) {
            null_update_threw = true;
        }
        require(null_update_threw && calls.size() == before_null_update,
                "Null creator update did not fail before sending the original query form");
        offline = false;
        list_reply = ServiceXmlValue::object({{"RESULT", "False"}});
        require(!remote->active_gestures(owner), "ActiveGestures failed reply did not return null");
        require(!remote->folder_items(owner, root), "FolderItems failed reply did not return null");
        list_reply = ServiceXmlValue::object({{"ITEMS", ServiceXmlValue::object({})}});
        const auto empty_gestures = remote->active_gestures(owner);
        require(empty_gestures && empty_gestures->empty(), "ActiveGestures empty success became null");
        const auto empty_items = remote->folder_items(owner, root);
        require(empty_items && empty_items->empty(), "FolderItems empty success became null");
        list_reply = ServiceXmlValue::object({{"RESULT", "True"}});
        require_throw([&] { remote->folder_items(owner, root); }, "FolderItems missing ITEMS was suppressed");
        require_throw([&] { remote->active_gestures(owner); }, "ActiveGestures missing ITEMS was suppressed");
        list_reply = ServiceXmlValue::object({{"ITEMS", "scalar"}});
        require_throw([&] { remote->folder_items(owner, root); }, "FolderItems scalar ITEMS was suppressed");
        require(!remote->active_gestures(owner), "ActiveGestures scalar ITEMS did not return null");
        list_reply = ServiceXmlValue::object(
            {{"ITEMS", ServiceXmlValue::object({{"first", item}, {"scalar", "invalid"}, {"last", item}})}});
        require_throw([&] { remote->folder_items(owner, root); }, "FolderItems skipped a scalar row");
        require_throw([&] { remote->active_gestures(owner); }, "ActiveGestures skipped a scalar row");
        list_reply.reset();
        require(remote->add_item(null_creator) && std::get<std::string>(calls.back().at("CreatorId")).empty(),
                "AddItem did not normalize null creator for its wire request");
        require(store.store(null_creator), "Could not store nullable creator control row");
        auto stored_creator = store.items({{"inventoryID", null_creator.id.str()}}).at(0);
        require(stored_creator.creator_id_is_null && stored_creator.creator_id.empty(),
                "SQLite read lost nullable creator identity");
        null_creator.creator_id_is_null = false;
        require(store.store(null_creator) &&
                    !store.items({{"inventoryID", null_creator.id.str()}}).at(0).creator_id_is_null,
                "SQLite storage collapsed explicitly empty creator into null");
        std::int32_t observed_timeout = 0;
        auto clients = std::make_shared<network::WebHttpClients>([&](const auto &settings) {
            return std::make_shared<network::WebHttpHandler>(
                settings, [&](auto, auto operation) -> std::shared_ptr<framework::ConfigXmlStream> {
                    observed_timeout = operation.timeout_milliseconds;
                    throw std::runtime_error("Controlled transport boundary");
                });
        });
        clients->setup(false, false, nullptr, 32);
        for (const auto &[seconds, expected] : std::vector<std::pair<int, int>>{
                 {-1, 30000}, {0, 30000}, {-7, 30000}, {2, 2000}, {2147483, 2147483000}, {2147484, 30000}}) {
            config.set("InventoryService", "RemoteRequestTimeout", std::to_string(seconds));
            auto timed = services::remote_inventory_service(store, config, {}, clients);
            observed_timeout = 0;
            require_throw([&] { timed->root(owner); }, "Timed root request failure was suppressed");
            require(observed_timeout == expected, "RemoteRequestTimeout changed seconds conversion/default rules");
        }
        std::cout << "PASS: remote inventory reads, writes, service lists, metadata and no local fallback\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
