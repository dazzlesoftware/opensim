// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/remote_inventory.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/framework/integer_text.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <limits>

namespace opensim::services {
namespace {
using framework::Uuid;
using Xml = network::ServiceXmlValue;
using Fields = std::vector<std::pair<std::string, std::optional<std::string>>>;
enum class RequestErrors { Suppress, Propagate };

const Xml &field(const Xml &value, std::string_view name) {
    for (const auto &[key, child] : value.fields)
        if (key == name)
            return child;
    static const Xml missing;
    return missing;
}

int number(const Xml &value, std::string_view name) {
    const auto text = framework::trim_utf8(field(value, name).text);
    std::int64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return 0;
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(parsed));
}

Uuid id(const Xml &value, std::string_view name) {
    Uuid result;
    framework::try_parse_sdk_uuid(field(value, name).text, result);
    return result;
}

bool success(const Xml &reply) {
    if (reply.fields.empty())
        return false;
    for (const auto &[name, value] : reply.fields)
        if (name == "RESULT" && !value.list)
            return framework::ordinal_equals_ignore_case(framework::trim_utf8(value.text), "true");
    return true;
}

int asset_permission_result(const Xml &reply) {
    const auto &value = field(reply, "RESULT");
    if (value.list)
        return 0;
    std::string_view text = value.text;
    // Int32.TryParse uses integer whitespace, an optional sign and a signed
    // range. Permission fields on items use UInt32 and retain different rules.
    while (!text.empty() && (text.front() == ' ' || (text.front() >= '\t' && text.front() <= '\r')))
        text.remove_prefix(1);
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.empty() || text.front() < '0' || text.front() > '9')
        return 0;
    std::uint32_t magnitude = 0;
    if (!framework::try_parse_unsigned_integer(text, magnitude) || magnitude > (negative ? 2147483648u : 2147483647u))
        return 0;
    return negative ? std::bit_cast<std::int32_t>(0u - magnitude) : static_cast<std::int32_t>(magnitude);
}

InventoryFolder decode_folder(const Xml &value) {
    try {
        const auto required_text = [&](std::string_view name) -> const std::string & {
            for (const auto &[key, child] : value.fields)
                if (key == name && !child.list)
                    return child.text;
            throw std::invalid_argument("Missing or non-string inventory folder field");
        };
        const auto parent = framework::parse_sdk_uuid(required_text("ParentID"));
        auto type_text = std::string_view(required_text("Type"));
        while (!type_text.empty() &&
               (type_text.front() == ' ' || (type_text.front() >= '\t' && type_text.front() <= '\r')))
            type_text.remove_prefix(1);
        bool negative = false;
        if (!type_text.empty() && (type_text.front() == '-' || type_text.front() == '+')) {
            negative = type_text.front() == '-';
            type_text.remove_prefix(1);
        }
        std::uint32_t magnitude = 0;
        std::uint16_t version = 0;
        if (type_text.empty() || type_text.front() < '0' || type_text.front() > '9' ||
            !framework::try_parse_unsigned_integer(type_text, magnitude) || magnitude > (negative ? 32768u : 32767u) ||
            !framework::try_parse_unsigned_integer(required_text("Version"), version))
            throw std::invalid_argument("Invalid inventory folder type or version");
        const auto type = negative ? -static_cast<int>(magnitude) : static_cast<int>(magnitude);
        const auto &name = required_text("Name");
        const auto owner = framework::parse_sdk_uuid(required_text("Owner"));
        const auto identifier = framework::parse_sdk_uuid(required_text("ID"));
        return {name, type, version, identifier, owner, parent};
    } catch (const std::exception &) {
        // Original BuildFolder discards the entire partially built object on
        // conversion failure and returns a fresh default InventoryFolderBase.
        return {};
    }
}

