// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/script_instance_create.hpp"
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
    for (unsigned char b : s) {
        out += "0123456789abcdef"[b >> 4];
        out += "0123456789abcdef"[b & 15];
    }
    return out;
}
std::string row(const std::shared_ptr<framework::TaskInventoryItem>& i) {
    return i ? i->item_id.str() + ":" + i->old_item_id.str() + ":" + std::to_string(i->permissions_mask) +
                   ":" + i->permissions_granter.str() + ":" + (i->script_running ? "1" : "0")
             : "NULL";
}
std::string probe(int mode, int state, int failure) {
    std::string trace;
    int active = 0;
    bool flagged = false, compiler_error = false;
    auto step = [&](int point, std::string value) {
        trace += "|" + value;
        if (point == failure)
            throw std::runtime_error("injected");
    };
    auto input = std::make_shared<framework::TaskInventoryItem>([] { return 111; });
    input->item_id = id(2);
    input->asset_id = id(3);
    input->old_item_id = id(4);
    input->loaded_item_id = id(5);
    input->permissions_mask = 123;
    input->permissions_granter = id(6);
    input->script_running = !(mode & 128);
    auto stored = (mode & 8) ? std::make_shared<framework::TaskInventoryItem>(input->clone()) : input;
    if (mode & 8) {
        stored->old_item_id = id(7);
        stored->permissions_mask = 456;
        stored->permissions_granter = id(8);
    }
    if (mode & 512)
        input.reset();
    if (mode & 1024)
        stored.reset();
    ScriptInstanceCreateBackend b;
    b.can_run = [&](auto i) {
        step(1, std::string("CAN:") + (i ? "I" : "NULL"));
        return !(mode & 1);
    };
    b.mark_scripted = [&] {
        step(2, "FLAG");
        flagged = true;
    };
    b.scripts_disabled = [&] {
        step(3, "DISABLED");
        return bool(mode & 2);
    };
    b.find_item = [&](auto key) -> std::optional<ScriptInstanceCreateBackend::Item> {
        step(4, "FIND:" + key.str());
        if (mode & 16384) {
            input->item_id = id(10);
            input->asset_id = id(11);
        }
        if (mode & 4)
            return {};
        return stored;
    };
    b.trust_binaries = [&] {
        step(5, "TRUST");
        return bool(mode & 16);
    };
    b.load_asset = [&](auto key) -> std::shared_ptr<framework::AssetBase> {
        step(6, "ASSET:" + key.str());
        if (mode & 32)
            return {};
        auto asset = std::make_shared<framework::AssetBase>();
        if (!(mode & 256))
            asset->data = std::make_shared<std::vector<std::uint8_t>>(
                (mode & 2048) ? std::vector<std::uint8_t>{'a', 0xe2, 0x82, 0}
                              : std::vector<std::uint8_t>{'s', 'r', 'c', 0});
        return asset;
    };
    b.has_saved_state = [&] {
        step(7, "SAVED");
        return bool(mode & 64);
    };
    b.restore_saved_state = [&](auto loaded, auto old, auto key) {
        step(8, "RESTORE:" + loaded.str() + ":" + old.str() + ":" + key.str());
        return id(9);
    };
    b.local_id = [&] {
        step(9, "LOCAL");
        return 123u;
    };
    b.trigger_rez = [&](auto local, auto key, const auto& text, int start, bool post, const auto& engine,
                        int source) {
        step(10, "REZ:" + std::to_string(local) + ":" + key.str() + ":" + hex(text) + ":" +
                     std::to_string(start) + ":" + (post ? "1" : "0") + ":" + engine + ":" +
                     std::to_string(source));
        compiler_error = bool(mode & 8192);
    };
    b.store_errors = [&](auto key, const auto& errors) {
        std::string value = "ERRORS:" + key.str() + ":";
        if (!errors)
            value += "NULL";
        else
            for (const auto& e : *errors)
                value += hex(e) + ",";
        step(11, value);
    };
    b.active_scripts = [&](int count) {
        step(12, "ACTIVE:" + std::to_string(count));
        active += count;
    };
    b.schedule_full = [&] { step(13, "FULL"); };
    b.error = [&](auto) { step(14, "LOG"); };
    ScriptInstanceCreateController controller(std::move(b));
    std::string result;
    try {
        result = controller.create(input, -42, true, "Native", state) ? "TRUE" : "FALSE";
    } catch (...) {
        result = "THROW";
    }
    return result + ":" + std::to_string(active) + ":" + (flagged ? "1" : "0") + ":" +
           (compiler_error ? "1" : "0") + ":" + row(input) + ":" + row(stored) + trace;
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int m, s, f;
            while (std::cin >> m >> s >> f)
                std::cout << probe(m, s, f) << '\n';
            return 0;
        }
        int n = 0;
        auto check = [&](bool b, const char* s) {
            ++n;
            if (!b)
                throw std::runtime_error(s);
        };
        check(probe(1, 0, 0).starts_with("FALSE:0:0"), "permission beforeflag");
        check(probe(2, 0, 0).starts_with("FALSE:0:1"), "disabledafterflag");
        check(probe(8192 | 128, 0, 0).starts_with("TRUE:1:1:1"), "stoppedcompileerror stillcounts");
        check(probe(16, 2, 0).find("|ASSET:") == std::string::npos &&
                  probe(16, 2, 0).find("|FULL") != std::string::npos,
              "trustedbinary skipsasset/fullupdate");
        check(probe(256, 0, 0).starts_with("THROW:0:1"), "nullassetdata beforecount");
        check(probe(8 | 64, 0, 0).find("|RESTORE:") != std::string::npos, "savedstaterestoration");
        std::cout << "PASS: " << n << " script instance create checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
