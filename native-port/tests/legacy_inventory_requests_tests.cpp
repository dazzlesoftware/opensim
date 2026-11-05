// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/legacy_inventory_requests.hpp"
#include <iostream>
using namespace opensim;
using namespace scene;
using namespace services;
namespace {
framework::Uuid id(int n) {
    auto s = std::to_string(n);
    return framework::Uuid("11223344-5566-7788-99aa-" + std::string(12 - s.size(), '0') + s);
}
int number(framework::Uuid key) {
    return std::stoi(key.str().substr(24));
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
InventoryItem item(int key, int asset = 500, int type = 6, int owner = 200) {
    InventoryItem i;
    i.id = id(key);
    i.owner = id(owner);
    i.parent = id(2);
    i.asset_id = id(asset);
    i.asset_type = type;
    i.inventory_type = 6;
    i.flags = -1;
    i.name = "item" + std::to_string(key);
    return i;
}
InventoryCollection contents(framework::Uuid folder, int mode) {
    InventoryCollection c;
    c.owner = id(250);
    c.folder = folder;
    c.version = 7;
    if (mode & 128)
        return c;
    InventoryFolder f;
    f.id = id(900);
    f.parent = folder;
    f.name = "child";
    f.type = -1;
    c.folders.push_back(f);
    int targets[] = {500, 500, 999, 999, 501, 502};
    for (int n = 0; n < 6; ++n)
        c.items.push_back(item(201 + n, targets[n], 24, 200 + n));
    return c;
}
std::string probe(int kind, int count, int folder_number, int mode, int flags) {
    std::vector<std::string> trace;
    auto add = [&](const std::string &s) { trace.push_back(s); };
    LegacyInventoryBackend backend;
    backend.item = [&](auto owner, auto key) -> std::optional<InventoryItem> {
        add("I" + owner.str() + "," + key.str());
        if (mode & 8)
            throw std::runtime_error("provider");
        int k = number(key);
        if (k == 999 || ((mode & 64) && k >= 300 && k < 500 && k % 2))
            return {};
        return item((mode & 16) && k == 500 ? 777 : k, 500, k == 501 ? 24 : k == 502 ? 25 : 6);
    };
    backend.content = [&](auto agent, auto folder) {
        add("C" + agent.str() + "," + folder.str());
        if (mode & 256)
            throw std::runtime_error("content provider");
        return contents(folder, mode);
    };
    backend.folder = [&](auto agent, auto folder) -> std::optional<InventoryFolder> {
        add("F" + agent.str() + "," + folder.str());
        if (mode & 512)
            throw std::runtime_error("folder provider");
        if (mode & 4)
            return {};
        InventoryFolder f;
        f.id = folder;
        f.owner = agent;
        f.version = 9;
        return f;
    };
    int library_calls = 0;
    if (mode & 3)
        backend.library_folder = [&](auto folder) -> std::optional<InventoryCollection> {
            add("L" + folder.str());
            ++library_calls;
            if ((mode & 1024) || ((mode & 2048) && library_calls > 1))
                throw std::runtime_error("library provider");
            if (number(folder) != 2 || !((mode & 1) || ((mode & 2) && library_calls > 1)))
                return {};
            return contents(folder, mode);
        };
    LegacyInventoryRequests core(std::move(backend));
    auto active = [&] {
        add(mode & 32 ? "A0" : "A1");
        return !(mode & 32);
    };
    auto sink = [&](auto packet, bool split) {
        add(std::string(split ? "P1:" : "P0:") + hex(network::lludp::encode_packet(packet)));
    };
    try {
        if (kind == 0) {
            std::vector<LegacyItemRequest> requests;
            for (int n = 0; n < count; ++n)
                requests.push_back({id(100 + n), id(300 + n)});
            try {
                core.process_items(id(42), requests, active, sink);
            } catch (...) {
            }
        } else {
            LegacyFolderRequest request{folder_number ? id(folder_number) : framework::Uuid{},
                                        bool(flags & 1), bool(flags & 2)};
            auto disposition = core.receive_folder(id(42), request, sink);
            if (disposition == LegacyFolderDisposition::Queued) {
                add("Q");
                if (kind == 2)
                    try {
                        if (core.process_folder(id(42), request, active, sink))
                            add("S50");
                    } catch (...) {
                        add("E");
                    }
            }
        }
    } catch (...) {
        add("E");
    }
    std::string result;
    for (const auto &part : trace) {
        if (!result.empty())
            result += '|';
        result += part;
    }
    return result;
}
int checks = 0;
void check(bool value, const char *label) {
    ++checks;
    if (!value)
        throw std::runtime_error(label);
}
void run() {
    check(probe(0, 1, 2, 32, 3) == "A0", "inactive item requests skipped");
    check(probe(1, 0, 0, 0, 3).empty(), "zero folder ignored before providers");
    check(probe(1, 0, 2, 0, 3) == "Q", "ordinary receive only queues");
    check(probe(2, 0, 2, 32, 3) == "Q|A0", "inactive queued folder skipped without sleep");
    check(probe(2, 0, 2, 1 | 32, 3).starts_with("L"),
          "library immediate branch does not require activity callback");
    check(probe(2, 0, 2, 4, 3).ends_with("|S50"), "missing ordinary folder still reaches queue gate");
    check(probe(2, 0, 2, 8, 3).ends_with("|E"), "provider error precedes sleep gate");
    check(probe(0, 3, 2, 64, 3).find("L") == std::string::npos, "item path never queries library");
}
}
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--probe") {
            int k, n, f, m, b;
            while (std::cin >> k >> n >> f >> m >> b)
                std::cout << probe(k, n, f, m, b) << '\n';
            return 0;
        }
        run();
        std::cout << "PASS: " << checks << " legacy inventory request checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
