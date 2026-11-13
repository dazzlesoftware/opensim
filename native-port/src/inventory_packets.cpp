// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_packets.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <set>
namespace opensim::scene {
namespace {
using namespace network::lludp;
using framework::Uuid;
using services::InventoryItem;
std::int64_t sbyte(int n) {
    return std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(n));
}
std::uint32_t crc(Uuid id) {
    const auto &b = id.bytes();
    auto le = [&](int i) {
        return std::uint32_t(b[i]) | (std::uint32_t(b[i + 1]) << 8) | (std::uint32_t(b[i + 2]) << 16) |
               (std::uint32_t(b[i + 3]) << 24);
    };
    return (std::uint32_t(b[0]) << 24) + (std::uint32_t(b[1]) << 16) + (std::uint32_t(b[2]) << 8) + b[3] +
           (std::uint32_t(b[4]) << 8) + b[5] + (std::uint32_t(b[6]) << 24) + (std::uint32_t(b[7]) << 16) +
           le(8) + le(12);
}
std::vector<std::uint8_t> text(std::string_view value) {
    auto nul = value.find('\0');
    if (nul != value.npos)
        value = value.substr(0, nul);
    auto size = std::min<std::size_t>(value.size(), 254);
    if (size < value.size())
        while (size && (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80)
            --size;
    if (!size)
        return {};
    std::vector<std::uint8_t> out(value.begin(), value.begin() + size);
    out.push_back(0);
    return out;
}
Block block(const Packet &packet, const char *name) {
    for (const auto &spec : packet.spec->blocks)
        if (spec.name == name)
            return make_block(spec);
    throw std::invalid_argument("Missing inventory block schema");
}
Block item_block(const Packet &packet, const char *group, const InventoryItem &item, bool fetch,
                 bool bulk = false) {
    auto b = block(packet, group);
    Uuid creator;
    framework::try_parse_sdk_uuid(item.creator_id.substr(0, item.creator_id.find(';')), creator);
    b["ItemID"] = item.id;
    b["AssetID"] = item.asset_id;
    b["CreatorID"] = creator;
    b["FolderID"] = item.parent;
    b["OwnerID"] = item.owner;
    b["GroupID"] = item.group_id;
    b["BaseMask"] = std::uint64_t(static_cast<std::uint32_t>(item.base_permissions));
    b["OwnerMask"] = std::uint64_t(static_cast<std::uint32_t>(item.current_permissions));
    b["EveryoneMask"] = std::uint64_t(static_cast<std::uint32_t>(item.everyone_permissions));
    b["GroupMask"] = std::uint64_t(static_cast<std::uint32_t>(item.group_permissions));
    b["NextOwnerMask"] = std::uint64_t(static_cast<std::uint32_t>(item.next_permissions));
    b["Name"] = text(item.name);
    b["Description"] =
        text(!bulk && framework::trim_utf8(item.description).empty() ? std::string{} : item.description);
    b["Type"] = sbyte(item.asset_type);
    b["InvType"] = sbyte(item.inventory_type);
    b["GroupOwned"] = item.group_owned != 0;
    b["CreationDate"] = std::int64_t(item.creation_date);
    b["SalePrice"] = std::int64_t(item.sale_price);
    b["SaleType"] = std::uint64_t(static_cast<std::uint8_t>(item.sale_type));
    auto flags = static_cast<std::uint32_t>(item.flags) & (fetch ? 0xffffffffu : 0x2000ffu);
    b["Flags"] = std::uint64_t(flags);
    std::uint32_t checksum = crc(item.asset_id) + crc(item.parent) + crc(item.id) + crc(creator) +
                             crc(item.owner) + crc(item.group_id);
    checksum += static_cast<std::uint32_t>(sbyte(item.asset_type)) +
                static_cast<std::uint32_t>(sbyte(item.inventory_type));
    if (bulk)
        checksum += 1000u + 100u + 1u + 4u * 0x8e000u;
    else if (fetch)
        checksum += 1000u + 100u + 1u + 4u * 0x7fffffffu;
    else
        checksum += static_cast<std::uint32_t>(item.current_permissions) +
                    static_cast<std::uint32_t>(item.next_permissions) +
                    static_cast<std::uint32_t>(item.everyone_permissions) +
                    static_cast<std::uint32_t>(item.group_permissions) + flags +
                    static_cast<std::uint32_t>(item.creation_date) +
                    static_cast<std::uint32_t>(item.sale_price) +
                    static_cast<std::uint32_t>(static_cast<std::uint8_t>(item.sale_type)) * 0x07073096u;
    b["CRC"] = std::uint64_t(checksum);
    return b;
}
}
void inventory_bulk_folder(Uuid agent, const services::InventoryFolder& folder,
                           const std::function<services::InventoryCollection(Uuid)>& content,
                           const InventoryPacketSink& emit, std::optional<Uuid> transaction) {
    auto packet = make_packet("BulkUpdateInventory");
    packet.zerocoded = true;
    packet.block("AgentData")["AgentID"] = agent;
    packet.block("AgentData")["TransactionID"] = transaction ? *transaction : Uuid::random();
    auto& folders = packet.blocks["FolderData"];
    auto& items = packet.blocks["ItemData"];
    const auto flush = [&] {
        emit(packet, true);
        folders.clear();
        items.clear();
    };
    std::set<Uuid> visited;
    const auto visit = [&](auto&& self, const services::InventoryFolder& current) -> void {
        if (!visited.insert(current.id).second)
            return;
        if (folders.size() == 255)
            flush();
        const Block row{{"FolderID", current.id}, {"ParentID", current.parent},
                        {"Type", sbyte(current.type)}, {"Name", text(current.name)}};
        folders.push_back(row);
        auto collection = content(current.id);
        while (!collection.items.empty()) {
            items.push_back(item_block(packet, "ItemData", collection.items.back(), false, true));
            collection.items.pop_back();
            if (items.size() == 5 || collection.items.empty()) {
                flush();
                if (!collection.items.empty())
                    folders.push_back(row);
            }
        }
        for (const auto& child : collection.folders)
            self(self, child);
    };
    visit(visit, folder);
    if (!folders.empty())
        flush();
}
network::lludp::Packet task_inventory_reply(Uuid part, std::int16_t serial,
                                            std::span<const std::uint8_t> filename) {
    auto p = make_packet("ReplyTaskInventory");
    p.reliable = true;
    p.zerocoded = false;
    auto &b = p.block("InventoryData");
    b["TaskID"] = part;
    b["Serial"] = std::int64_t(serial);
    auto count = static_cast<std::uint8_t>(filename.size());
    b["Filename"] = std::vector<std::uint8_t>(filename.begin(), filename.begin() + count);
    return p;
}
void inventory_item_details(Uuid agent, std::span<const std::optional<InventoryItem>> items,
                            const InventoryPacketSink &emit) {
    if (!emit)
        throw std::invalid_argument("Null inventory packet sink");
    auto packet = make_packet("FetchInventoryReply");
    packet.block("AgentData")["AgentID"] = agent;
    packet.zerocoded = true;
    auto total = items.size();
    std::size_t count = 0, expected = 0;
    for (const auto &item : items) {
        if (!count) {
            expected = std::min<std::size_t>(total, 10);
            total = total < 10 ? 0 : total - 10;
            packet.blocks["InventoryData"].clear();
        }
        if (!item)
            throw InventoryPacketNullError{};
        packet.blocks["InventoryData"].push_back(item_block(packet, "InventoryData", *item, true));
        ++count;
        if (count == 10 || total == 0) {
            // Source sets total=0 before filling the final array and calls OutPacket
            // after its first item. SDK packet.Length then dereferences null tail slots.
            if (count != expected)
                throw InventoryPacketNullError{};
            (void)packet_length(packet);
            emit(packet, true);
            if (!total)
                break;
            count = 0;
        }
    }
}
void inventory_folder_details(Uuid agent, Uuid owner, Uuid folder,
                              std::span<const std::optional<InventoryItem>> items,
                              std::span<const std::optional<services::InventoryFolder>> folders, int version,
                              int descendants, bool fetch_folders, bool fetch_items,
                              const InventoryPacketSink &emit) {
    if (!emit)
        throw std::invalid_argument("Null inventory packet sink");
    auto create = [&] {
        auto packet = make_packet("InventoryDescendents");
        packet.zerocoded = true;
        auto &a = packet.block("AgentData");
        a["AgentID"] = agent;
        a["OwnerID"] = owner;
        a["FolderID"] = folder;
        a["Version"] = std::int64_t(version);
        a["Descendents"] = std::int64_t(descendants);
        auto f = block(packet, "FolderData");
        f["Type"] = std::int64_t(-1);
        packet.blocks["FolderData"] = {f};
        auto i = block(packet, "ItemData");
        i["Type"] = std::int64_t(-1);
        packet.blocks["ItemData"] = {i};
        return packet;
    };
    auto nf = fetch_folders ? folders.size() : 0, ni = fetch_items ? items.size() : 0;
    if (!nf && !ni) {
        emit(create(), false);
        return;
    }
    for (std::size_t start = 0; start < nf; start += 6) {
        auto packet = create();
        packet.blocks["FolderData"].clear();
        for (auto end = std::min(start + 6, nf), i = start; i < end; ++i) {
            if (!folders[i])
                throw InventoryPacketNullError{};
            const auto &entry = *folders[i];
            auto f = block(packet, "FolderData");
            f["FolderID"] = entry.id;
            f["ParentID"] = entry.parent;
            f["Type"] = sbyte(entry.type);
            f["Name"] = text(entry.name);
            packet.blocks["FolderData"].push_back(std::move(f));
        }
        emit(std::move(packet), false);
    }
    for (std::size_t start = 0; start < ni; start += 5) {
        auto packet = create();
        packet.blocks["ItemData"].clear();
        for (auto end = std::min(start + 5, ni), i = start; i < end; ++i) {
            if (!items[i])
                throw InventoryPacketNullError{};
            packet.blocks["ItemData"].push_back(item_block(packet, "ItemData", *items[i], false));
        }
        emit(std::move(packet), false);
    }
}
}
