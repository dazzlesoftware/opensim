// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_packets.hpp"
#include <iostream>
using namespace opensim;
using namespace scene;
using namespace services;
namespace {
framework::Uuid id(int n) {
    auto s = std::to_string(n);
    return framework::Uuid("11223344-5566-7788-99aa-" + std::string(12 - s.size(), '0') + s);
}
InventoryItem sample(int n, int seed, int mode) {
    InventoryItem i;
    i.id = id(100 + n);
    i.parent = id(2);
    i.asset_id = id(300 + n);
    i.owner = id(3);
    i.group_id = id(4);
    i.creator_id = id(5).str();
    i.base_permissions = -1;
    i.current_permissions = 0x7fffffff - seed;
    i.next_permissions = -seed;
    i.everyone_permissions = seed * 19;
    i.group_permissions = -19;
    i.flags = -1 - seed;
    i.inventory_type = seed % 2 ? 257 : -129;
    i.asset_type = seed % 2 ? -129 : 257;
    i.group_owned = seed % 2;
    i.creation_date = -1000 - seed;
    i.sale_price = -seed * 11;
    i.sale_type = seed % 2 ? 257 : -1;
    i.name = "name" + std::to_string(n);
    i.description = "description";
    if (mode & 1)
        i.name = std::string(300, 'x');
    if (mode & 2) {
        i.name = std::string(252, 'x') + "雪🙂";
        i.description = "é雪🙂";
    }
    if (mode & 4) {
        i.name = std::string("a\0b", 3);
        i.description = std::string("\0trailing", 9);
    }
    if (mode & 8)
        i.description = "\xe2\x80\x83\t";
    return i;
}
std::string hex(std::span<const std::uint8_t> bytes) {
    std::string out;
    constexpr char d[] = "0123456789abcdef";
    for (auto b : bytes) {
        out += d[b >> 4];
        out += d[b & 15];
    }
    return out;
}
std::string probe(int kind, int seed, int ni, int nf, int mode, int flags) {
    std::vector<std::optional<InventoryItem>> items;
    std::vector<std::optional<InventoryFolder>> folders;
    for (int n = 0; n < ni; ++n)
        items.push_back((mode & 16) && n == ni - 1 ? std::nullopt : std::optional{sample(n, seed, mode)});
    for (int n = 0; n < nf; ++n) {
        InventoryFolder f;
        f.id = id(600 + n);
        f.parent = id(2);
        f.type = n + 129;
        f.name = (mode & 1) ? std::string(300, 'f') : "folder雪" + std::to_string(n);
        folders.push_back((mode & 32) && n == nf - 1 ? std::nullopt : std::optional{f});
    }
    std::string emitted;
    auto sink = [&](network::lludp::Packet packet, bool split) {
        emitted += '|';
        emitted += split ? '1' : '0';
        emitted += ':';
        emitted += hex(network::lludp::encode_packet(packet));
        emitted += ':';
        auto datagrams = split
                             ? network::lludp::encode_packets(packet)
                             : std::vector<std::vector<std::uint8_t>>{network::lludp::encode_packet(packet)};
        for (std::size_t n = 0; n < datagrams.size(); ++n) {
            if (n)
                emitted += ',';
            emitted += hex(network::lludp::zero_encode(datagrams[n]));
        }
    };
    std::string result = "OK";
    try {
        if (kind == 0)
            inventory_item_details(id(1), items, sink);
        else
            inventory_folder_details(id(1), id(3), id(2), items, folders, -seed, ni + nf, flags & 1,
                                     flags & 2, sink);
    } catch (const InventoryPacketNullError &) {
        result = "NULL";
    } catch (const std::exception &) {
        result = "ERROR";
    }
    return result + emitted;
}
int checks = 0;
void check(bool value, const char *label) {
    ++checks;
    if (!value)
        throw std::runtime_error(label);
}
void run() {
    check(probe(0, 0, 0, 0, 0, 3) == "OK", "empty item details emits nothing");
    check(probe(0, 1, 1, 0, 0, 3).starts_with("OK|1:"), "single item uses source automatic splitting");
    check(probe(0, 1, 2, 0, 0, 3) == "NULL", "source final batch null blocks preserved");
    check(probe(0, 1, 11, 0, 0, 3).starts_with("OK|1:"), "source eleven item complete batches");
    check(probe(0, 1, 12, 0, 0, 3).starts_with("NULL|1:"),
          "source partial emission before invalid remainder");
    check(probe(1, 1, 0, 0, 0, 3).starts_with("OK|0:"), "empty folder sentinel packet");
    check(probe(1, 1, 6, 7, 0, 0).starts_with("OK|0:"), "both fetch flags false sends empty sentinel");
    check(probe(1, 1, 6, 7, 0, 3).starts_with("OK|0:"), "folder batching uses manual splitting");
}
}
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--probe") {
            int k, s, i, f, m, b;
            while (std::cin >> k >> s >> i >> f >> m >> b)
                std::cout << probe(k, s, i, f, m, b) << '\n';
            return 0;
        }
        run();
        std::cout << "PASS: " << checks << " inventory packet checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
