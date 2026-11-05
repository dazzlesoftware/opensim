// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/task_inventory_move.hpp"
#include <bit>
#include <iostream>
using namespace opensim;
using namespace scene;
namespace {
framework::Uuid id(int n) {
    auto t = std::to_string(n);
    return framework::Uuid("11223344-5566-7788-99aa-" + std::string(12 - t.size(), '0') + t);
}
std::string text(const std::optional<std::string> &s) {
    if (!s)
        return "NULL";
    std::string out;
    for (unsigned char c : *s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string dump(const TaskInventoryAgentItem &v) {
    const auto &i = v.item;
    auto u = [](int n) { return std::to_string(static_cast<std::uint32_t>(n)); };
    return i.id.str() + ":" + i.owner.str() + ":" + i.asset_id.str() + ":" + i.creator_id + ":" +
           text(v.creator_data) + ":" + text(i.name) + ":" + text(i.description) + ":" +
           std::to_string(i.asset_type) + ":" + std::to_string(i.inventory_type) + ":" +
           u(i.base_permissions) + ":" + u(i.current_permissions) + ":" + u(i.next_permissions) + ":" +
           u(i.everyone_permissions) + ":" + u(i.group_permissions) + ":" + u(i.flags) + ":" +
           std::to_string(i.creation_date) + ":" + i.group_id.str() + ":" + std::to_string(i.group_owned) +
           ":" + std::to_string(i.sale_price) + ":" + std::to_string(i.sale_type) + ":" + i.parent.str();
}
std::string probe(int kind, int mode, int perm, int failure) {
    std::string trace;
    bool present = true;
    int stored = 0;
    auto step = [&](int point, std::string s) {
        trace += "|" + s;
        if (point == failure)
            throw std::runtime_error("injected");
    };
    auto task = std::make_shared<framework::TaskInventoryItem>([] { return 999; });
    task->item_id = id(2);
    task->asset_id = id(3);
    task->creator_id = id(4);
    task->owner_id = (mode & 1) ? id(9) : id(1);
    task->type = (mode & 256) ? 10 : 7;
    task->inventory_type = (mode & 128) ? 6 : 10;
    task->base_permissions = 0xf00fffffu;
    task->next_permissions = 0x9e000;
    task->everyone_permissions = 0x8a000;
    task->group_permissions = 0x84000;
    task->current_permissions =
        0x80000u | (static_cast<std::uint32_t>(perm & 240) << 9) | static_cast<std::uint32_t>(perm & 15);
    if (mode & 32)
        task->current_permissions &= ~0x8000u;
    if (mode & 64)
        task->current_permissions &= ~0x2000u;
    task->flags = 0xffffffffu;
    task->creator_data =
        (mode & 512) ? std::optional<std::string>{} : std::optional<std::string>("https://profile;Creator");
    task->name = (mode & 2048)   ? std::optional<std::string>{}
                 : (mode & 1024) ? std::optional<std::string>(std::string("A\0é", 4))
                                 : std::optional<std::string>("Script");
    task->description = (mode & 2048)     ? std::optional<std::string>{}
                        : (mode & 1024)   ? std::optional<std::string>(std::string("D\0中", 5))
                        : (mode & 524288) ? std::optional<std::string>(" \t\n　")
                                          : std::optional<std::string>("Description");
    TaskInventoryMovePart part{id(6), (mode & 2) ? id(9) : id(1), 123};
    TaskInventoryMoveBackend b;
    b.task_item = [&](auto p, auto key) {
        step(1, "GET:" + p.id.str() + ":" + key.str());
        return (mode & 4) || !present ? std::shared_ptr<framework::TaskInventoryItem>{} : task;
    };
    b.unix_time = [&] {
        step(2, "TIME");
        return 111;
    };
    b.random_uuid = [&] {
        step(3, "NEW");
        return id(5);
    };
    b.propagate_permissions = [&] {
        step(4, "PROPAGATE");
        return bool(mode & 8);
    };
    b.bypass_permissions = [&] {
        step(6, "BYPASS");
        return bool(mode & 16);
    };
    auto added = [&](auto value) {
        ++stored;
        if (mode & 65536)
            present = false;
        if (mode & 131072)
            task->current_permissions &= ~0x8000u;
        value->item.parent = id(8);
    };
    b.add_client = [&](const auto &c, auto value) {
        step(5, "ADD:" + c.agent.str() + ":" + dump(*value));
        added(value);
    };
    b.add_offline = [&](auto value) {
        step(14, "OFFLINE:" + dump(*value));
        added(value);
    };
    b.client = [&](auto avatar) -> std::optional<TaskInventoryMoveClient> {
        step(13, "CLIENT:" + avatar.str());
        if (mode & 4096)
            return {};
        return TaskInventoryMoveClient{id(1), {}};
    };
    b.remove_script_events = [&](auto p, auto key) { step(7, "EVENTS:" + p.id.str() + ":" + key.str()); };
    b.active_script_count = [&](auto p, int n) { step(8, "ACTIVE:" + p.id.str() + ":" + std::to_string(n)); };
    b.remove_item = [&](auto p, auto key) {
        step(9, "REMOVE:" + p.id.str() + ":" + key.str());
        present = false;
    };
    b.part = [&](auto local) -> std::optional<TaskInventoryMovePart> {
        step(10, "PART:" + std::to_string(local));
        if (mode & 8192)
            return {};
        return part;
    };
    b.can_copy = [&](auto key, auto object, auto agent) {
        step(11, "COPY:" + key.str() + ":" + object.str() + ":" + agent.str());
        return !(mode & 16384);
    };
    b.debug = [&](auto) { step(15, "DEBUG"); };
    b.error = [&](auto) { step(16, "ERROR"); };
    b.warn = [&](auto) { step(17, "WARN"); };
    TaskInventoryMoveClient client{id(1), [&](const auto &msg, bool modal) {
                                       step(12, "ALERT:" + msg.value_or("NULL") + ":" + (modal ? "1" : "0"));
                                   }};
    TaskInventoryMoveController controller(std::move(b));
    std::string result = "VOID";
    try {
        auto key = (mode & 32768) ? framework::Uuid{} : id(2);
        TaskInventoryMoveOutcome outcome;
        if (kind == 0)
            outcome = controller.create_agent_item(id(1), part, key);
        else if (kind == 1) {
            controller.remove_noncopy(part, key);
            return result + ":" + (present ? "1" : "0") + ":" + std::to_string(stored) + trace;
        } else if (kind == 2)
            outcome = controller.move(client, id(7), part, key);
        else if (kind == 3) {
            controller.client_move(client, id(7), 123, key);
            return result + ":" + (present ? "1" : "0") + ":" + std::to_string(stored) + trace;
        } else
            outcome = controller.move((mode & 1048576) ? id(9) : id(1), id(7), part, key);
        result = (outcome.item ? dump(*outcome.item) : "NULL") + ":" + outcome.message.value_or("NULL");
    } catch (...) {
        result = "THROW";
    }
    return result + ":" + (present ? "1" : "0") + ":" + std::to_string(stored) + trace;
}
}
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int k, m, p, f;
            while (std::cin >> k >> m >> p >> f)
                std::cout << probe(k, m, p, f) << '\n';
            return 0;
        }
        int count = 0;
        auto check = [&](bool v, const char *s) {
            ++count;
            if (!v)
                throw std::runtime_error(s);
        };
        check(probe(2, 256, 240, 0).find("|REMOVE:") == std::string::npos, "copy kept");
        check(probe(2, 256 | 32, 240, 0).find("|EVENTS:") != std::string::npos, "no-copy script removed");
        check(probe(2, 256 | 32 | 16, 240, 0).find("|REMOVE:") == std::string::npos,
              "bypass retains no-copy");
        check(probe(0, 1 | 64, 240, 0).starts_with("NULL:Item doesn't have the Transfer permission."),
              "transfer required");
        check(probe(2, 0, 240, 5).starts_with("THROW:1:0"), "addexception preserves source");
        check(probe(3, 32768, 240, 0) == "VOID:1:0", "zero vieweritem ignored");
        check(probe(2, 1024, 240, 0).find("41000000") != std::string::npos, "SDK embedded-NUL normalization");
        std::cout << "PASS: " << count << " task inventory move checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
