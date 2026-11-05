// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/library_descendents.hpp"
#include "opensim/network/osd_value.hpp"
#include <set>
#include <stdexcept>
namespace opensim::services {
namespace {
using framework::Uuid;
constexpr auto empty = "<llsd><map><key>folders</key><array /></map></llsd>";
struct Request {
    Uuid id, owner;
};
struct Collection {
    InventoryCollection value;
    int descendants = 0;
};
void key(std::string& out, std::string_view name) {
    out += "<key>";
    out += name;
    out += "</key>";
}
void number(std::string& out, std::string_view name, int value) {
    key(out, name);
    out += value ? "<integer>" + std::to_string(value) + "</integer>" : "<integer />";
}
void uuid(std::string& out, std::string_view name, Uuid value) {
    key(out, name);
    out += value.is_zero() ? "<uuid />" : "<uuid>" + value.str() + "</uuid>";
}
std::string escaped(std::string_view value) {
    std::string out;
    for (char c : value)
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
void bad_xml(std::string& out, const std::vector<Uuid>& bad) {
    out += "<map><key>bad_folders</key><array>";
    for (auto id : bad)
        out += "<map><key>folder_id</key><uuid>" + id.str() +
               "</uuid><key>error</key><string>Unknown</string></map>";
    out += "</array></map>";
}
void collection_xml(std::string& out, const Collection& collection, Uuid agent) {
    const auto& c = collection.value;
    out += "<map>";
    uuid(out, "folder_id", c.folder);
    uuid(out, "agent_id", agent);
    uuid(out, "owner_id", c.owner);
    number(out, "descendents", collection.descendants);
    number(out, "version", c.version);
    key(out, "categories");
    if (c.folders.empty())
        out += "<array />";
    else {
        out += "<array>";
        for (const auto& folder : c.folders) {
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
        for (const auto& item : c.items)
            out += inventory_item_llsd(item);
        out += "</array>";
    }
    out += "</map>";
}
}
FetchLibraryDescendentsCore::FetchLibraryDescendentsCore(LibraryDescendentsBackend backend)
    : backend_(std::move(backend)) {
    if (!backend_.root_owner || !backend_.folder_content || !backend_.deserialize_xml)
        throw std::invalid_argument("Null library descendents backend");
    auto owner = backend_.root_owner();
    if (!owner)
        throw std::invalid_argument("Library descendents requires actual library root");
    initial_owner_ = *owner;
}
network::HttpResponse FetchLibraryDescendentsCore::handle(const network::HttpRequest& incoming,
                                                          InventoryBadRequests* cache, Uuid agent) const {
    network::HttpResponse response;
    response.content_type = "text/html;charset=UTF-8";
    if (!backend_.root_owner()) {
        response.status = 503;
        return response;
    }
    response.body = empty;
    auto log = [&](const char* category, InventoryLogLevel level, const std::string& message) {
        if (backend_.diagnostic)
            backend_.diagnostic(level, message);
        else if (backend_.log)
            backend_.log(category);
    };
    std::vector<Request> requests;
    std::vector<Uuid> bad;
    try {
        auto decoded = backend_.deserialize_xml(incoming.body);
        const auto& map = decoded.view();
        if (map.kind != network::Value::Kind::Map)
            throw std::invalid_argument(inventory_cast_message(map.kind));
        const auto& raw_values = map.get("folders");
        if (raw_values.kind == network::Value::Kind::NullReference)
            return response;
        const auto& values = raw_values.view();
        if (values.kind != network::Value::Kind::Array || values.array.empty())
            return response;
        for (const auto& value : values.array) {
            const auto& m = value.view();
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
                    (void)network::osd_integer(m.get("sort_order"));
                    (void)network::osd_boolean(m.get("fetch_folders"));
                    (void)network::osd_boolean(m.get("fetch_items"));
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
            "[FETCH LIB DESC]: fail parsing request: " + inventory_exception_message());
        return response;
    }
    if (requests.empty()) {
        if (bad.empty())
            return response;
        std::string warning = "[FETCH LIB DESC HANDLER]: Unable to fetch folders:";
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
    std::vector<Request> library;
    std::set<Uuid> seen;
    bool zero = false;
    std::vector<Collection> result;
    for (const auto& request : requests) {
        if (request.id.is_zero()) {
            if (!zero) {
                zero = true;
                Collection c;
                c.value.owner = request.owner;
                c.value.version = -1;
                result.push_back(c);
            }
            continue;
        }
        if (request.owner == initial_owner_ && seen.insert(request.id).second)
            library.push_back(request);
    }
    for (const auto& request : library) {
        if (!backend_.root_owner())
            throw std::runtime_error("Library root disappeared before FindFolder");
        auto content = backend_.folder_content(request.id);
        if (!content) {
            bad.push_back(request.id);
            continue;
        }
        auto owner = backend_.root_owner();
        if (!owner)
            throw std::runtime_error("Library root disappeared before owner access");
        content->owner = *owner;
        content->folder = request.id;
        Collection c;
        c.descendants = static_cast<int>(content->folders.size() + content->items.size());
        c.value = std::move(*content);
        result.push_back(std::move(c));
    }
    response.body = "<llsd><map><key>folders</key>";
    if (result.empty())
        response.body += "<array />";
    else {
        response.body += "<array>";
        for (const auto& collection : result)
            collection_xml(response.body, collection, agent);
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
