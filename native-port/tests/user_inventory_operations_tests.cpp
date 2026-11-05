// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
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
std::string hex(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string dump(UserInventoryItem i) {
    if (!i)
        return "NULL";
    auto u = [](int n) { return std::to_string(static_cast<std::uint32_t>(n)); };
    return hex(i->name) + ":" + hex(i->description) + ":" + u(i->base_permissions) + "," +
           u(i->current_permissions) + "," + u(i->next_permissions) + "," + u(i->everyone_permissions) + "," +
           u(i->group_permissions) + "," + u(i->flags) + "," + std::to_string(i->creation_date) + "," +
           std::to_string(i->inventory_type) + "," + std::to_string(i->sale_price) + "," +
           std::to_string(i->sale_type) + "," + std::to_string(i->group_owned) + "," + i->group_id.str() +
           "," + i->owner.str() + "," + i->parent.str() + "," + i->asset_id.str();
}
std::string probe(int kind, int mode, int text, int failure) {
    auto bit = [&](int b) { return (mode & b) != 0; };
    std::string trace;
    auto step = [&](int n, std::string s) {
        trace += '|' + s;
        if (n == failure)
            throw std::runtime_error("injected");
    };
    const std::string names[] = {"Name",       "HG Native", "HG ­Native", std::string("HG \0tail", 8),
                                 "HG Native­", "ＨＧ old",  "Plainé",     "HG 😃"};
    const std::string descriptions[] = {
        "original @ http://grid", "old @ h­ttp://grid", "😀 @ httone @ htttwo",          "old @ ＨＴＴgrid",
        "old ­@ httlink",         "description",        std::string("a\0@ httlink", 11), "old @ httend"};
    const std::string incoming[] = {
        "edited", "", "edited @ embedded", " \t　", std::string("a\0b", 3), "新しい", "😃", "@"};
    auto current = std::make_shared<services::InventoryItem>();
    current->id = id(2);
    current->owner = id(bit(2) ? 9 : 1);
    current->creator_id = id(bit(4096) ? 9 : 1).str();
    current->name = user_inventory_text(names[text & 7]);
    current->description = user_inventory_text(descriptions[text & 7], true);
    current->base_permissions = bit(8192) ? 0x96000 : 0x9e000;
    current->current_permissions = bit(16384) ? 0x8e000 : 0x9e000;
    current->next_permissions = 0x82000;
    current->everyone_permissions = bit(1024) ? 0x12000 : 0x2000;
    current->group_permissions = 0x4000;
    current->flags = 0x1234fedc;
    current->creation_date = 222;
    current->inventory_type = 10;
    current->asset_type = bit(8) ? 3 : 10;
    current->sale_price = 10;
    current->sale_type = 1;
    current->parent = id(3);
    current->asset_id = id(4);
    auto input = std::make_shared<services::InventoryItem>();
    input->id = id(2);
    input->owner = id(1);
    input->name = user_inventory_text((text & 1) ? "HG Edited" : "Edited");
    input->description = user_inventory_text(incoming[text & 7], true);
    input->next_permissions = bit(4) ? 0 : 0x82000;
    input->everyone_permissions = bit(2048) ? 0x1e000 : 0x8000;
    input->group_permissions = 0x2000;
    input->group_id = id(5);
    input->group_owned = 1;
    input->creation_date = bit(256) ? 0 : -123;
    input->inventory_type = bit(512) ? 18 : 10;
    input->flags = std::bit_cast<std::int32_t>(0xabcd1234u);
    input->sale_price = bit(32768) ? 20 : 10;
    input->sale_type = bit(32768) ? 2 : 1;
    input->parent = id(6);
    if (bit(65536))
        input = current;
    if (bit(131072))
        input.reset();
    auto list = std::make_unique<UserInventoryMoveList>();
    auto first = std::make_shared<services::InventoryItem>();
    first->id = id(2);
    first->owner = id(1);
    auto second = std::make_shared<services::InventoryItem>();
    second->id = id(7);
    second->owner = id(1);
    list->add(bit(4) ? UserInventoryItem{} : first);
    list->add(second);
    if (bit(16))
        list.reset();
    UserInventoryBackend b;
    b.item = [&](auto, auto key) {
        step(1, "GET:" + key.str());
        if (kind == 1) {
            if (bit(1024))
                list->add(std::make_shared<services::InventoryItem>());
            if (bit(512))
                return UserInventoryItem{};
        }
        return bit(1) ? UserInventoryItem{} : current;
    };
    b.update = [&](auto i) {
        step(3, "UPDATE:" + dump(i));
        if (bit(262144)) {
            i->inventory_type = 10;
            i->flags = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(i->flags) ^ 0x80000000u);
            i->asset_id = id(9);
        }
        return !bit(64);
    };
    b.move = [&](auto, auto &items) {
        step(7, "MOVE:" + std::to_string(items.size()));
        if (bit(128))
            items.clear();
        if (bit(256)) {
            auto added = std::make_shared<services::InventoryItem>();
            added->id = id(8);
            items.add(added);
        }
        return !bit(64);
    };
    b.bulk = [&](auto, auto i) {
        step(5, "BULK:" + dump(i));
        if (kind == 1 && bit(2048))
            list->add(std::make_shared<services::InventoryItem>());
    };
    if (bit(32))
        b.transaction = [&](auto, auto, auto i) { step(4, "TRANSACTION:" + dump(i)); };
    b.unix_time = [&] {
        step(2, "TIME");
        return 111;
    };
    b.delete_items = [&](auto, const auto &ids) { step(10, "DELETE:" + ids[0].str() + "," + ids[1].str()); };
    b.delete_folders = [&](auto, const auto &ids) {
        step(11, "FOLDERS:" + ids[0].str() + "," + ids[1].str());
    };
    b.error = [&](auto) { step(6, "ERROR"); };
    b.debug = [&](auto s) {
        step(8, "DEBUG:" + std::string(kind == 1 ? std::to_string(list->size()) : "2"));
    };
    b.warn = [&](auto) { step(9, "WARN"); };
    std::string result = "OK";
    try {
        UserInventoryOperations operations(std::move(b));
        if (kind == 0)
            operations.update(id(1), bit(16) ? id(8) : framework::Uuid{}, id(2), input);
        else if (kind == 1)
            operations.move(id(1), list.get());
        else if (kind == 2)
            operations.remove_items(id(1), {id(2), id(7)});
        else
            operations.remove_folders(id(1), {id(3), id(6)});
    } catch (...) {
        result = "THROW";
    }
    return result + ":" + dump(current) + ":" + dump(input) + trace;
}
}
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int k, m, t, f;
            while (std::cin >> k >> m >> t >> f)
                std::cout << probe(k, m, t, f) << '\n';
            return 0;
        }
        int n = 0;
        auto check = [&](bool ok, const char *s) {
            ++n;
            if (!ok)
                throw std::runtime_error(s);
        };
        check(probe(0, 4, 0, 0).find("|UPDATE:") == std::string::npos, "zero next permissions skips storage");
        check(probe(0, 8, 0, 0).find("|BULK:") != std::string::npos, "landmark suffix refresh");
        check(probe(0, 8 | 16, 0, 0).find("|BULK:") == std::string::npos,
              "transaction suppresses bulk even without module");
        check(probe(0, 2 | 131072, 0, 0).starts_with("OK:"), "wrong owner before null input");
        check(probe(1, 64, 0, 0).find("|BULK:") != std::string::npos, "failed move still reads/notifies");
        check(probe(1, 1024, 0, 0).starts_with("THROW:"), "source enumerator detects callback mutation");
        std::cout << "PASS: " << n << " user inventory operation checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
