// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/inventory_asset_caps.hpp"
#include "opensim/network/osd_value.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <latch>
using namespace opensim;
using namespace services;
namespace {
framework::Uuid id(int n) {
    return framework::Uuid(n == 1 ? "11223344-5566-7788-99aa-000000000001"
                                  : "11223344-5566-7788-99aa-000000000002");
}
std::string hex(std::string_view s) {
    std::string o;
    for (unsigned char c : s) {
        o += "0123456789abcdef"[c >> 4];
        o += "0123456789abcdef"[c & 15];
    }
    return o;
}
std::string probe(int kind, int mode, int phase) {
    std::string trace;
    auto add = [&](std::string s) { trace += "|" + s; };
    InventoryAssetCapsBackend b;
    std::shared_ptr<InventoryAssetUpload> held;
    b.object_exists = [&](auto) {
        add("O");
        if (mode & 8)
            throw std::runtime_error("object");
        return !(mode & 1);
    };
    b.can_edit_inventory = [&](auto, auto) {
        add("I");
        return !(mode & 2);
    };
    b.new_path = [&] {
        add("P");
        return "/upload";
    };
    b.register_upload = [&](auto upload) {
        held = upload;
        add("A");
    };
    b.remove_upload = [&](auto) { add("R"); };
    b.timer = [&](auto, int n) { add("T" + std::to_string(n)); };
    b.log = [&](auto) { add("L"); };
    if (!(mode & 16))
        b.update = [&](auto a, auto i, auto o, std::span<const std::uint8_t> data) {
            add("U:" + std::to_string(data.size()));
            if (a != id(1) || i != id(1) || o != id(2))
                throw std::runtime_error("IDs");
            if (mode & 32)
                throw std::runtime_error("update");
            return mode & 64 ? framework::Uuid{} : id(2);
        };
    if (mode & 128)
        b.dump = [&](auto) {
            add("D");
            throw std::runtime_error("dump");
        };
    network::HttpRequest req;
    req.method = "POST";
    req.remote_ip = "127.0.0.1";
    req.body = "default{}";
    network::HttpResponse r;
    if (kind == 0 || kind == 2) {
        UpdateInventoryAssetCore core(
            id(1), "http://host:9000",
            kind == 2 ? std::optional<std::uint8_t>{} : std::optional<std::uint8_t>(7), b);
        auto map = network::Value::object({{"item_id", network::Value(phase == 6 ? "invalid" : id(1).str())},
                                           {"task_id", network::Value(phase == 7 ? "invalid" : id(2).str())},
                                           {"is_script_running", network::Value(true)}});
        if (phase == 8)
            map.map.erase("task_id");
        try {
            r = core.handle(req, map);
        } catch (...) {
            r.status = 500;
        }
    } else {
        InventoryAssetUpload upload(id(1), id(1), id(2), static_cast<std::uint8_t>(mode), "/upload",
                                    req.remote_ip, b);
        if (phase == 1)
            req.remote_ip = "127.0.0.2";
        if (phase == 2)
            req.method = "GET";
        if (phase == 3)
            req.body = "";
        if (phase == 4)
            req.body.assign(10000001, 'x');
        if (phase == 6)
            req.body.assign(10000000, 'x');
        if (phase == 5) {
            upload.expire();
            r.status = 0;
        } else {
            if (phase == 7)
                upload.expire();
            r = upload.handle(req);
        }
        return std::to_string(r.status) + "|" + hex(r.body) + trace;
    }
    return std::to_string(r.status) + "|" + hex(r.body) + trace;
}
int checks = 0;
void check(bool v, const char *s) {
    ++checks;
    if (!v)
        throw std::runtime_error(s);
}
}
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int k, m, p;
            while (std::cin >> k >> m >> p)
                std::cout << probe(k, m, p) << '\n';
            return 0;
        }
        check(probe(0, 0, 0).find("|L|O|I|P|T30000|A") != std::string::npos, "source first-stage order");
        check(probe(2, 0, 0) == "200|", "animation wrapper is a no-op");
        check(probe(1, 0, 1).starts_with("401|"), "IP mismatch consumes route");
        check(probe(1, 16, 0).starts_with("410|"), "missing upload delegate");
        check(probe(1, 32, 0).starts_with("500||"), "update exception has no invented compile error body");
        check(probe(1, 0, 6).starts_with("200|"), "exact10MB allowed");
        check(probe(1, 0, 4) == "413||T30000", "oversized retains uploader");
        std::cout << "PASS: " << checks << " generic inventory asset capability checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