InventoryItem decode_item(const Xml &value) {
    try {
        const auto text = [&](std::string_view name) -> const std::string & {
            for (const auto &[key, child] : value.fields)
                if (key == name && !child.list)
                    return child.text;
            throw std::invalid_argument("Missing or non-string inventory item field");
        };
        const auto signed_integer = [&](std::string_view name) {
            auto input = std::string_view(text(name));
            while (!input.empty() && (input.front() == ' ' || (input.front() >= '\t' && input.front() <= '\r')))
                input.remove_prefix(1);
            bool negative = false;
            if (!input.empty() && (input.front() == '-' || input.front() == '+')) {
                negative = input.front() == '-';
                input.remove_prefix(1);
            }
            std::uint32_t magnitude = 0;
            if (input.empty() || input.front() < '0' || input.front() > '9' ||
                !framework::try_parse_unsigned_integer(input, magnitude) ||
                magnitude > (negative ? 2147483648u : 2147483647u))
                throw std::invalid_argument("Invalid signed inventory item integer");
            return negative ? std::bit_cast<std::int32_t>(0u - magnitude) : static_cast<std::int32_t>(magnitude);
        };
        const auto unsigned_integer = [&](std::string_view name) {
            std::uint32_t parsed = 0;
            if (!framework::try_parse_unsigned_integer(text(name), parsed))
                throw std::invalid_argument("Invalid unsigned inventory item integer");
            return std::bit_cast<std::int32_t>(parsed);
        };
        InventoryItem item;
        item.asset_id = framework::parse_sdk_uuid(text("AssetID"));
        item.asset_type = signed_integer("AssetType");
        item.name = text("Name");
        item.owner = framework::parse_sdk_uuid(text("Owner"));
        item.id = framework::parse_sdk_uuid(text("ID"));
        item.inventory_type = signed_integer("InvType");
        item.parent = framework::parse_sdk_uuid(text("Folder"));
        item.creator_id = text("CreatorId");
        item.next_permissions = unsigned_integer("NextPermissions");
        item.current_permissions = unsigned_integer("CurrentPermissions");
        item.base_permissions = unsigned_integer("BasePermissions");
        item.everyone_permissions = unsigned_integer("EveryOnePermissions");
        item.group_permissions = unsigned_integer("GroupPermissions");
        item.group_id = framework::parse_sdk_uuid(text("GroupID"));
        const auto group_owned = framework::trim_utf8(text("GroupOwned"));
        if (!framework::ordinal_equals_ignore_case(group_owned, "true") &&
            !framework::ordinal_equals_ignore_case(group_owned, "false"))
            throw std::invalid_argument("Invalid inventory item boolean");
        item.group_owned = framework::ordinal_equals_ignore_case(group_owned, "true");
        item.sale_price = signed_integer("SalePrice");
        std::uint8_t sale_type = 0;
        if (!framework::try_parse_unsigned_integer(text("SaleType"), sale_type))
            throw std::invalid_argument("Invalid inventory item sale type");
        item.sale_type = sale_type;
        item.flags = unsigned_integer("Flags");
        item.creation_date = signed_integer("CreationDate");
        item.description = text("Description");
        if (framework::trim_utf8(item.description).empty())
            item.description.clear();
        for (const auto &[key, child] : value.fields)
            if (key == "CreatorData") {
                if (child.list)
                    throw std::invalid_argument("Non-string inventory creator data");
                if (!child.text.empty())
                    item.creator_id += ';' + child.text;
                break;
            }
        return item;
    } catch (const std::exception &) {
        InventoryItem item;
        item.creator_id_is_null = true;
        const auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        item.creation_date = seconds >= -2147483648ll && seconds <= 2147483647ll
                                 ? static_cast<std::int32_t>(seconds)
                                 : std::numeric_limits<std::int32_t>::min();
        return item;
    }
}

template <typename T, typename Decode> std::vector<T> rows(const Xml &reply, std::string_view name, Decode decode) {
    std::vector<T> result;
    if (!success(reply))
        return result;
    for (const auto &[key, value] : field(reply, name).fields)
        if (value.list)
            result.push_back(decode(value));
    return result;
}

std::string joined(const std::vector<Uuid> &ids) {
    std::string result;
    for (const auto &value : ids) {
        if (!result.empty())
            result += ',';
        result += value.str();
    }
    return result;
}

Fields encode_folder(const InventoryFolder &folder) {
    return {{"ParentID", folder.parent.str()},
            {"Type", std::to_string(folder.type)},
            {"Version", std::to_string(folder.version)},
            {"Name", folder.name},
            {"Owner", folder.owner.str()},
            {"ID", folder.id.str()}};
}

