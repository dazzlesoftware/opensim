#include "opensim/scene/object_teleport.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include <bit>
#include <iostream>
#include <optional>
#include <stdexcept>
using namespace opensim;

network::Value bits(const scene::ObjectTeleportMotion &m) {
    network::Value out;
    out.kind = network::Value::Kind::Array;
    for (float f : {m.velocity.x, m.velocity.y, m.velocity.z, m.angular_velocity.x, m.angular_velocity.y,
                    m.angular_velocity.z, m.acceleration.x, m.acceleration.y, m.acceleration.z, m.orientation.x,
                    m.orientation.y, m.orientation.z, m.orientation.w})
        out.array.emplace_back(std::to_string(std::bit_cast<std::uint32_t>(f)));
    return out;
}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto f = network::parse_osd_json(line);
        scene::ObjectTeleportState state;
        state.id = framework::Uuid("11111111-1111-4111-8111-111111111111");
        const auto source = framework::Uuid("22222222-2222-4222-8222-222222222222");
        const auto initial = f.get("state").text;
        state.in_transit = initial == "busy";
        state.deleted = initial == "deleted";
        state.attached = initial == "attached";
        state.selected = initial == "selected";
        state.scene_available = initial != "noscene";
        state.keyframe_motion = initial == "keyframe";
        state.position = {12, 13, 14};
        state.scale = {2, 4, 6};
        state.motion = {{1, 2, 3}, {-.1f, .2f, -.3f}, {.01f, .02f, .03f}, {0, 0, .6f, .8f}};
        if (f.get("seats").boolean)
            state.seated_avatars = {state.id, source};
        network::Value events;
        events.kind = network::Value::Kind::Array;
        const auto event = [&](std::string name) { events.array.emplace_back(std::move(name)); };
        const auto fail = [&](std::string name) {
            if (f.get("throws").text == name)
                throw std::runtime_error(name);
        };
        auto current = std::make_shared<scene::ObjectTeleportParcel>();
        auto target = std::make_shared<scene::ObjectTeleportParcel>();
        const auto parcels = f.get("parcels").text;
        if (parcels == "same")
            target = current;
        if (parcels == "no-current")
            current.reset();
        if (parcels == "no-target")
            target.reset();
        if (parcels == "none") {
            current.reset();
            target.reset();
        }
        if (target) {
            target->restricted = [&](auto id) {
                const int n = id == state.id ? 0 : 1;
                event("restricted" + std::to_string(n));
                fail("restricted" + std::to_string(n));
                return f.get("restricted").integer == n;
            };
            target->banned = [&](auto id) {
                const int n = id == state.id ? 0 : 1;
                event("banned" + std::to_string(n));
                fail("banned" + std::to_string(n));
                return f.get("banned").integer == n;
            };
        }
        int land_queries = 0;
        std::optional<scene::ObjectTeleportCrossing> planned;
        scene::ObjectTeleportHost host;
        host.inside_region = [&](auto) {
            event("inside");
            fail("inside");
            return f.get("inside").boolean;
        };
        host.cooldown = [&](auto object, auto caller, int timeout) {
            if (object != state.id || caller != source)
                throw std::runtime_error("Cooldown argument order");
            event("cooldown" + std::to_string(timeout));
            fail("cooldown");
            return f.get("cooldown").boolean;
        };
        host.land_at = [&](auto, auto) {
            event("land");
            fail("land");
            return land_queries++ == 0 ? current : target;
        };
        host.can_enter_with_scripts = [&](const auto &) {
            event("entry");
            fail("entry");
            return f.get("enter").boolean;
        };
        host.ground_height = [&](auto, auto) {
            event("ground");
            fail("ground");
            return float(network::osd_real(f.get("ground")));
        };
        host.terse_update = [&] {
            event("update");
            fail("update");
        };
        host.schedule_crossing = [&](const auto &request) {
            planned = request;
            event("queue");
            fail("queue");
        };
        auto result = network::Value::object({{"outcome", "return"}});
        try {
            result.map["result"] =
                network::Value(scene::teleport_object(state, source, {25, 27, float(network::osd_real(f.get("z")))},
                                                      {.2f, .1f, .3f, .7f}, f.get("flags").integer, host));
        } catch (const std::exception &) {
            result.map["outcome"] = network::Value("throw");
        }
        result.map["inTransit"] = network::Value(state.in_transit);
        result.map["motion"] = bits(state.motion);
        network::Value pos;
        pos.kind = network::Value::Kind::Array;
        for (float v : {state.position.x, state.position.y, state.position.z})
            pos.array.emplace_back(std::to_string(std::bit_cast<std::uint32_t>(v)));
        result.map["position"] = std::move(pos);
        result.map["events"] = std::move(events);
        result.map["planned"] = planned ? bits(planned->motion) : network::Value{};
        result.map["plannedFlags"] = planned ? network::Value(planned->flags) : network::Value{};
        result.map["plannedSource"] = planned ? network::Value(planned->source.str()) : network::Value{};
        std::cout << network::osd_json(result, true) << '\n';
    }
}
