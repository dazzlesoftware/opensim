// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/library_descendents.hpp"
#include <iostream>
using namespace opensim;
using namespace services;
namespace {
bool diagnostics_enabled = false;

framework::Uuid id(int n) {
    auto s = std::to_string(n);
    return framework::Uuid("00000000-0000-0000-0000-" + std::string(12 - s.size(), '0') + s);
}
std::string hex(std::string_view s) {
    std::string out;
    constexpr char d[] = "0123456789abcdef";
    for (unsigned char c : s) {
        out += d[c >> 4];
        out += d[c & 15];
    }
    return out;
}
std::string unhex(const std::string& s) {
    if (s == "-")
        return {};
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
    return out;
}
InventoryCollection collection(framework::Uuid key, int options) {
    InventoryCollection c;
    c.version = 7;
    InventoryFolder f;
    f.id = id(900);
    f.parent = key;
    f.name = "library <&>";
    f.type = -1;
    if (!(options & 16))
        c.folders.push_back(f);
    InventoryItem item;
    item.id = id(201);
    item.owner = id(200);
    item.parent = key;
    item.asset_id = id(500);
    item.name = "library item";
    item.flags = -1;
    if (!(options & 32))
        c.items.push_back(item);
    return c;
}
std::string probe(int mode, int options, const std::string& body) {
    std::string diagnostics;
    std::string trace;
    auto add = [&](const std::string& s) {
        if (!trace.empty())
            trace += ';';
        trace += s;
    };
    bool available = !(mode & 2), constructed = false;
    framework::Uuid owner = options & 8 ? framework::Uuid{} : id(200);
    InventoryBadRequests bad([] { return 0; });
    if (options & 1)
        bad.add(id(2));
    LibraryDescendentsBackend backend;
    if (diagnostics_enabled)
        backend.diagnostic = [&](auto level, const auto& message) {
            if (!diagnostics.empty())
                diagnostics += ';';
            diagnostics += (level == InventoryLogLevel::Debug  ? "D:"
                            : level == InventoryLogLevel::Warn ? "W:"
                                                               : "E:") +
                           hex(message);
        };
    backend.root_owner = [&]() -> std::optional<framework::Uuid> {
        add("R");
        return available ? std::optional{owner} : std::nullopt;
    };
    backend.folder_content = [&](auto key) -> std::optional<InventoryCollection> {
        add("F" + key.str());
        if (mode & 8)
            throw std::runtime_error("provider");
        if ((mode & 4) || key == id(999))
            return {};
        auto c = collection(key, options);
        if (mode & 32)
            available = false;
        return c;
    };
    if (mode & 64)
        backend.root_owner = {};
    std::string result;
    try {
        FetchLibraryDescendentsCore core(std::move(backend));
        constructed = true;
        if (mode & 1)
            available = false;
        if (mode & 16)
            owner = id(201);
        network::HttpRequest request;
        request.body = body;
        auto response =
            core.handle(request, options & 2 ? nullptr : &bad, options & 4 ? framework::Uuid{} : id(42));
        result = std::to_string(response.status) + ":" + hex(response.body);
    } catch (const std::exception&) {
        result = constructed ? "EXCEPTION" : "CTOR";
    }
    return result + "|" + trace + "|" + (bad.contains(id(1)) ? "1" : "0") +
           (bad.contains(id(2)) ? "1" : "0") + (bad.contains(id(3)) ? "1" : "0") +
           (diagnostics_enabled ? "|" + diagnostics : "");
}
std::string request(int key, int owner = 200) {
    return "<llsd><map><key>folders</key><array><map><key>folder_id</key><uuid>" + id(key).str() +
           "</uuid><key>owner_id</key><uuid>" + id(owner).str() +
           "</uuid><key>fetch_folders</key><boolean>0</boolean><key>fetch_items</key><boolean>0</boolean></map></array></map></llsd>";
}
int checks = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value)
        throw std::runtime_error(label);
}
void run() {
    check(probe(64, 0, request(1)) == "CTOR||000", "missing library service rejected at construction");
    check(probe(2, 0, request(1)).starts_with("CTOR|R|"), "source constructor requires root");
    check(probe(1, 0, "bad") == "503:|R;R|000", "absent runtime root503 before parsing");
    check(probe(0, 0, "bad").ends_with("|R;R|000"), "parse errors empty folders");
    check(probe(0, 0, request(1, 100)).ends_with("|R;R|000"), "foreign owner ignored");
    check(probe(0, 0, request(0)).find(hex("<key>version</key><integer>-1</integer>")) != std::string::npos,
          "zero folder version minus one");
    check(probe(4, 0, request(1)).ends_with("|100"), "missing library folder negatively cached");
    check(probe(0, 1, request(2)).find(";F") == std::string::npos, "cached folder avoids tree search");
    check(probe(0, 0, request(1)).find(hex("<key>flags</key><integer>-1</integer>")) != std::string::npos,
          "false fetch flags ignored with full item flags");
    check(probe(32, 0, request(1)).starts_with("EXCEPTION"),
          "root disappearing during tree lookup propagates");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 &&
            (std::string_view(argv[1]) == "--probe" || std::string_view(argv[1]) == "--probe-logs")) {
            diagnostics_enabled = std::string_view(argv[1]) == "--probe-logs";
            int mode, options;
            std::string body;
            while (std::cin >> mode >> options >> body)
                std::cout << probe(mode, options, unhex(body)) << '\n';
            return 0;
        }
        run();
        std::cout << "PASS: " << checks << " library descendents checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
