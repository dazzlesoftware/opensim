// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_copy.hpp"
#include "opensim/scene/user_inventory_operations.hpp"
#include <bit>
#include <iostream>
using namespace opensim;
using namespace scene;
namespace {
framework::Uuid id(int n) {
    auto s = std::to_string(n);
    return framework::Uuid("11223344-5566-7788-99aa-" + std::string(12 - s.size(), '0') + s);
}
std::string hex(const std::optional<std::string> &s) {
    if (!s)
        return "NULL";
    std::string out;
    for (unsigned char c : *s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string dump(InventoryCopyItem p, bool fresh = false) {
    if (!p)
        return "NULL";
    const auto &i = p->item;
    auto u = [](int n) { return std::to_string(static_cast<std::uint32_t>(n)); };
    return (fresh ? std::string(i.id.is_zero() ? "ZERO" : "NEW") : i.id.str()) + ":" + i.owner.str() + ":" +
           i.asset_id.str() + ":" + hex(i.creator_id) + ":" + hex(p->creator_data) + ":" + hex(i.name) + ":" +
           hex(i.description) + ":" + u(i.base_permissions) + "," + u(i.current_permissions) + "," +
           u(i.everyone_permissions) + "," + u(i.next_permissions) + "," + u(i.group_permissions) + "," +
           u(i.flags) + "," + std::to_string(i.asset_type) + "," + std::to_string(i.inventory_type) + "," +
           std::to_string(i.creation_date) + "," + i.group_id.str() + "," + std::to_string(i.group_owned) +
           "," + std::to_string(i.sale_price) + "," + std::to_string(i.sale_type) + "," + i.parent.str();
}
std::string probe(int kind, int mode, int value, int failure) {
    auto bit = [&](int m) { return (mode & m) != 0; };
    std::string trace;
    auto step = [&](int n, std::string s) {
        trace += '|' + s;
        if (n == failure)
            throw std::runtime_error("injected");
    };
    auto source = std::make_shared<TaskInventoryAgentItem>();
    auto &i = source->item;
    i.id = id(2);
    i.owner = id(bit(4194304) ? 9 : 1);
    i.asset_id = id(4);
    i.creator_id = id(3).str();
    source->creator_data =
        bit(65536) ? std::optional<std::string>{} : std::optional<std::string>("https://creator;Creator");
    i.name = bit(32768) ? "脚本 😃" : "Stored script";
    i.description = bit(32768) ? "説明 é" : "Description";
    i.base_permissions = std::bit_cast<std::int32_t>(0xf00fffffu);
    i.current_permissions = std::bit_cast<std::int32_t>((bit(16) ? 0xffff7fffu : 0xffffffffu) &
                                                        (bit(32) ? 0xffffdfffu : 0xffffffffu));
    i.everyone_permissions = 0x8a000;
    i.next_permissions = value;
    i.group_permissions = 0x84000;
    i.flags = 0x12345678;
    i.asset_type = value;
    i.inventory_type = bit(4096) ? 96 : 10;
    i.creation_date = 222;
    i.group_id = id(5);
    i.group_owned = 1;
    i.sale_price = 99;
    i.sale_type = 2;
    i.parent = id(6);
    InventoryCopyBackend b;
    b.can_create_user = [&](auto type, auto agent) {
        step(12, "CREATEPERM:" + std::to_string(type) + ":" + agent.str());
        return !bit(1);
    };
    b.has_presence = [&](auto agent) {
        step(13, "PRESENCE:" + agent.str());
        return !bit(2);
    };
    b.library = [&]() -> std::optional<InventoryCopyLibrary> {
        if (bit(1) || bit(2))
            return {};
        return InventoryCopyLibrary{id(7), [&](auto key) {
                                        step(2, "LIBRARY:" + key.str());
                                        return bit(4) ? InventoryCopyItem{} : source;
                                    }};
    };
    b.item = [&](auto agent, auto key) {
        step(3, "USER:" + agent.str() + ":" + key.str());
        return bit(8) ? InventoryCopyItem{} : source;
    };
    b.bypass_permissions = [&] {
        step(9, "BYPASS");
        if (bit(524288)) {
            source->item.name = "Mutated";
            source->item.next_permissions = 0x4000;
            source->item.flags = 7;
        }
        return bit(512);
    };
    b.can_copy_user = [&](auto agent, auto key) {
        step(10, "PERMISSION:" + agent.str() + ":" + key.str());
        return !bit(1024);
    };
    b.unix_time = [&] {
        step(8, "TIME");
        if (bit(1048576)) {
            source->item.flags = 0;
            source->item.asset_id = id(9);
        }
        return 111;
    };
    b.random_uuid = [] { return id(8); };
    b.add = [&](auto item) {
        step(4, "ADD:" + dump(item, true));
        if (bit(262144)) {
            item->item.parent = id(9);
            item->item.flags = 1;
        }
        return !bit(2048);
    };
    b.create_update = [&](auto, auto item, auto callback) {
        step(5, "REPLY:" + std::to_string(callback) + ":" + dump(item, true));
    };
    b.alert = [&](auto, const auto &text) { step(6, "ALERT:" + text); };
    b.warn = [&](auto) { step(7, "WARN"); };
    b.error = [&](auto) { step(11, "ERROR"); };
    b.debug = [&](auto) { step(1, "DEBUG"); };
    std::string result = "OK";
    try {
        InventoryCopyController controller(std::move(b));
        if (kind == 0)
            controller.copy(id(1), static_cast<std::uint32_t>(value),
                            bit(256)   ? framework::Uuid{}
                            : bit(128) ? id(7)
                            : bit(64)  ? id(9)
                                       : id(1),
                            id(2), id(6),
                            bit(8192) ? std::optional<std::string>{}
                                      : std::optional<std::string>(bit(16384)   ? ""
                                                                   : bit(32768) ? "新しい 😃"
                                                                                : "Copied script"));
        else if (kind == 1 || kind == 2)
            controller.create(id(1), static_cast<std::uint32_t>(value), *source);
        else if (kind == 3)
            animation_set_create_permissions(source.get());
        else if (kind == 4 || kind == 5)
            animation_set_enforce_permissions(source.get(), kind == 4);
        else if (kind == 7)
            controller.link(id(1), static_cast<std::uint32_t>(value), id(6), id(2),
                            bit(8192)    ? std::optional<std::string>{}
                            : bit(16384) ? std::optional<std::string>("")
                                         : std::optional<std::string>(source->item.name),
                            bit(65536) ? std::optional<std::string>{}
                                       : std::optional<std::string>(source->item.description),
                            static_cast<std::int8_t>(source->item.inventory_type),
                            std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value)));
        else {
            animation_set_create_permissions(nullptr);
            animation_set_enforce_permissions(nullptr, true);
        }
    } catch (...) {
        result = "THROW";
    }
    return result + ":" + dump(source) + trace;
}
}
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int k, m, v, f;
            while (std::cin >> k >> m >> v >> f)
                std::cout << probe(k, m, v, f) << '\n';
            return 0;
        }
        int n = 0;
        auto check = [&](bool v, const char *s) {
            ++n;
            if (!v)
                throw std::runtime_error(s);
        };
        check(probe(0, 16, 10, 0).find("|ADD:") != std::string::npos, "library skips copy permission");
        check(probe(0, 1 | 16, 10, 0).find("|ADD:") == std::string::npos, "user item needs copy");
        check(probe(0, 64 | 32, 10, 0).find("|BYPASS") == std::string::npos, "transfer before bypass");
        check(probe(0, 64 | 512 | 1024, 10, 0).find("|PERMISSION:") == std::string::npos,
              "bypass short circuits copy policy");
        check(probe(0, 2048, 10, 0).find("|ALERT:Failed to create item") != std::string::npos,
              "add failure alerts");
        check(probe(0, 2048, 10, 0).find("|REPLY:") == std::string::npos, "add failure has no create reply");
        check(probe(7, 0, 24, 0).find("|LIBRARY") == std::string::npos &&
                  probe(7, 0, 24, 0).find("|USER") == std::string::npos,
              "link never looks up target");
        check(probe(7, 1, 24, 0).find("|PRESENCE") == std::string::npos, "link permission before presence");
        check(probe(7, 2, 24, 0).find("|TIME") == std::string::npos, "link missing presence before clock");
        std::cout << "PASS: " << n << " inventory copy/create checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
