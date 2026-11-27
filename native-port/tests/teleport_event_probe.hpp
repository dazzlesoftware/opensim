#pragma once
#include "opensim/scene/region_runtime.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include <optional>
#include <stdexcept>

namespace opensim::tests {
inline std::optional<std::uint64_t> take_teleport_event(scene::RegionRuntime &runtime, framework::Uuid agent) {
    const auto caps = runtime.capabilities();
    const auto events = caps ? caps->event_queue() : nullptr;
    const auto request = framework::Uuid::random();
    if (!events || !events->has_events(request, agent))
        return std::nullopt;
    const auto reply = events->get_events(request, agent);
    if (!reply || !reply->contains("bin_response_data")) {
        if (reply && std::any_cast<std::int32_t>(reply->get("int_response_code")) == 404)
            throw std::runtime_error("Missing viewer EventQueue: " + agent.str());
        return std::nullopt;
    }
    const auto bytes = std::any_cast<server::PollBytes>(reply->get("bin_response_data"));
    const std::string xml(bytes->begin(), bytes->end());
    const auto batch = network::deserialize_sdk_llsd_xml(xml);
    if (batch.kind != network::Value::Kind::Map)
        throw std::runtime_error("Invalid viewer event LLSD");
    for (const auto &event : batch.get("events").array) {
        if (event.get("message").as_string() != "TeleportFinish")
            continue;
        const auto info = event.get("body").get("Info").array.at(0);
        const auto handle = info.get("RegionHandle");
        const auto encoded = network::osd_binary(handle);
        if (info.get("AgentID").as_string() != agent.str() || handle.kind != network::Value::Kind::Binary || !encoded ||
            encoded->size() != 8)
            throw std::runtime_error("Teleport event has invalid identity or handle encoding");
        std::uint64_t value = 0;
        for (const auto byte : *encoded)
            value = (value << 8) | byte;
        return value;
    }
    if (xml.find("TeleportFinish") != std::string::npos)
        throw std::runtime_error("Missing parsed teleport event");
    return std::nullopt;
}
} // namespace opensim::tests
