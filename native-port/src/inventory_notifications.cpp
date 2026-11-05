// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/inventory_notifications.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
#include <algorithm>
namespace opensim::scene {
namespace {
void uuid(std::string &out, std::string_view key, framework::Uuid value) {
    out += "<key>";
    out += key;
    out += "</key>";
    out += value.is_zero() ? "<uuid />" : "<uuid>" + value.str() + "</uuid>";
}
server::PollBytes removal(framework::Uuid agent, framework::Uuid session,
                          const std::vector<framework::Uuid> *ids, bool folders) {
    auto buffer =
        services::EventQueueModule::start_event(folders ? "RemoveInventoryFolder" : "RemoveInventoryItem");
    auto &xml = buffer->text;
    xml += "<key>AgentData</key><array><map>";
    uuid(xml, "AgentID", agent);
    uuid(xml, "SessionID", session);
    xml += "</map></array><key>";
    xml += folders ? "FolderData" : "InventoryData";
    xml += "</key><array>";
    if (!ids)
        throw std::invalid_argument("Null inventory removal array");
    for (auto id : *ids) {
        xml += "<map>";
        uuid(xml, folders ? "FolderID" : "ItemID", id);
        xml += "</map>";
    }
    xml += "</array>";
    return services::EventQueueModule::end_event(buffer);
}
std::vector<std::uint8_t> bytes(std::string_view value) {
    framework::utf16(value);
    auto nul = value.find('\0');
    if (nul != value.npos)
        value = value.substr(0, nul);
    auto n = std::min<std::size_t>(254, value.size());
    if (n < value.size())
        while (n && (static_cast<unsigned char>(value[n]) & 0xc0) == 0x80)
            --n;
    if (!n)
        return {};
    std::vector<std::uint8_t> out(value.begin(), value.begin() + n);
    out.push_back(0);
    return out;
}
}
server::PollBytes remove_inventory_items_event(framework::Uuid agent, framework::Uuid session,
                                               const std::vector<framework::Uuid> *items) {
    return removal(agent, session, items, false);
}
server::PollBytes remove_inventory_folders_event(framework::Uuid agent, framework::Uuid session,
                                                 const std::vector<framework::Uuid> *folders) {
    return removal(agent, session, folders, true);
}
void send_remove_inventory_items(framework::Uuid agent, framework::Uuid session,
                                 const std::vector<framework::Uuid> *items,
                                 const InventoryNotificationBackend &backend) {
    if (!backend.enqueue) {
        if (backend.debug)
            backend.debug("[LLCLIENT]: Null event queue");
        return;
    }
    backend.enqueue(remove_inventory_items_event(agent, session, items), agent);
}
void send_remove_inventory_folders(framework::Uuid agent, framework::Uuid session,
                                   const std::vector<framework::Uuid> *folders,
                                   const InventoryNotificationBackend &backend) {
    if (!backend.enqueue) {
        if (backend.debug)
            backend.debug("[LLCLIENT]: Null event queue");
        return;
    }
    backend.enqueue(remove_inventory_folders_event(agent, session, folders), agent);
}
network::lludp::Packet agent_alert(framework::Uuid agent, const std::optional<std::string> &input, bool modal,
                                   AlertPrefix prefix) {
    if (!prefix)
        prefix = framework::invariant_starts_with;
    auto message = input;
    if (!modal) {
        if (!message)
            throw std::invalid_argument("Null agent alert message");
        if (!prefix(*message, "ALERT: ") && !prefix(*message, "NOTIFY: ") &&
            *message != "Home position set." &&
            *message != "You died and have been teleported to your home location")
            message = "/" + *message;
    }
    auto packet = network::lludp::make_packet("AgentAlertMessage");
    packet.block("AgentData")["AgentID"] = agent;
    auto &data = packet.block("AlertData");
    data["Modal"] = modal;
    data["Message"] = bytes(message.value_or(""));
    return packet;
}
}
