// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "legacy_inventory_probe.hpp"
#include "opensim/network/osd_json.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
namespace opensim::tests {
namespace {
using framework::Uuid;
using network::lludp::Packet;
using network::lludp::Block;
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error("Legacy inventory UDP probe: " + message);
}
bool named(const Packet& p, const char* name) {
    return p.spec && p.spec->name == name;
}
Uuid uuid(const Block& block, const char* name) {
    return std::get<Uuid>(block.at(name));
}
std::int64_t integer(const Block& block, const char* name) {
    const auto& value = block.at(name);
    if (auto p = std::get_if<std::int64_t>(&value))
        return *p;
    return static_cast<std::int64_t>(std::get<std::uint64_t>(value));
}
std::string text(const Block& block, const char* name) {
    const auto& bytes = std::get<std::vector<std::uint8_t>>(block.at(name));
    return {bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)};
}
struct Window {
    std::size_t start = 0;
    std::set<std::uint32_t> previous;
};
Window mark(const LegacyInventoryProbeIo& io, const char* name) {
    auto packets = io.snapshot();
    Window window;
    window.start = packets.size();
    for (const auto& p : packets)
        if (named(p, name))
            window.previous.insert(p.sequence);
    return window;
}
std::vector<const Packet*> selected(const std::vector<Packet>& packets, const Window& window,
                                    const char* name) {
    std::vector<const Packet*> result;
    auto seen = window.previous;
    for (auto n = window.start; n < packets.size(); ++n)
        if (named(packets[n], name) && seen.insert(packets[n].sequence).second)
            result.push_back(&packets[n]);
    return result;
}
std::set<Uuid> ids(const network::Value& metadata, const char* name) {
    const auto& values = metadata.get(name).view();
    require(values.kind == network::Value::Kind::Array, std::string(name) + " must be an array");
    std::set<Uuid> result;
    for (const auto& value : values.array) {
        auto id = network::osd_uuid(value);
        require(!id.is_zero(), std::string(name) + " contains an invalid UUID");
        result.insert(id);
    }
    return result;
}
Uuid required_id(const network::Value& metadata, const char* name) {
    require(metadata.contains(name), std::string("missing metadata ") + name);
    auto id = network::osd_uuid(metadata.get(name));
    require(!id.is_zero(), std::string("invalid metadata UUID ") + name);
    return id;
}
std::set<Uuid> folder_ids(const std::vector<Packet>& packets, const Window& window, Uuid folder) {
    std::set<Uuid> result;
    for (auto p : selected(packets, window, "InventoryDescendents")) {
        if (uuid(p->block("AgentData"), "FolderID") != folder)
            continue;
        for (const auto& f : p->blocks.at("FolderData")) {
            auto id = uuid(f, "FolderID");
            if (!id.is_zero())
                result.insert(id);
        }
    }
    return result;
}
std::set<Uuid> item_ids(const std::vector<Packet>& packets, const Window& window, Uuid folder) {
    std::set<Uuid> result;
    for (auto p : selected(packets, window, "InventoryDescendents")) {
        if (uuid(p->block("AgentData"), "FolderID") != folder)
            continue;
        for (const auto& item : p->blocks.at("ItemData")) {
            auto id = uuid(item, "ItemID");
            if (!id.is_zero())
                result.insert(id);
        }
    }
    return result;
}
}
void run_legacy_inventory_probe(Uuid agent, const std::filesystem::path& path,
                                const LegacyInventoryProbeIo& io) {
    require(io.make && io.send && io.wait && io.snapshot, "missing I/O callback");
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "cannot open metadata");
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    require(!file.bad(), "cannot read metadata");
    auto meta = network::parse_osd_json(bytes);
    require(meta.view().kind == network::Value::Kind::Map, "metadata must be a map");
    for (const auto& packet : io.snapshot())
        require(!named(packet, "AgentMovementComplete"), "helper must run before root transition");
    require(meta.contains("version") && meta.contains("library_version"), "missing folder version metadata");
    const auto item = required_id(meta, "item_id"), asset = required_id(meta, "asset_id"),
               parent = required_id(meta, "parent"), owner = required_id(meta, "owner"),
               target = required_id(meta, "linked_item"), link = required_id(meta, "link_id"),
               library = required_id(meta, "library_folder"),
               library_owner = required_id(meta, "library_owner");
    const auto categories = ids(meta, "categories"), library_categories = ids(meta, "library_categories");
    const auto version = network::osd_integer(meta.get("version")),
               library_version = network::osd_integer(meta.get("library_version"));
    const auto name = meta.contains("item_name") ? network::osd_string(meta.get("item_name"))
                                                 : std::string("Viewer <item> & text");
    const auto flags = meta.contains("item_flags") ? network::osd_unsigned(meta.get("item_flags")) : 0x1234u;
    auto fetch_items = [&](Uuid key, int count = 1) {
        auto packet = io.make("FetchInventory");
        packet.blocks["InventoryData"].clear();
        for (int n = 0; n < count; ++n) {
            auto block = network::lludp::make_block(packet.spec->blocks.at(1));
            block["ItemID"] = key;
            block["OwnerID"] = owner;
            packet.blocks["InventoryData"].push_back(std::move(block));
        }
        io.send(packet);
    };
    auto fetch_folder = [&](Uuid folder, Uuid wire_owner, bool fetch_folders, bool fetch_items) {
        auto packet = io.make("FetchInventoryDescendents");
        auto& b = packet.block("InventoryData");
        b["FolderID"] = folder;
        b["OwnerID"] = wire_owner;
        b["FetchFolders"] = fetch_folders;
        b["FetchItems"] = fetch_items;
        b["SortOrder"] = std::int64_t(99);
        io.send(packet);
    };
    auto valid_item = [&] {
        auto window = mark(io, "FetchInventoryReply");
        fetch_items(item);
        io.wait([&](const auto& packets) {
            for (auto p : selected(packets, window, "FetchInventoryReply"))
                for (const auto& b : p->blocks.at("InventoryData"))
                    if (uuid(b, "ItemID") == item)
                        return true;
            return false;
        });
        auto packets = io.snapshot();
        bool found = false;
        for (auto p : selected(packets, window, "FetchInventoryReply")) {
            require(uuid(p->block("AgentData"), "AgentID") == agent, "item reply agent mismatch");
            for (const auto& b : p->blocks.at("InventoryData")) {
                require(uuid(b, "ItemID") == item, "single-item request returned unexpected item");
                require(uuid(b, "AssetID") == asset && uuid(b, "FolderID") == parent &&
                            uuid(b, "OwnerID") == owner,
                        "item reply identity mismatch");
                require(text(b, "Name") == name && static_cast<std::uint32_t>(integer(b, "Flags")) == flags,
                        "item name/full flags mismatch");
                found = true;
            }
        }
        require(found, "item reply absent");
    };
    valid_item();
    auto category_window = mark(io, "InventoryDescendents");
    fetch_folder(parent, owner, true, false);
    io.wait([&](const auto& packets) {
        auto actual = folder_ids(packets, category_window, parent);
        if (actual != categories)
            return false;
        for (auto p : selected(packets, category_window, "InventoryDescendents"))
            if (uuid(p->block("AgentData"), "FolderID") == parent)
                return true;
        return false;
    });
    auto packets = io.snapshot();
    require(folder_ids(packets, category_window, parent) == categories, "ordinary child folder IDs mismatch");
    for (auto p : selected(packets, category_window, "InventoryDescendents")) {
        const auto& a = p->block("AgentData");
        if (uuid(a, "FolderID") != parent)
            continue;
        require(uuid(a, "AgentID") == agent && uuid(a, "OwnerID") == owner,
                "ordinary folder agent/owner mismatch");
        require(integer(a, "Version") == version &&
                    integer(a, "Descendents") == static_cast<std::int64_t>(categories.size() + 2),
                "ordinary full count/version mismatch");
        for (const auto& b : p->blocks.at("ItemData"))
            require(uuid(b, "ItemID").is_zero(), "category-only response leaked items");
    }
    auto item_window = mark(io, "InventoryDescendents");
    fetch_folder(parent, owner, false, true);
    const std::set<Uuid> wanted{item, link, target};
    io.wait([&](const auto& list) { return item_ids(list, item_window, parent) == wanted; });
    packets = io.snapshot();
    require(item_ids(packets, item_window, parent) == wanted, "linked item expansion mismatch");
    bool saw_item = false;
    for (auto p : selected(packets, item_window, "InventoryDescendents")) {
        const auto& a = p->block("AgentData");
        if (uuid(a, "FolderID") != parent)
            continue;
        bool has_items = false;
        for (const auto& b : p->blocks.at("ItemData"))
            if (!uuid(b, "ItemID").is_zero())
                has_items = true;
        if (!has_items)
            continue;
        require(uuid(a, "AgentID") == agent && uuid(a, "OwnerID") == owner &&
                    integer(a, "Version") == version,
                "item-only folder identity/version mismatch");
        require(integer(a, "Descendents") == static_cast<std::int64_t>(categories.size() + 2),
                "item-only count must include all original categories/items");
        for (const auto& b : p->blocks.at("FolderData"))
            require(uuid(b, "FolderID").is_zero(), "item-only response leaked categories");
        for (const auto& b : p->blocks.at("ItemData"))
            if (uuid(b, "ItemID") == item) {
                require(integer(b, "Flags") == static_cast<std::int64_t>(flags & 0x2000ffu),
                        "folder item flags source mask mismatch");
                saw_item = true;
            }
    }
    require(saw_item, "primary folder item absent");
    auto library_window = mark(io, "InventoryDescendents");
    Uuid wrong_owner = agent == library_owner ? Uuid("00000000-0000-0000-0000-000000000001") : agent;
    fetch_folder(library, wrong_owner, true, false);
    io.wait([&](const auto& list) {
        if (folder_ids(list, library_window, library) != library_categories)
            return false;
        for (auto p : selected(list, library_window, "InventoryDescendents"))
            if (uuid(p->block("AgentData"), "FolderID") == library)
                return true;
        return false;
    });
    packets = io.snapshot();
    require(folder_ids(packets, library_window, library) == library_categories, "library child IDs mismatch");
    for (auto p : selected(packets, library_window, "InventoryDescendents")) {
        const auto& a = p->block("AgentData");
        if (uuid(a, "FolderID") != library)
            continue;
        require(uuid(a, "AgentID") == agent && uuid(a, "OwnerID") == library_owner &&
                    integer(a, "Version") == library_version,
                "library lookup must ignore wire owner and return actual owner/version");
        require(integer(a, "Descendents") >= static_cast<std::int64_t>(library_categories.size()),
                "library full count below category count");
    }
    auto no_folder_reply = [&](Uuid folder) {
        auto window = mark(io, "InventoryDescendents");
        fetch_folder(folder, owner, true, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto list = io.snapshot();
        for (auto p : selected(list, window, "InventoryDescendents"))
            require(uuid(p->block("AgentData"), "FolderID") != folder,
                    "zero/missing folder unexpectedly produced reply");
        valid_item();
    };
    no_folder_reply(Uuid{});
    no_folder_reply(Uuid("00000000-0000-0000-0000-00000000f001"));
    auto missing_window = mark(io, "FetchInventoryReply");
    fetch_items(Uuid("00000000-0000-0000-0000-00000000f002"));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    require(selected(io.snapshot(), missing_window, "FetchInventoryReply").empty(),
            "missing item unexpectedly produced reply");
    valid_item();
    if (meta.contains("verify_partial_batch") && network::osd_boolean(meta.get("verify_partial_batch"))) {
        auto window = mark(io, "FetchInventoryReply");
        fetch_items(item, 12);
        auto count = [&](const auto& list) {
            std::size_t n = 0;
            for (auto p : selected(list, window, "FetchInventoryReply"))
                n += p->blocks.at("InventoryData").size();
            return n;
        };
        io.wait([&](const auto& list) { return count(list) >= 10; });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        packets = io.snapshot();
        require(count(packets) == 10, "source12-item send must emit exactly valid10-item prefix");
        for (auto p : selected(packets, window, "FetchInventoryReply"))
            for (const auto& b : p->blocks.at("InventoryData"))
                require(uuid(b, "ItemID") == item, "partial-batch item ID mismatch");
        valid_item();
    }
    for (const auto& packet : io.snapshot())
        require(!named(packet, "AgentMovementComplete"), "root transition occurred during legacy checks");
    std::cout
        << "Verified before-root legacy inventory UDP items, category/items folders, links, library routing and missing/zero requests\n";
}
}
