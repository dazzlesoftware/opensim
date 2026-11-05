// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/inventory_descendents.hpp"
#include "opensim/network/osd_value.hpp"
#include <set>
#include <stdexcept>
namespace opensim::services {
namespace {
using framework::Uuid;
constexpr auto empty = "<llsd><map><key>folders</key><array /></map></llsd>";
struct Request {
    Uuid id, owner;
    int sort = 0;
    bool folders = false, items = false;
};
struct Collection {
    InventoryCollection value;
    int descendants = 0;
};
void key(std::string &out, std::string_view name) {
    out += "<key>";
    out += name;
    out += "</key>";
}
void number(std::string &out, std::string_view name, int n) {
    key(out, name);
    out += n ? "<integer>" + std::to_string(n) + "</integer>" : "<integer />";
}
void uuid(std::string &out, std::string_view name, Uuid id) {
    key(out, name);
    out += id.is_zero() ? "<uuid />" : "<uuid>" + id.str() + "</uuid>";
}
std::string escaped(std::string_view s) {
    std::string out;
    for (char c : s)
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out += c;
        }
    return out;
}
void bad_xml(std::string &out, const std::vector<Uuid> &bad) {
    out += "<map><key>bad_folders</key><array>";
    for (auto id : bad)
        out += "<map><key>folder_id</key><uuid>" + id.str() +
               "</uuid><key>error</key><string>Unknown</string></map>";
    out += "</array></map>";
}
void collection_xml(std::string &out, const Collection &collection) {
    const auto &c = collection.value;
    out += "<map>";
    uuid(out, "folder_id", c.folder);
    uuid(out, "agent_id", c.owner);
    uuid(out, "owner_id", c.owner);
    number(out, "descendents", collection.descendants);
    number(out, "version", c.version);
    key(out, "categories");
    if (c.folders.empty())
        out += "<array />";
    else {
        out += "<array>";
        for (const auto &folder : c.folders) {
            out += "<map>";
            uuid(out, "category_id", folder.id);
            uuid(out, "parent_id", folder.parent);
            key(out, "name");
            out += folder.name.empty() ? "<string />" : "<string>" + escaped(folder.name) + "</string>";
            number(out, "type_default", folder.type);
            number(out, "version", folder.version);
            out += "</map>";
        }
        out += "</array>";
    }
    key(out, "items");
    if (c.items.empty())
        out += "<array />";
    else {
        out += "<array>";
        for (const auto &item : c.items)
            out += inventory_item_llsd(item);
        out += "</array>";
    }
    out += "</map>";
}
}
FetchInventoryDescendentsCore::FetchInventoryDescendentsCore(InventoryDescendentsBackend backend)
    : backend_(std::move(backend)) {
    if (!backend_.multiple_content || !backend_.folder || !backend_.multiple_items ||
        !backend_.deserialize_xml)
        throw std::invalid_argument("Null inventory descendents backend");
    if (backend_.library_owner && !backend_.library_content)
        throw std::invalid_argument("Missing inventory library backend");
}
network::HttpResponse FetchInventoryDescendentsCore::handle(const network::HttpRequest &incoming,
                                                            InventoryBadRequests *cache) const {
    network::HttpResponse response;
    response.content_type = "text/html;charset=UTF-8";
    response.body = empty;
    auto log = [&](const char *category, InventoryLogLevel level, const std::string &message) {
        if (backend_.diagnostic)
            backend_.diagnostic(level, message);
        else if (backend_.log)
            backend_.log(category);
    };
    std::vector<Request> requests;
    std::vector<Uuid> bad;
    try {
        auto root = backend_.deserialize_xml(incoming.body);
        const auto &map = root.view();
        if (map.kind != network::Value::Kind::Map)
            throw std::invalid_argument(inventory_cast_message(map.kind));
        const auto &raw_values = map.get("folders");
        if (raw_values.kind == network::Value::Kind::NullReference)
            return response;
        const auto &values = raw_values.view();
        if (values.kind != network::Value::Kind::Array || values.array.empty())
            return response;
        for (const auto &value : values.array) {
            const auto &m = value.view();
            if (m.kind != network::Value::Kind::Map)
                throw std::invalid_argument("Object reference not set to an instance of an object.");
            auto id = network::osd_uuid(m.get("folder_id"));
            if (!cache)
                throw std::invalid_argument("Object reference not set to an instance of an object.");
            if (cache->contains(id))
                bad.push_back(id);
            else {
                Request request;
                try {
                    request.id = id;
                    request.owner = network::osd_uuid(m.get("owner_id"));
                    request.sort = network::osd_integer(m.get("sort_order"));
                    request.folders = network::osd_boolean(m.get("fetch_folders"));
                    request.items = network::osd_boolean(m.get("fetch_items"));
                } catch (...) {
                    log("deserialize-folder", InventoryLogLevel::Debug,
                        "[WEB FETCH INV DESC HANDLER]: caught exception doing OSD deserialize" +
                            inventory_exception_message());
                    continue;
                }
                requests.push_back(request);
            }
        }
    } catch (...) {
        log("parse-error", InventoryLogLevel::Error,
            "[FETCH INV DESC]: fail parsing request: " + inventory_exception_message());
        return response;
    }
    if (requests.empty()) {
        if (bad.empty())
            return response;
        std::string warning = "[WEB FETCH INV DESC HANDLER]: Unable to fetch folders owned by Unknown user:";
        int limit = 5, count = 0;
        for (auto id : bad) {
            if (cache->contains(id))
                continue;
            warning += ' ';
            warning += id.str();
            ++count;
            if (--limit < 0)
                break;
        }
        if (count) {
            if (limit < 0)
                warning += " ...";
            log("bad-folders", InventoryLogLevel::Warn, warning);
        }
        response.body = "<llsd><map><key>folders</key><array /></map>";
        bad_xml(response.body, bad);
        response.body += "</llsd>";
        return response;
    }
    std::vector<Request> ordinary, library;
    std::vector<Uuid> ids;
    std::set<Uuid> seen, lib_seen;
    std::vector<Collection> result;
    bool zero = false;
    for (const auto &request : requests) {
        if (request.id.is_zero()) {
            if (!zero) {
                zero = true;
                Collection c;
                c.value.owner = request.owner;
                c.value.folder = request.id;
                result.push_back(c);
            }
            continue;
        }
        if (backend_.library_owner && request.owner == *backend_.library_owner) {
            if (lib_seen.insert(request.id).second)
                library.push_back(request);
            continue;
        }
        if (seen.insert(request.id).second) {
            ordinary.push_back(request);
            ids.push_back(request.id);
        }
    }
    auto bad_folder = [&](const Request &request, std::optional<InventoryCollection> &collection) {
        if (!collection) {
            bad.push_back(request.id);
            return true;
        }
        if (collection->folder.is_zero()) {
            auto folder = backend_.folder(request.owner, request.id);
            if (!folder) {
                bad.push_back(request.id);
                return true;
            }
            collection->folder = folder->id;
            collection->owner = folder->owner;
            collection->version = folder->version;
        }
        return false;
    };
    bool null_result = false;
    if (!ordinary.empty()) {
        auto contents = backend_.multiple_content(ordinary.front().owner, ids);
        if (!contents)
            null_result = true;
        else if (contents->empty()) {
            for (const auto &request : ordinary) {
                std::optional<InventoryCollection> none;
                bad_folder(request, none);
            }
        } else {
            std::size_t i = 0;
            for (auto &content : *contents) {
                const auto &request = ordinary.at(i++);
                if (bad_folder(request, content))
                    continue;
                if (!request.folders)
                    content->folders.clear();
                if (!request.items)
                    content->items.clear();
                Collection collection;
                collection.descendants = static_cast<int>(content->folders.size() + content->items.size());
                std::vector<Uuid> links;
                for (const auto &item : content->items)
                    if (item.asset_type == 24)
                        links.push_back(item.asset_id);
                if (!links.empty()) {
                    auto linked = backend_.multiple_items(request.owner, links);
                    if (linked) {
                        std::vector<InventoryItem> prefix;
                        for (auto &item : *linked)
                            if (item && item->asset_type != 24)
                                prefix.push_back(std::move(*item));
                        content->items.insert(content->items.begin(), prefix.begin(), prefix.end());
                    }
                }
                collection.value = std::move(*content);
                result.push_back(std::move(collection));
            }
        }
    }
    if (null_result)
        result.clear();
    else
        for (const auto &request : library) {
            auto content = backend_.library_content(request.id);
            if (content) {
                Collection collection;
                content->owner = *backend_.library_owner;
                content->folder = request.id;
                collection.descendants = static_cast<int>(content->folders.size() + content->items.size());
                collection.value = std::move(*content);
                result.push_back(std::move(collection));
            }
        }
    response.body = "<llsd><map><key>folders</key>";
    if (result.empty())
        response.body += "<array />";
    else {
        response.body += "<array>";
        for (const auto &collection : result)
            collection_xml(response.body, collection);
        response.body += "</array>";
    }
    response.body += "</map>";
    if (!bad.empty()) {
        for (auto id : bad)
            cache->add(id);
        bad_xml(response.body, bad);
        std::string warning = "[WEB FETCH INV DESC HANDLER]: Unable to fetch folders owned by " +
                              requests.front().owner.str() + " :";
        int limit = 9;
        for (auto id : bad) {
            warning += ' ';
            warning += id.str();
            if (--limit < 0)
                break;
        }
        if (limit < 0)
            warning += " ...";
        log("bad-folders", InventoryLogLevel::Warn, warning);
    }
    response.body += "</llsd>";
    return response;
}
}