Fields encode_item(const InventoryItem &item, bool adding = false) {
    if (!adding && item.creator_id_is_null && item.creator_id.empty())
        throw std::runtime_error("Object reference not set to an instance of an object.");
    const auto separator = item.creator_id.find(';');
    const auto permissions = [](int value) { return std::to_string(static_cast<std::uint32_t>(value)); };
    return {{"AssetID", item.asset_id.str()},
            {"AssetType", std::to_string(item.asset_type)},
            {"Name", item.name},
            {"Owner", item.owner.str()},
            {"ID", item.id.str()},
            {"InvType", std::to_string(item.inventory_type)},
            {"Folder", item.parent.str()},
            {"CreatorId", item.creator_id.substr(0, separator)},
            {"CreatorData", separator == std::string::npos ? "" : item.creator_id.substr(separator + 1)},
            {"Description", item.description},
            {"NextPermissions", permissions(item.next_permissions)},
            {"CurrentPermissions", permissions(item.current_permissions)},
            {"BasePermissions", permissions(item.base_permissions)},
            {"EveryOnePermissions", permissions(item.everyone_permissions)},
            {"GroupPermissions", permissions(item.group_permissions)},
            {"GroupID", item.group_id.str()},
            {"GroupOwned", item.group_owned ? "True" : "False"},
            {"SalePrice", std::to_string(item.sale_price)},
            {"SaleType", std::to_string(item.sale_type)},
            {"Flags", permissions(item.flags)},
            {"CreationDate", std::to_string(item.creation_date)}};
}

class RemoteInventory final : public InventoryService {
  public:
    RemoteInventory(InventoryFolderStore &store, std::string uri, network::RestFormsRequest request,
                    std::shared_ptr<RemoteInventoryItemCache> cache)
        : InventoryService(store), uri_(std::move(uri)), request_(std::move(request)), cache_(std::move(cache)) {}

    bool create_user_inventory(const Uuid &principal) override {
        return success(send("CREATEUSERINVENTORY", {{"PRINCIPAL", principal.str()}}, RequestErrors::Propagate));
    }

    std::optional<std::vector<InventoryFolder>> skeleton(const Uuid &principal) override {
        const auto reply = send("GETINVENTORYSKELETON", {{"PRINCIPAL", principal.str()}}, RequestErrors::Propagate);
        if (!success(reply))
            return {};
        const Xml *folders = nullptr;
        for (const auto &[key, value] : reply.fields)
            if (key == "FOLDERS") {
                folders = &value;
                break;
            }
        // The original dictionary lookup/cast precedes its list-unwrapping
        // catch. Only row failures return an already decoded prefix.
        if (!folders)
            throw std::out_of_range("Inventory skeleton response has no FOLDERS collection");
        if (!folders->list)
            throw std::invalid_argument("Inventory skeleton FOLDERS value is not a dictionary");
        std::vector<InventoryFolder> result;
        try {
            for (const auto &[key, value] : folders->fields) {
                if (!value.list)
                    throw std::invalid_argument("Inventory skeleton row is not a dictionary");
                result.push_back(decode_folder(value));
            }
        } catch (const std::exception &) {
        }
        return result;
    }

    std::optional<InventoryFolder> root(const Uuid &principal) override {
        return single_folder("GETROOTFOLDER", {{"PRINCIPAL", principal.str()}}, RequestErrors::Propagate);
    }

    std::optional<InventoryFolder> for_type(const Uuid &principal, int type) override {
        return single_folder("GETFOLDERFORTYPE", {{"PRINCIPAL", principal.str()}, {"TYPE", std::to_string(type)}},
                             RequestErrors::Propagate);
    }

    std::optional<InventoryFolder> folder(const Uuid &principal, const Uuid &folder) override {
        return single_folder("GETFOLDER", {{"PRINCIPAL", principal.str()}, {"ID", folder.str()}});
    }

    std::optional<InventoryItem> item(const Uuid &principal, const Uuid &item) override {
        std::optional<InventoryItem> result;
        if (cache_->try_get_value(item, result))
            return result;
        try {
            const auto reply =
                send("GETITEM", {{"PRINCIPAL", principal.str()}, {"ID", item.str()}}, RequestErrors::Propagate);
            if (!success(reply))
                return {};
            const auto &value = field(reply, "item");
            if (value.list)
                result = decode_item(value);
        } catch (const std::exception &) {
            // The original caches the null retrieved value after its catch.
            // An explicit failed reply returns above without populating cache.
        }
        cache_->add_or_update(item, result, 30.0);
        return result;
    }

