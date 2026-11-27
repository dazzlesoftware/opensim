#include "opensim/scene/group_position.hpp"
#include "opensim/network/osd_json.hpp"
#include <bit>
#include <iostream>
using namespace opensim;

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto f = network::parse_osd_json(line);
        const auto vector = [&](const char *key) {
            const auto &a = f.get(key).array;
            const auto x = [](const auto &v) {
                return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(v.text)));
            };
            return framework::Vector3{x(a.at(0)), x(a.at(1)), x(a.at(2))};
        };
        scene::GroupPositionState s;
        s.position = vector("position");
        s.sandbox_origin = vector("origin");
        s.scene = f.get("scene").boolean;
        s.attached = f.get("attached").boolean;
        s.loading = f.get("loading").boolean;
        s.disable_transfer = f.get("disabled").boolean;
        s.in_transit = f.get("transit").boolean;
        s.sandboxed = f.get("sandbox").boolean;
        s.duplicating = f.get("duplicate").boolean;
        s.deleted = f.get("deleted").boolean;
        s.changed_events = f.get("changed").boolean;
        network::Value events;
        events.kind = network::Value::Kind::Array;
        auto event = [&](const char *text) { events.array.emplace_back(text); };
        scene::GroupPositionHost host;
        host.inside = [&](auto) {
            event("inside");
            return f.get("inside").boolean;
        };
        host.schedule_crossing = [&](auto) { event("cross"); };
        host.disable_physics = [&] { event("physicsOff"); };
        host.sandbox_debug = [&] { event("debug"); };
        host.write_positions = [&](auto) {
            event("root");
            event("child1");
            event("child2");
        };
        host.move_sitters = [&] {
            event("sitter1");
            event("sitter2");
        };
        host.changed_position = [&] {
            event("changed0");
            event("changed1");
            event("changed2");
        };
        host.taint_parcel_counts = [&] { event("taint"); };
        scene::set_group_position(s, vector("target"), host);
        network::Value pos;
        pos.kind = network::Value::Kind::Array;
        for (float x : {s.position.x, s.position.y, s.position.z})
            pos.array.emplace_back(std::to_string(std::bit_cast<std::uint32_t>(x)));
        std::cout << network::osd_json(
                         network::Value::object(
                             {{"position", pos}, {"transit", network::Value(s.in_transit)}, {"events", events}}),
                         true)
                  << '\n';
    }
}
