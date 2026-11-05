// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/task_inventory_item.hpp"
#include <iostream>
#include <vector>
using namespace opensim::framework;
namespace {
Uuid id(int n) {
    auto t = std::to_string(n);
    return Uuid("11223344-5566-7788-99aa-" + std::string(12 - t.size(), '0') + t);
}
std::string str(const std::optional<std::string> &s) {
    if (!s)
        return "-";
    std::string out;
    for (unsigned char c : *s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::vector<std::optional<std::string>> inputs() {
    return {{},
            "",
            id(2).str(),
            "invalid",
            id(2).str() + ";https://profile",
            id(2).str() + ";https://profile;Display Name",
            id(2).str() + ";profile;Name;ignored;tail",
            ";",
            ";;",
            ";;;extra",
            "bad;profile;name",
            id(2).str() + ";;",
            id(2).str() + ";",
            " " + id(2).str() + " ",
            "{" + id(2).str() + "}",
            "112233445566778899aa000000000002",
            id(2).str() + ";é中;😃",
            std::string("bad\0uuid", 8),
            id(2).str() + "; ",
            id(2).str() + ";profile;",
            id(2).str() + ";profile;;ignored",
            id(2).str() + "\n",
            "00000000-0000-0000-0000-000000000000"};
}
std::string dump(const TaskInventoryItem &i) {
    return i.asset_id.str() + "," + std::to_string(i.creation_date) + "," + i.creator_id.str() + "," +
           str(i.creator_data) + "," + std::to_string(i.flags) + "," + std::to_string(i.type) + "," +
           std::to_string(i.inventory_type) + "," + i.item_id.str() + "," + i.old_item_id.str() + "," +
           i.loaded_item_id.str() + "," + i.rezzer_id.str() + "," + str(i.name) + "," + str(i.description) +
           "," + i.owner_id.str() + "," + i.last_owner_id.str() + "," + i.group_id.str() + "," +
           i.parent_id.str() + "," + i.parent_part_id.str() + "," + std::to_string(i.base_permissions) + "," +
           std::to_string(i.current_permissions) + "," + std::to_string(i.everyone_permissions) + "," +
           std::to_string(i.group_permissions) + "," + std::to_string(i.next_permissions) + "," +
           i.permissions_granter.str() + "," + std::to_string(i.permissions_mask) + "," +
           (i.owner_changed ? "1" : "0") + "," + (i.script_running ? "1" : "0") + "," +
           str(i.creator_identification());
}
std::string probe(int c, int mode, std::int32_t clock) {
    int clock_calls = 0, random_calls = 0;
    TaskInventoryItem item([&] {
        ++clock_calls;
        return clock;
    });
    std::string result = dump(item);
    if (mode & 1) {
        item.asset_id = id(1);
        item.flags = 0x8000ffff;
        item.type = -42;
        item.inventory_type = 57;
        item.item_id = id(2);
        item.old_item_id = id(3);
        item.loaded_item_id = id(4);
        item.rezzer_id = id(5);
        item.name = "name";
        item.description = "desc";
        item.owner_id = id(6);
        item.last_owner_id = id(7);
        item.group_id = id(8);
        item.parent_id = id(9);
        item.parent_part_id = id(10);
        item.base_permissions = 1;
        item.current_permissions = 0x80000000;
        item.everyone_permissions = 3;
        item.group_permissions = 0xffffffff;
        item.next_permissions = 5;
        item.permissions_granter = id(11);
        item.permissions_mask = -2147483647;
        item.owner_changed = true;
        item.script_running = false;
    }
    item.creator_id = id(1);
    item.creator_data = (mode & 2)   ? std::optional<std::string>{}
                        : (mode & 4) ? std::optional<std::string>("")
                                     : std::optional<std::string>("previous;data");
    item.creator_identification(inputs().at(c));
    if (mode & 8)
        item.creator_identification(id(2).str());
    if (mode & 16)
        item.creator_data = {};
    result += "|SET:" + dump(item);
    if (mode & 32) {
        auto clone = item.clone();
        item.name = "changed";
        item.creator_data = "changed";
        item.creator_id = id(8);
        item.item_id = id(8);
        item.owner_changed = !item.owner_changed;
        result += "|CLONE:" + dump(clone);
    }
    auto random = [&] {
        ++random_calls;
        if ((mode & 128) || ((mode & 512) && random_calls == 2))
            throw std::runtime_error("RNG");
        return id(8 + random_calls);
    };
    try {
        if (mode & 64)
            item.reset_ids(id(5), random);
        if (mode & 256)
            item.reset_ids(id(6), random);
    } catch (...) {
        result += "|THROW";
    }
    return result + "|FINAL:" + dump(item) + "|C:" + std::to_string(clock_calls) +
           "|R:" + std::to_string(random_calls);
}
}
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int c, m;
            std::int32_t t;
            while (std::cin >> c >> m >> t)
                std::cout << probe(c, m, t) << '\n';
            return 0;
        }
        int checks = 0;
        auto check = [&](bool v, const char *s) {
            ++checks;
            if (!v)
                throw std::runtime_error(s);
        };
        TaskInventoryItem item([] { return -1; });
        check(item.creation_date == 0xffffffff && item.script_running && !item.owner_changed,
              "constructor defaults");
        check(!item.name && !item.description && item.creator_data && item.creator_data->empty(),
              "null strings vs empty data");
        check(item.base_permissions == 0x7fffffff && item.current_permissions == 0x7fffffff &&
                  item.everyone_permissions == 0x7fffffff && item.group_permissions == 0x7fffffff &&
                  item.next_permissions == 0x7fffffff,
              "permission defaults");
        item.creator_id = id(1);
        item.creator_data = "old";
        item.creator_identification(id(2).str());
        check(item.creator_identification() == id(2).str() + ";old", "plain ID retains data");
        item.creator_identification(std::nullopt);
        check(item.creator_id == id(2) && item.creator_data == std::optional<std::string>(""),
              "null clears only data");
        item.creator_identification(id(1).str() + ";profile");
        check(item.creator_data == std::optional<std::string>("profile;Unknown User"), "missing displayname");
        item.creator_identification(id(1).str() + ";p;n;ignored");
        check(item.creator_data == std::optional<std::string>("p;n"), "extra fields ignored");
        item.item_id = id(2);
        item.old_item_id = id(3);
        item.parent_id = id(4);
        try {
            item.reset_ids(id(5), []() -> Uuid { throw std::runtime_error("rng"); });
        } catch (...) {
        }
        check(item.loaded_item_id == id(3) && item.old_item_id == id(2) && item.item_id == id(2) &&
                  item.parent_id == id(4),
              "reset mutation before RNG failure");
        auto clone = item.clone();
        item.name = "new";
        check(!clone.name, "clone assignment independence");
        std::cout << "PASS: " << checks << " task inventory model checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