    std::optional<InventoryCollection> content(const Uuid &principal, const Uuid &folder) override {
        InventoryCollection result;
        result.owner = principal;
        try {
            const auto reply = send("GETFOLDERCONTENT", {{"PRINCIPAL", principal.str()}, {"FOLDER", folder.str()}},
                                    RequestErrors::Propagate);
            if (!success(reply))
                return std::nullopt;
            const auto append = [&](std::string_view name, auto &destination, auto decode) {
                const auto entry = std::find_if(reply.fields.begin(), reply.fields.end(),
                                                [&](const auto &entry) { return entry.first == name; });
                if (entry == reply.fields.end())
                    return;
                if (!entry->second.list)
                    throw std::invalid_argument("Inventory collection is not a dictionary");
                for (const auto &[key, value] : entry->second.fields) {
                    if (!value.list)
                        throw std::invalid_argument("Inventory collection row is not a dictionary");
                    destination.push_back(decode(value));
                }
            };
            append("FOLDERS", result.folders, decode_folder);
            append("ITEMS", result.items, decode_item);
        } catch (const std::exception &) {
            // Original GetFolderContent returns the prefix accumulated before
            // an error, including an empty collection after request failure.
        }
        return result;
    }

    InventoryCollections multiple_content(const Uuid &principal, const std::vector<Uuid> &folders) override {
        std::vector<std::optional<InventoryCollection>> result(folders.size());
        try {
            const auto reply = send("GETMULTIPLEFOLDERSCONTENT",
                                    {{"PRINCIPAL", principal.str()},
                                     {"FOLDERS", joined(folders)},
                                     {"COUNT", std::to_string(folders.size())}},
                                    RequestErrors::Propagate);
            if (!success(reply))
                return std::nullopt;
            std::size_t next = 0;
            for (const auto &folder : folders) {
                const auto &value = field(reply, "F_" + folder.str());
                if (!value.list) {
                    ++next;
                    continue;
                }
                const auto find = [&](std::string_view name) -> const Xml * {
                    for (const auto &[key, child] : value.fields)
                        if (key == name)
                            return &child;
                    return nullptr;
                };
                InventoryCollection collection;
                const auto uuid_field = [&](std::string_view name, Uuid &out) {
                    const auto entry = find(name);
                    if (!entry)
                        return false;
                    if (entry->list)
                        throw std::invalid_argument("Inventory collection UUID is not a string");
                    return framework::try_parse_sdk_uuid(entry->text, out);
                };
                // The original continues without advancing its output index
                // when a dictionary has a missing or invalid folder/owner ID.
                if (!uuid_field("FID", collection.folder) || !uuid_field("OWNER", collection.owner))
                    continue;
                collection.version = -1;
                if (const auto version = find("VERSION")) {
                    if (version->list)
                        throw std::invalid_argument("Inventory collection version is not a string");
                    auto text = std::string_view(version->text);
                    while (!text.empty() && (text.front() == ' ' || (text.front() >= '\t' && text.front() <= '\r')))
                        text.remove_prefix(1);
                    bool negative = false;
                    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
                        negative = text.front() == '-';
                        text.remove_prefix(1);
                    }
                    std::uint32_t magnitude = 0;
                    if (!text.empty() && text.front() >= '0' && text.front() <= '9' &&
                        framework::try_parse_unsigned_integer(text, magnitude) &&
                        magnitude <= (negative ? 2147483648u : 2147483647u))
                        collection.version =
                            negative ? std::bit_cast<std::int32_t>(0u - magnitude) : static_cast<int>(magnitude);
                }
                const auto append = [&](std::string_view name, auto &destination, auto decode) {
                    const auto entry = find(name);
                    if (!entry || !entry->list)
                        return;
                    for (const auto &[key, child] : entry->fields) {
                        if (!child.list)
                            throw std::invalid_argument("Inventory collection row is not a dictionary");
                        destination.push_back(decode(child));
                    }
                };
                append("FOLDERS", collection.folders, decode_folder);
                append("ITEMS", collection.items, decode_item);
                result[next++] = std::move(collection);
            }
        } catch (const std::exception &) {
            // Only complete collections assigned before the failure survive.
        }
        return result;
    }

    InventoryItemResults multiple_items(const Uuid &principal, const std::vector<Uuid> &items) override {
        std::vector<std::optional<InventoryItem>> result(items.size());
        std::vector<Uuid> pending;
        std::size_t next = 0;
        for (const auto &item : items) {
            std::optional<InventoryItem> cached;
            if (cache_->try_get_value(item, cached))
                result[next++] = std::move(cached);
            else
                pending.push_back(item);
        }
        if (pending.empty())
            return result;
        try {
            const auto reply = send(
                "GETMULTIPLEITEMS",
                {{"PRINCIPAL", principal.str()}, {"ITEMS", joined(pending)}, {"COUNT", std::to_string(pending.size())}},
                RequestErrors::Propagate);
            if (!success(reply))
                return next == 0 ? InventoryItemResults{} : InventoryItemResults(result);
            for (const auto &[key, value] : reply.fields)
                if (framework::invariant_starts_with(key, "item_")) {
                    if (value.list) {
                        auto decoded = decode_item(value);
                        // The original caches an excess row before the array
                        // assignment throws and stops response enumeration.
                        cache_->add_or_update(decoded.id, std::optional(decoded), 30.0);
                        result.at(next++) = std::move(decoded);
                    } else
                        result.at(next++) = std::nullopt;
                }
        } catch (const std::exception &) {
        }
        return result;
    }

    std::optional<std::vector<InventoryItem>> folder_items(const Uuid &principal, const Uuid &folder) override {
        return item_rows(send("GETFOLDERITEMS", {{"PRINCIPAL", principal.str()}, {"FOLDER", folder.str()}},
                              RequestErrors::Propagate),
                         false);
    }

    bool add_folder(InventoryFolder folder) override {
        return success(send("ADDFOLDER", encode_folder(folder), RequestErrors::Propagate));
    }

    bool update_folder(InventoryFolder folder) override {
        return success(send("UPDATEFOLDER", encode_folder(folder), RequestErrors::Propagate));
    }

    bool move_folder(const InventoryFolder &folder) override {
        return success(
            send("MOVEFOLDER",
                 {{"PRINCIPAL", folder.owner.str()}, {"ID", folder.id.str()}, {"ParentID", folder.parent.str()}},
                 RequestErrors::Propagate));
    }

    bool add_item(const InventoryItem &item) override {
        return success(send("ADDITEM", encode_item(item, true), RequestErrors::Propagate));
    }

    bool update_item(InventoryItem &item) override {
        const bool saved = success(send("UPDATEITEM", encode_item(item), RequestErrors::Propagate));
        if (saved)
            cache_->add_or_update(item.id, std::optional(item), 30.0);
        return saved;
    }

    bool move_items(const Uuid &principal, const std::vector<InventoryItem> &items) override {
        Fields fields{{"PRINCIPAL", principal.str()}};
        for (const auto &item : items) {
            cache_->remove(item.id);
            fields.emplace_back("IDLIST[]", item.id.str());
            fields.emplace_back("DESTLIST[]", item.parent.str());
        }
        return success(send("MOVEITEMS", std::move(fields), RequestErrors::Propagate));
    }

    bool delete_items(const Uuid &principal, const std::vector<Uuid> &items) override {
        for (const auto &item : items)
            cache_->remove(item);
        return erase("DELETEITEMS", "ITEMS[]", principal, items);
    }

    bool delete_folders(const Uuid &principal, const std::vector<Uuid> &folders, bool) override {
        return erase("DELETEFOLDERS", "FOLDERS[]", principal, folders);
    }

    bool purge_folder(const Uuid &folder, bool) override {
        return success(send("PURGEFOLDER", {{"ID", folder.str()}}, RequestErrors::Propagate));
    }

    std::optional<std::vector<InventoryItem>> active_gestures(const Uuid &principal) override {
        return item_rows(send("GETACTIVEGESTURES", {{"PRINCIPAL", principal.str()}}, RequestErrors::Propagate), true);
    }

    int asset_permissions(const Uuid &principal, const Uuid &asset) override {
        return asset_permission_result(send(
            "GETASSETPERMISSIONS", {{"PRINCIPAL", principal.str()}, {"ASSET", asset.str()}}, RequestErrors::Propagate));
    }

  private:
    static std::optional<std::vector<InventoryItem>> item_rows(const Xml &reply, bool gestures) {
        if (!success(reply))
            return {};
        const auto entry = std::find_if(reply.fields.begin(), reply.fields.end(),
                                        [](const auto &entry) { return entry.first == "ITEMS"; });
        if (entry == reply.fields.end())
            throw std::out_of_range("Inventory response has no ITEMS dictionary");
        if (!entry->second.list) {
            if (gestures)
                return {};
            throw std::invalid_argument("Inventory ITEMS value is not a dictionary");
        }
        std::vector<InventoryItem> result;
        for (const auto &[key, value] : entry->second.fields) {
            if (!value.list)
                throw std::invalid_argument("Inventory item row is not a dictionary");
            result.push_back(decode_item(value));
        }
        return result;
    }

    Xml send(std::string_view method, Fields fields, RequestErrors errors = RequestErrors::Suppress) const {
        fields.insert(fields.begin(), {"METHOD", std::string(method)});
        const auto perform = [&] {
            const auto reply = request_(uri_, network::build_service_form(fields));
            return reply.empty() ? Xml{} : network::parse_service_xml_stream(reply);
        };
        if (errors == RequestErrors::Propagate)
            return perform();
        try {
            return perform();
        } catch (...) {
            return {};
        }
    }

    std::optional<InventoryFolder> single_folder(std::string_view method, Fields fields,
                                                 RequestErrors errors = RequestErrors::Suppress) const {
        const auto reply = send(method, std::move(fields), errors);
        if (!success(reply))
            return std::nullopt;
        if (errors == RequestErrors::Propagate) {
            const auto entry = std::find_if(reply.fields.begin(), reply.fields.end(),
                                            [](const auto &entry) { return entry.first == "folder"; });
            if (entry == reply.fields.end())
                throw std::out_of_range("Inventory response has no folder dictionary");
            if (!entry->second.list)
                throw std::invalid_argument("Inventory folder value is not a dictionary");
            return decode_folder(entry->second);
        }
        const auto &value = field(reply, "folder");
        return value.list ? std::optional(decode_folder(value)) : std::nullopt;
    }

    bool erase(std::string_view method, std::string name, const Uuid &principal, const std::vector<Uuid> &ids) {
        Fields fields{{"PRINCIPAL", principal.str()}};
        for (const auto &id : ids)
            fields.emplace_back(name, id.str());
        return success(send(method, std::move(fields), RequestErrors::Propagate));
    }

    std::string uri_;
    network::RestFormsRequest request_;
    std::shared_ptr<RemoteInventoryItemCache> cache_;
};
} // namespace

