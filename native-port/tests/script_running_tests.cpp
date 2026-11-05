// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/script_running.hpp"
#include <iostream>
using namespace opensim;
using namespace scene;
namespace {
framework::Uuid id(int n) {
    if (!n)
        return {};
    auto s = std::to_string(n);
    return framework::Uuid("11223344-5566-7788-99aa-" + std::string(12 - s.size(), '0') + s);
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
std::string probe(int mode, int object, int item, int agent) {
    std::vector<std::string> trace;
    bool stored = true;
    auto add = [&](const std::string& s) { trace.push_back(s); };
    ScriptRunningBackend backend;
    backend.find_instance = [&](auto key) -> std::optional<ScriptRunningInstance> {
        add("F:" + key.str());
        if (mode & 4)
            throw std::runtime_error("find");
        if ((mode & 2) || key != id(10))
            return {};
        ScriptRunningInstance instance;
        instance.running = [&] {
            add("G");
            if (mode & 8)
                throw std::runtime_error("get");
            return bool(mode & 1);
        };
        instance.set_item_running = [&](bool value) {
            add(value ? "S1" : "S0");
            if (mode & 16)
                throw std::runtime_error("set");
            stored = value;
        };
        return instance;
    };
    backend.trace = [&](auto obj, auto key) {
        add("T:" + obj.str() + "," + key.str());
        if (mode & 128)
            throw std::runtime_error("trace");
    };
    backend.reply = [&](auto who, auto obj, auto key, bool value) {
        if (mode & 64)
            add("EQ:" + obj.str() + "," + key.str() + "," + (value ? "1" : "0") + "," + who.str());
        else
            add("UDP:" + hex(network::lludp::encode_packet(script_running_reply_packet(obj, key, value))));
        if (mode & 32)
            throw std::runtime_error("reply");
    };
    GetScriptRunningController controller(std::move(backend));
    std::string status = "OK";
    try {
        controller.request(id(agent), id(object), id(item));
    } catch (const std::exception&) {
        status = "ERROR";
    }
    status += stored ? ":1" : ":0";
    for (const auto& event : trace) {
        status += '|';
        status += event;
    }
    return status;
}
int checks = 0;
void check(bool value, const char* text) {
    ++checks;
    if (!value)
        throw std::runtime_error(text);
}
void run() {
    check(probe(0, 20, 99, 42) == "OK:1|F:" + id(99).str(), "missing instance sends no response");
    check(probe(0, 20, 10, 42).find("|G|S0|UDP:") != std::string::npos,
          "false running updates metadata and replies");
    check(probe(1 | 64, 77, 10, 42).find("EQ:" + id(77).str()) != std::string::npos,
          "source echoes mismatching request object");
    check(probe(8, 20, 10, 42).find("|S") == std::string::npos, "getter failure prevents metadata/reply");
    check(probe(16, 20, 10, 42).find("UDP:") == std::string::npos, "metadata failure prevents response");
    check(probe(32, 20, 10, 42).starts_with("ERROR:0"), "metadata update precedes reply failure");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--probe") {
            int mode, obj, item, agent;
            while (std::cin >> mode >> obj >> item >> agent)
                std::cout << probe(mode, obj, item, agent) << '\n';
            return 0;
        }
        run();
        std::cout << "PASS: " << checks << " script running checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
