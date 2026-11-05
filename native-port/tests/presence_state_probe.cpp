// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/presence_state.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include <iostream>
#include <functional>
#include <stdexcept>
using namespace opensim;
using network::Value;
std::string hex(std::string_view s) {
    std::string out;
    const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out;
}
std::string decode(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return out;
}
struct Context {
    Value command;
    std::string trace;
    services::PresenceStateMachine* machine = nullptr;
    std::function<void()> release;
    bool mutated = false;
    void log(const char* op) {
        trace += std::string(op) + ";";
        auto mutation = network::osd_string(command.get("mutation"));
        if (!mutated && mutation.starts_with(std::string(op) + "_")) {
            mutated = true;
            if (mutation.ends_with("release"))
                release();
            else
                machine->set_state(mutation.ends_with("pre")       ? services::PresenceState::pre_remove
                                   : mutation.ends_with("removed") ? services::PresenceState::removed
                                                                   : services::PresenceState::removing);
        }
        if (network::osd_string(command.get("failure")) == op)
            throw std::runtime_error(op);
    }
};
struct Identity : services::PresenceStateIdentity {
    Context& ctx;
    explicit Identity(Context& c) : ctx(c) {
    }
    std::optional<std::string> presence_name() const override {
        ctx.log("name");
        return network::osd_boolean(ctx.command.get("null_name")) ? std::nullopt
                                                                  : std::optional<std::string>("Avatar 試験");
    }
    std::optional<std::string> scene_name() const override {
        if (network::osd_boolean(ctx.command.get("null_scene")))
            throw std::runtime_error("Object reference not set to an instance of an object.");
        ctx.log("scene");
        return network::osd_boolean(ctx.command.get("null_scene_name"))
                   ? std::nullopt
                   : std::optional<std::string>("Region");
    }
};
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        Context ctx{network::parse_osd_json(decode(line))};
        std::shared_ptr<Identity> identity;
        if (!network::osd_boolean(ctx.command.get("null_owner")))
            identity = std::make_shared<Identity>(ctx);
        std::weak_ptr<Identity> weak = identity;
        services::PresenceStateMachine machine(identity);
        ctx.machine = &machine;
        ctx.release = [&] { identity.reset(); };
        std::string results;
        for (const auto& step : ctx.command.get("steps").array) {
            try {
                machine.set_state(static_cast<services::PresenceState>(network::osd_integer(step)));
                results += "OK";
            } catch (const std::exception& e) {
                results += "EX:" + hex(e.what());
            }
            results += ":" + std::to_string(static_cast<int>(machine.get_state())) + ";";
        }
        std::cout << results << "|" << hex(ctx.trace) << "|" << (!weak.expired()) << "\n";
    }
}
