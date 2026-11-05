// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/agent_update_records.hpp"
#include "opensim/framework/integer_text.hpp"
#include <bit>
#include <stdexcept>
namespace opensim::framework {
using network::Value;
namespace {
Value uuid(const Uuid& id) {
    Value value(id.str());
    value.kind = Value::Kind::Uuid;
    return value;
}
const Value& map(const Value* data) {
    if (!data || data->view().kind != Value::Kind::Map)
        throw std::invalid_argument("Agent update record requires an OSD map");
    return data->view();
}
}
Value AgentGroupData::pack() const {
    auto data = Value::object({});
    data.set("group_id", uuid(group_id));
    data.set("group_powers", std::to_string(group_powers));
    data.set("accept_notices", Value(accept_notices));
    return data;
}
void AgentGroupData::unpack(const Value* input) {
    const auto& data = map(input);
    if (data.contains("group_id"))
        group_id = network::osd_uuid(data.get("group_id"));
    if (data.contains("group_powers"))
        try_parse_unsigned_integer(network::osd_string(data.get("group_powers")), group_powers);
    if (data.contains("accept_notices"))
        accept_notices = network::osd_boolean(data.get("accept_notices"));
}
Value ControllerData::pack() const {
    auto data = Value::object({});
    data.set("object", uuid(object_id));
    data.set("item", uuid(item_id));
    data.set("ignore", Value(std::bit_cast<std::int32_t>(ignore_controls)));
    data.set("event", Value(std::bit_cast<std::int32_t>(event_controls)));
    return data;
}
void ControllerData::unpack(const Value* input) {
    const auto& data = map(input);
    if (data.contains("object"))
        object_id = network::osd_uuid(data.get("object"));
    if (data.contains("item"))
        item_id = network::osd_uuid(data.get("item"));
    if (data.contains("ignore"))
        ignore_controls = static_cast<std::uint32_t>(network::osd_integer(data.get("ignore")));
    if (data.contains("event"))
        event_controls = static_cast<std::uint32_t>(network::osd_integer(data.get("event")));
}
}
