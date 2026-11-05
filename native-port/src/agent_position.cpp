// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/agent_position.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/framework/integer_text.hpp"
#include <stdexcept>
namespace opensim::framework {
using network::Value;
network::Value AgentPosition::pack(EntityTransferContext*) const {
    auto args = Value::object({});
    args.set("message_type", "AgentPosition");
    args.set("region_handle", std::to_string(region_handle));
    args.set("circuit_code", std::to_string(circuit_code));
    auto uuid = [](const Uuid& id) {
        Value value(id.str());
        value.kind = Value::Kind::Uuid;
        return value;
    };
    args.set("agent_uuid", uuid(agent_id));
    args.set("session_uuid", uuid(session_id));
    for (auto [key, vector] : {std::pair{"position", position},
                               {"velocity", velocity},
                               {"center", center},
                               {"size", size},
                               {"at_axis", at_axis},
                               {"left_axis", left_axis},
                               {"up_axis", up_axis}})
        args.set(key, vector3_string(vector));
    Value distance;
    distance.kind = Value::Kind::Real;
    distance.real = far;
    args.set("far", distance);
    args.set("changed_grid", Value(changed_grid));
    if (god_data) {
        args.set("god_data", Value::alias(god_data));
        const auto& god = god_data->view();
        if (god.kind != Value::Kind::Map)
            throw std::invalid_argument("GodData requires an OSD map");
        if (god.contains("ViewerUiIsGod"))
            args.set("god_level", Value(network::osd_boolean(god.get("ViewerUiIsGod")) ? 200 : 0));
    }
    if (throttles && !throttles->empty())
        args.set("throttles", network::osd_from_binary(throttles));
    if (children_cap_seeds && !children_cap_seeds->empty()) {
        Value seeds;
        seeds.kind = Value::Kind::Array;
        for (const auto& [handle, seed] : *children_cap_seeds) {
            auto pair = Value::object({});
            pair.set("handle", std::to_string(handle));
            pair.set("seed", seed.value_or(""));
            seeds.array.push_back(std::move(pair));
        }
        args.set("children_seeds", std::move(seeds));
    }
    return args;
}
void AgentPosition::unpack(Value& input, EntityTransferContext*) {
    if (input.reference) {
        unpack(*input.reference);
        return;
    }
    if (input.kind != Value::Kind::Map)
        throw std::invalid_argument("AgentPosition requires an OSD map");
    auto& args = input;
    if (args.contains("region_handle"))
        try_parse_unsigned_integer(network::osd_string(args.get("region_handle")), region_handle);
    if (args.contains("circuit_code"))
        try_parse_unsigned_integer(network::osd_string(args.get("circuit_code")), circuit_code);
    if (args.contains("agent_uuid"))
        agent_id = network::osd_uuid(args.get("agent_uuid"));
    if (args.contains("session_uuid"))
        session_id = network::osd_uuid(args.get("session_uuid"));
    auto vector = [&](const char* key, Vector3& value) {
        if (args.contains(key))
            parse_vector3(network::osd_string(args.get(key)), value);
    };
    vector("position", position);
    vector("velocity", velocity);
    vector("center", center);
    vector("size", size);
    vector("at_axis", at_axis);
    vector("left_axis", left_axis);
    vector("up_axis", up_axis);
    if (args.contains("changed_grid"))
        changed_grid = network::osd_boolean(args.get("changed_grid"));
    if (args.contains("god_data")) {
        auto& value = args.map.at("god_data");
        if (!value.reference) {
            auto node = std::make_shared<Value>(std::move(value));
            value = Value::alias(node);
        }
        god_data = value.reference;
    }
    if (args.contains("far"))
        far = static_cast<float>(network::osd_real(args.get("far")));
    if (args.contains("throttles"))
        throttles = network::osd_binary(args.get("throttles"));
    if (args.contains("children_seeds")) {
        const auto& seeds = args.get("children_seeds").view();
        if (seeds.kind == Value::Kind::Array) {
            children_cap_seeds = std::make_shared<ChildrenSeeds>();
            for (const auto& item : seeds.array) {
                const auto& pair = item.view();
                if (pair.kind != Value::Kind::Map || !pair.contains("handle"))
                    continue;
                std::uint64_t handle;
                if (!try_parse_unsigned_integer(network::osd_string(pair.get("handle")), handle))
                    continue;
                if (!children_cap_seeds->contains(handle))
                    (*children_cap_seeds)[handle] =
                        pair.contains("seed") ? network::osd_string(pair.get("seed")) : "";
            }
        }
    }
}
void AgentPosition::copy_from(const ChildAgentDataUpdate* child, const Uuid& session) {
    if (!child)
        throw std::invalid_argument("Null child agent update");
    agent_id = child->agent_id;
    session_id = session;
    size = {0, 0, child->avatar_height};
    center = child->camera_position;
    far = child->draw_distance;
    position = child->position;
    region_handle = child->region_handle;
    throttles = child->throttles;
    velocity = child->velocity;
}
}
