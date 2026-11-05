// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/script_task_caps.hpp"
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
    ScriptTaskCapsBackend b;
    std::shared_ptr<TaskScriptUpload> held;
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
    b.can_edit_script = [&](auto, auto, auto) {
        add("S");
        return !(mode & 4);
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
        b.update = [&](auto a, auto i, auto o, bool run, std::span<const std::uint8_t> data) {
            add(std::string("U") + (run ? "1" : "0") + ":" + std::to_string(data.size()));
            if (a != id(1) || i != id(1) || o != id(2))
                throw std::runtime_error("IDs");
            if (mode & 32)
                throw std::runtime_error("compile");
            return (mode & 64) ? std::vector<std::string>{"(1,2) syntax <error> & text\r\n\t\"'é 😃", ""}
                               : std::vector<std::string>{};
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
    if (kind == 0) {
        UpdateScriptTaskCore core(id(1), "http://host:9000", b);
        auto map = network::Value::object({{"item_id", network::Value(phase == 6 ? "invalid" : id(1).str())},
                                           {"task_id", network::Value(phase == 7 ? "invalid" : id(2).str())},
                                           {"is_script_running", network::Value(true)}});
        r = core.handle(req, map);
    } else {
        TaskScriptUpload upload(id(1), id(1), id(2), bool(mode & 1), "/upload", req.remote_ip, b);
        if (phase == 1)
            req.remote_ip = "127.0.0.2";
        if (phase == 2)
            req.method = "GET";
        if (phase == 3)
            req.body = "";
        if (phase == 4)
            req.body.assign(10000001, 'x');
        if (phase == 5) {
            upload.expire();
            r.status = 0;
        } else
            r = upload.handle(req);
        return std::to_string(r.status) + "|" + hex(r.body) + trace;
    }
    return std::to_string(r.status) + "|" + hex(r.body) + trace;
}
int checks = 0;
void check(bool v, const char* s) {
    ++checks;
    if (!v)
        throw std::runtime_error(s);
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int k, m, p;
            while (std::cin >> k >> m >> p)
                std::cout << probe(k, m, p) << '\n';
            return 0;
        }
        check(probe(0, 0, 0).find("|O|I|S|P|T30000|A") != std::string::npos, "initial registration order");
        check(probe(1, 0, 1).starts_with("401|"), "IP failure");
        check(probe(1, 16, 0).starts_with("410|"), "missing update");
        check(probe(1, 32, 0).find(hex("could not compile script")) != std::string::npos, "compile failure");
        check(probe(1, 0, 2) == "404||T30000", "wrong method retains URL");
        check(probe(1, 0, 3) == "400||T30000", "empty body retains URL");
        check(probe(1, 0, 4) == "413||T30000", "large body retains URL");
        check(probe(1, 0, 5) == "0||T30000|R|T0", "expiry removes URL");
        ScriptTaskCapsBackend backend;
        std::atomic<int> removed = 0, cancelled = 0, updated = 0;
        backend.timer = [&](auto, int n) {
            if (!n)
                ++cancelled;
        };
        backend.remove_upload = [&](auto) { ++removed; };
        backend.update = [&](auto, auto, auto, bool, std::span<const std::uint8_t>) {
            ++updated;
            return std::vector<std::string>{};
        };
        TaskScriptUpload upload(id(1), id(1), id(2), true, "/upload", "0:0:0:0:0:0:0:1", backend);
        network::HttpRequest req;
        req.method = "POST";
        req.remote_ip = "::1";
        req.body = "arbitrary actual script bytes";
        check(upload.handle(req).status == 200, "equivalent IPv6 addresses");
        check(!upload.active(), "consumed upload");
        check(upload.handle(req).status == 404, "one shot");
        upload.expire();
        check(removed == 1 && cancelled == 1 && updated == 1, "idempotent expiry");
        for (int n = 0; n < 32; ++n) {
            TaskScriptUpload simultaneous(id(1), id(1), id(2), false, "/race", "::1", backend);
            std::latch start(2);
            int a = 0, b = 0;
            std::thread first([&] {
                start.count_down();
                start.wait();
                a = simultaneous.handle(req).status;
            });
            std::thread second([&] {
                start.count_down();
                start.wait();
                b = simultaneous.handle(req).status;
            });
            first.join();
            second.join();
            check((a == 200 && b == 404) || (a == 404 && b == 200), "concurrent upload consumed once");
        }
        check(removed == 33 && cancelled == 33 && updated == 33, "concurrent backend calls exactly once");
        backend.object_exists = [](auto) { return true; };
        backend.can_edit_inventory = [](auto, auto) { return true; };
        backend.can_edit_script = [](auto, auto, auto) { return true; };
        backend.new_path = []() { return "/next"; };
        std::shared_ptr<TaskScriptUpload> registered;
        backend.register_upload = [&](auto value) { registered = value; };
        auto core = std::make_shared<UpdateScriptTaskCore>(id(1), "https://host:9001", backend);
        UpdateScriptTaskRoute route("/script", core);
        req.method = "GET";
        check(route.handle(req).status == 404, "initial wrong method");
        req.method = "POST";
        req.body = "[]";
        check(route.handle(req).status == 400, "initial nonmap");
        req.body = "<llsd><map>";
        check(route.handle(req).status == 400, "initial invalid XML");
        req.body = "{}";
        check(route.handle(req).body.find("failed to recode request") != std::string::npos,
              "initial missing IDs");
        req.body = "{\"item_id\":\"" + id(1).str() + "\",\"task_id\":\"" + id(2).str() +
                   "\",\"is_script_running\":true}";
        check(route.handle(req).body.find("https://host:9001/next") != std::string::npos && registered,
              "JSON initial request");
        registered->expire();
        req.body = "<llsd><map><key>item_id</key><uuid>" + id(1).str() + "</uuid><key>task_id</key><uuid>" +
                   id(2).str() + "</uuid></map></llsd>";
        check(route.handle(req).status == 200 && registered->active(), "XML initial request");
        registered->expire();
        std::cout << "PASS: " << checks << " script task capability checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
