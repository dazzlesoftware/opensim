// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_notifications.hpp"
#include <iostream>
using namespace opensim;
using namespace scene;
namespace {
framework::Uuid id(int n) {
    auto t = std::to_string(n);
    return framework::Uuid("11223344-5566-7788-99aa-" + std::string(12 - t.size(), '0') + t);
}
std::string hex(std::span<const std::uint8_t> bytes) {
    std::string s;
    for (auto b : bytes) {
        s += "0123456789abcdef"[b >> 4];
        s += "0123456789abcdef"[b & 15];
    }
    return s;
}
std::vector<std::optional<std::string>> messages() {
    std::string unicode;
    for (int i = 0; i < 100; ++i)
        unicode += "é中😃";
    return {{},
            "",
            "message",
            "ALERT: message",
            "NOTIFY: message",
            "alert: message",
            "Home position set.",
            "You died and have been teleported to your home location",
            std::string(300, 'a'),
            unicode,
            std::string("abc\0suffix", 10),
            "ALERT:",
            "NOTIFY:",
            " ALERT: message",
            "\xc2\xad"
            "ALERT: message",
            "A\xc2\xadLERT: message",
            std::string("\0ALERT: message", 15),
            "\r\nmessage"};
}
std::string probe(int kind, int mode, int count) {
    auto agent = (mode & 4) ? framework::Uuid{} : id(1), session = (mode & 8) ? framework::Uuid{} : id(2);
    std::string result = "OK", trace;
    try {
        if (kind == 2) {
            auto p = agent_alert(agent, messages().at(count), bool(mode & 1));
            trace =
                "|" + hex(network::lludp::encode_packet(p)) + "|" + hex(network::lludp::encode_datagram(p));
        } else {
            std::vector<framework::Uuid> ids;
            for (int n = 0; n < count; ++n)
                ids.push_back(n % 3 == 0 ? framework::Uuid{} : id(n % 2 + 3));
            InventoryNotificationBackend b;
            if (!(mode & 1))
                b.enqueue = [&](auto bytes, auto who) {
                    trace += "|E:" + who.str() + ":" + hex(*bytes);
                    if (mode & 32)
                        throw std::runtime_error("enqueue");
                    return !(mode & 16);
                };
            b.debug = [&](auto) {
                trace += "|D";
                if (mode & 64)
                    throw std::runtime_error("debug");
            };
            auto values = (mode & 2) ? nullptr : &ids;
            if (kind == 0)
                send_remove_inventory_items(agent, session, values, b);
            else
                send_remove_inventory_folders(agent, session, values, b);
        }
    } catch (...) {
        result = "THROW";
    }
    return result + trace;
}
}
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int k, m, n;
            while (std::cin >> k >> m >> n)
                std::cout << probe(k, m, n) << '\n';
            return 0;
        }
        if (probe(0, 1 | 2, 0) != "OK|D" || probe(0, 2, 0) != "THROW" || probe(2, 0, 0) != "THROW" ||
            !probe(2, 1, 0).starts_with("OK|"))
            throw std::runtime_error("source null ordering");
        std::cout << "PASS: inventory notification null/availability checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