std::unique_ptr<InventoryService> remote_inventory_service(InventoryFolderStore &store, const server::IniConfig &config,
                                                           network::RestFormsRequest request,
                                                           std::shared_ptr<network::WebHttpClients> clients,
                                                           std::shared_ptr<RemoteInventoryItemCache> cache) {
    auto uri = config.get("InventoryService", "InventoryServerURI");
    if (uri.empty())
        throw std::invalid_argument("InventoryServerURI is missing");
    if (uri.back() != '/')
        uri += '/';
    uri += "xinventory";
    if (!request) {
        std::optional<std::pair<std::string, std::string>> auth;
        if (config.get("InventoryService", "AuthType", config.get("Network", "AuthType")) == "BasicHttpAuthentication")
            auth = {{config.get("InventoryService", "HttpAuthUsername", config.get("Network", "HttpAuthUsername")),
                     config.get("InventoryService", "HttpAuthPassword", config.get("Network", "HttpAuthPassword"))}};
        const auto timeout = std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(config.integer("InventoryService", "RemoteRequestTimeout", -1)) * 1000u);
        request = clients ? network::rest_forms_transport(std::move(clients), auth, timeout, true)
                          : network::rest_forms_transport(auth, timeout, true);
    }
    if (!cache) {
        static auto shared_cache = std::make_shared<RemoteInventoryItemCache>(15000);
        cache = shared_cache;
    }
    return std::make_unique<RemoteInventory>(store, std::move(uri), std::move(request), std::move(cache));
}
} // namespace opensim::services
