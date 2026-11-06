// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/event_queue.hpp"
#include "opensim/services/avatar_data.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <algorithm>
namespace opensim::services {
namespace {
using framework::Uuid;
using framework::Vector3;
using server::PollBytes;
using S = std::optional<std::string>;
template <class T> T& required(T* p) {
    if (!p)
        throw EventQueueNullReference();
    return *p;
}
template <class T> T& required(const std::shared_ptr<T>& p) {
    return required(p.get());
}
struct Xml {
    std::string s;
    void key(const std::string& key) {
        s += "<key>";
        for (auto c : framework::utf16_replacing(key))
            s += static_cast<char>(c & 255);
        s += "</key>";
    }
    void map() {
        s += "<map>";
    }
    void endmap() {
        s += "</map>";
    }
    void array() {
        s += "<array>";
    }
    void endarray() {
        s += "</array>";
    }
    void map(const std::string& n) {
        key(n);
        map();
    }
    void array(const std::string& n) {
        key(n);
        array();
    }
    void arraymap(const std::string& n) {
        array(n);
        map();
    }
    void endmaparray() {
        endmap();
        endarray();
    }
    void empty_map(const std::string& n) {
        key(n);
        s += "<map />";
    }
    void empty_array(const std::string& n) {
        key(n);
        s += "<array />";
    }
    void elem(const std::string& n, int32_t v) {
        key(n);
        s += v == 0 ? "<integer />" : "<integer>" + std::to_string(v) + "</integer>";
    }
    void elem(const std::string& n, bool v) {
        key(n);
        s += v ? "<boolean>1</boolean>" : "<boolean>0</boolean>";
    }
    void elem(const std::string& n, Uuid v) {
        key(n);
        s += v.is_zero() ? "<uuid />" : "<uuid>" + v.str() + "</uuid>";
    }
    void elem(const std::string& n, const S& v) {
        key(n);
        if (!v || v->empty()) {
            s += "<string />";
            return;
        }
        auto encoded = event_queue_inner_xml(network::Value(*v));
        s += encoded;
    }
    void elem(const std::string& n, const char* v) {
        elem(n, S(v));
    }
    void elem(const std::string& n, PollBytes v) {
        key(n);
        if (!v || v->empty()) {
            s += "<binary />";
            return;
        }
        s += "<binary>" + network::binary_base64(network::osd_from_binary(v)) + "</binary>";
    }
    void elem(const std::string& n, uint32_t v) {
        auto bytes = std::make_shared<std::vector<uint8_t>>(4);
        for (int i = 0; i < 4; ++i)
            (*bytes)[i] = uint8_t(v >> (24 - i * 8));
        elem(n, bytes);
    }
    void elem(const std::string& n, uint64_t v) {
        auto bytes = std::make_shared<std::vector<uint8_t>>(8);
        for (int i = 0; i < 8; ++i)
            (*bytes)[i] = uint8_t(v >> (56 - i * 8));
        elem(n, bytes);
    }
    void real(float v) {
        s += "<real>";
        s += v == 0 ? "0" : avatar_float_string(v);
        s += "</real>";
    }
    void elem(const std::string& n, float v) {
        key(n);
        real(v);
    }
    void elem(const std::string& n, Vector3 v) {
        array(n);
        real(v.x);
        real(v.y);
        real(v.z);
        endarray();
    }
};
Xml start(const S& name) {
    Xml x;
    x.map();
    x.elem("message", name);
    x.map("body");
    return x;
}
PollBytes finish(Xml& x) {
    x.endmap();
    x.endmap();
    return std::make_shared<std::vector<uint8_t>>(x.s.begin(), x.s.end());
}
PollBytes address(const std::shared_ptr<framework::IpEndpoint>& endpoint) {
    auto& ip = required(required(endpoint).address);
    auto data = ip.bytes();
    return std::make_shared<std::vector<uint8_t>>(data.begin(), data.begin() + (ip.v6() ? 16 : 4));
}
uint32_t uuid_crc(Uuid id) {
    const auto& b = id.bytes();
    uint32_t result = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
    result += (uint32_t(b[4]) << 8) | b[5] | (uint32_t(b[6]) << 24) | (uint32_t(b[7]) << 16);
    for (int i = 8; i < 16; i += 4)
        result += uint32_t(b[i]) | (uint32_t(b[i + 1]) << 8) | (uint32_t(b[i + 2]) << 16) |
                  (uint32_t(b[i + 3]) << 24);
    return result;
}
void generic_head(Xml& x, Uuid agent, std::optional<Uuid> transaction, std::optional<Uuid> session,
                  const S& method, Uuid invoice) {
    x.arraymap("AgentData");
    x.elem("AgentID", agent);
    if (transaction)
        x.elem("TransactionID", *transaction);
    if (session)
        x.elem("SessionID", *session);
    x.endmaparray();
    x.arraymap("MethodData");
    x.elem("Method", method);
    x.elem("Invoice", invoice);
    x.endmaparray();
    x.arraymap("ParamList");
}
}
std::shared_ptr<EventQueueByteBuffer> EventQueueModule::start_event(const S& name, int) {
    auto result = std::make_shared<EventQueueByteBuffer>();
    result->text = start(name).s;
    return result;
}
PollBytes EventQueueModule::end_event(std::shared_ptr<EventQueueByteBuffer> buffer) {
    required(buffer).text += "</map></map>";
    return buffer->to_array();
}
PollBytes EventQueueModule::build_event(const S& name, std::shared_ptr<network::Value> body) {
    required(body);
    auto map = network::Value::object({});
    map.set("message", network::Value(name.value_or("")));
    map.set("body", network::Value::alias(body));
    auto text = event_queue_inner_xml(map);
    return std::make_shared<std::vector<uint8_t>>(text.begin(), text.end());
}
std::string EventQueueModule::keep_alive_event() {
    auto x = start("FAKEEVENT");
    auto b = finish(x);
    return {b->begin(), b->end()};
}
void EventQueueModule::enable_simulator(uint64_t handle, std::shared_ptr<framework::IpEndpoint> endpoint,
                                        Uuid agent, int rx, int ry) {
    if (int32_t(debug_level) > 0)
        backend_.debug("[EVENT QUEUE GET MODULE] EnableSimulator. handle=" + std::to_string(handle) +
                       ", endPoint=" + (endpoint ? endpoint->str() : "") + ", avatarID=" + agent.str());
    auto x = start("EnableSimulator");
    x.arraymap("SimulatorInfo");
    x.elem("Handle", handle);
    x.elem("IP", address(endpoint));
    x.elem("Port", int32_t(required(endpoint).port));
    x.elem("RegionSizeX", uint32_t(rx));
    x.elem("RegionSizeY", uint32_t(ry));
    x.endmaparray();
    enqueue(finish(x), agent);
}
void EventQueueModule::establish_communication(Uuid agent, std::shared_ptr<framework::IpEndpoint> endpoint,
                                               const S& seed, uint64_t handle, int, int) {
    if (int32_t(debug_level) > 0)
        backend_.debug(
            "[EVENT QUEUE GET MODULE] EstablishAgentCommunication. handle=" + std::to_string(handle) +
            ", endPoint=" + (endpoint ? endpoint->str() : "") + ", avatarID=" + agent.str());
    auto x = start("EstablishAgentCommunication");
    x.elem("agent-id", agent);
    x.elem("sim-ip-and-port", S(required(endpoint).str()));
    x.elem("seed-capability", seed);
    enqueue(finish(x), agent);
}
void EventQueueModule::teleport_finish(uint64_t handle, uint8_t access,
                                       std::shared_ptr<framework::IpEndpoint> endpoint, uint32_t,
                                       uint32_t flags, const S& seed, Uuid agent, int rx, int ry) {
    if (int32_t(debug_level) > 0)
        backend_.debug("[EVENT QUEUE GET MODULE] TeleportFinishEvent. handle=" + std::to_string(handle) +
                       ", endPoint=" + (endpoint ? endpoint->str() : "") + ", avatarID=" + agent.str());
    flags = 16 | (flags & 8192);
    auto x = start("TeleportFinish");
    x.arraymap("Info");
    x.elem("AgentID", agent);
    x.elem("LocationID", uint32_t(4));
    x.elem("SimIP", address(endpoint));
    x.elem("SimPort", int32_t(required(endpoint).port));
    x.elem("RegionHandle", handle);
    x.elem("SeedCapability", seed);
    x.elem("SimAccess", int32_t(access));
    x.elem("TeleportFlags", flags);
    x.elem("RegionSizeX", uint32_t(rx));
    x.elem("RegionSizeY", uint32_t(ry));
    x.endmaparray();
    enqueue(finish(x), agent);
}
void EventQueueModule::cross_region(uint64_t handle, Vector3 position, Vector3 look,
                                    std::shared_ptr<framework::IpEndpoint> endpoint, const S& seed,
                                    Uuid agent, Uuid session, int rx, int ry) {
    if (int32_t(debug_level) > 0)
        backend_.debug("[EVENT QUEUE GET MODULE] CrossRegion. handle=" + std::to_string(handle) +
                       ", avatarID=" + agent.str() + ", regionSize=" + std::to_string(rx) + "," +
                       std::to_string(ry) + ">");
    auto x = start("CrossedRegion");
    x.arraymap("AgentData");
    x.elem("AgentID", agent);
    x.elem("SessionID", session);
    x.endmaparray();
    x.arraymap("Info");
    x.elem("LookAt", look);
    x.elem("Position", position);
    x.endmaparray();
    x.arraymap("RegionData");
    x.elem("RegionHandle", handle);
    x.elem("SeedCapability", seed);
    x.elem("SimIP", address(endpoint));
    x.elem("SimPort", int32_t(required(endpoint).port));
    x.elem("RegionSizeX", uint32_t(rx));
    x.elem("RegionSizeY", uint32_t(ry));
    x.endmaparray();
    enqueue(finish(x), agent);
}
void EventQueueModule::chatterbox_invitation(Uuid session, const S& session_name, Uuid from, const S& message,
                                             Uuid to, const S& from_name, uint8_t dialog, uint32_t timestamp,
                                             bool offline, int estate, Vector3 position, uint32_t ttl,
                                             Uuid transaction, bool group, PollBytes bucket) {
    auto x = start("ChatterBoxInvitation");
    x.elem("session_id", session);
    x.elem("from_name", from_name);
    x.elem("session_name", session_name);
    x.elem("from_id", from);
    auto inner_start = x.s.size();
    x.map("instantmessage");
    x.map("message_params");
    x.elem("type", int32_t(dialog));
    x.elem("position", position);
    x.elem("region_id", Uuid{});
    x.elem("to_id", to);
    x.elem("source", int32_t(0));
    x.map("data");
    x.elem("binary_bucket", bucket);
    x.endmap();
    x.elem("message", message);
    x.elem("id", transaction);
    x.elem("from_name", from_name);
    x.elem("timestamp", timestamp);
    x.elem("offline", int32_t(offline ? 1 : 0));
    x.elem("parent_estate_id", int32_t(estate));
    x.elem("ttl", std::bit_cast<int32_t>(ttl));
    x.elem("from_id", from);
    x.elem("from_group", group);
    x.endmap();
    x.map("agent_params");
    x.elem("agent_id", from);
    x.elem("check_estate", true);
    x.elem("god_level", int32_t(0));
    x.elem("limited_to_estate", true);
    x.endmap();
    x.endmap();
    x.s.erase(std::remove(x.s.begin() + static_cast<std::ptrdiff_t>(inner_start), x.s.end(), char(0)),
              x.s.end());
    enqueue(finish(x), to);
}
void EventQueueModule::chatterbox_updates(Uuid session, Uuid to,
                                          const std::vector<EventGroupMember>* updates) {
    auto x = start("ChatterBoxSessionAgentListUpdates");
    x.map("agent_updates");
    for (auto& u : required(updates)) {
        x.map(u.agent.str());
        x.map("info");
        x.elem("can_voice_chat", u.voice);
        x.elem("is_moderator", u.moderator);
        x.map("mutes");
        x.elem("text", u.muted_text);
        x.endmap();
        x.endmap();
        x.elem("transition", u.enter ? "ENTER" : "LEAVE");
        x.endmap();
    }
    x.endmap();
    x.empty_map("updates");
    x.elem("session_id", session);
    enqueue(finish(x), to);
}
void EventQueueModule::chatterbox_start(Uuid session, const S& name, int type, bool voice, bool moderated,
                                        Uuid temporary, bool success, const S& error, Uuid to) {
    auto x = start("ChatterBoxSessionStartReply");
    x.elem("session_id", session);
    x.elem("temp_session_id", temporary);
    x.elem("success", success);
    if (success) {
        x.map("session_info");
        x.map("moderated_mode");
        x.elem("voice", moderated);
        x.endmap();
        x.elem("session_name", name);
        x.elem("type", int32_t(type));
        x.elem("voice_enabled", voice);
        x.endmap();
    } else
        x.elem("error", error);
    enqueue(finish(x), to);
}
void EventQueueModule::chatterbox_close(Uuid to, Uuid session, const S& reason) {
    auto x = start("ForceCloseChatterBoxSession");
    x.elem("session_id", session);
    x.elem("reason", reason);
    enqueue(finish(x), to);
}
void EventQueueModule::group_membership(Uuid agent,
                                        const std::vector<std::shared_ptr<EventGroupMembership>>* groups) {
    auto x = start("AgentGroupDataUpdate");
    x.arraymap("AgentData");
    x.elem("AgentID", agent);
    x.endmaparray();
    if (required(groups).empty()) {
        x.empty_array("GroupData");
        x.empty_array("NewGroupData");
    } else {
        x.array("GroupData");
        std::vector<bool> profiles;
        for (auto& p : *groups) {
            auto& g = required(p);
            x.map();
            x.elem("GroupID", g.group);
            x.elem("GroupPowers", g.powers);
            x.elem("AcceptNotices", g.notices);
            x.elem("GroupInsigniaID", g.picture);
            x.elem("Contribution", g.contribution);
            x.elem("GroupName", g.name);
            x.endmap();
            profiles.push_back(g.profile);
        }
        x.endarray();
        x.array("NewGroupData");
        for (bool profile : profiles) {
            x.map();
            x.elem("ListInProfile", profile);
            x.endmap();
        }
        x.endarray();
    }
    enqueue(finish(x), agent);
}
void EventQueueModule::places_reply(Uuid agent, Uuid query, Uuid transaction,
                                    const std::vector<std::shared_ptr<EventPlace>>* places) {
    Xml x;
    x.map();
    x.elem("message", "PlacesReplyMessage");
    x.map("QueryData[]");
    x.array();
    x.arraymap("AgentData");
    x.elem("AgentID", agent);
    x.elem("QueryID", query);
    x.elem("TransactionID", transaction);
    x.endmaparray();
    x.array("QueryData");
    for (auto& p : required(places)) {
        auto& v = required(p);
        x.map();
        x.elem("ActualArea", v.actual_area);
        x.elem("BillableArea", v.billable_area);
        x.elem("Description", v.description);
        x.elem("Dwell", v.dwell);
        x.elem("Flags", int32_t(v.flags));
        x.elem("GlobalX", v.x);
        x.elem("GlobalY", v.y);
        x.elem("GlobalZ", v.z);
        x.elem("Name", v.name);
        x.elem("OwnerID", v.owner);
        x.elem("SimName", v.sim_name);
        x.elem("SnapShotID", v.snapshot);
        x.elem("ProductSku", int32_t(0));
        x.elem("Price", v.price);
        x.endmap();
    }
    x.endarray();
    enqueue(finish(x), agent);
}
void EventQueueModule::script_running(Uuid object, Uuid item, bool running, Uuid agent) {
    auto x = start("ScriptRunningReply");
    x.arraymap("Script");
    x.elem("ObjectID", object);
    x.elem("ItemID", item);
    x.elem("Running", running);
    x.elem("Mono", true);
    x.endmaparray();
    enqueue(finish(x), agent);
}
void EventQueueModule::physics_properties(uint32_t local, uint8_t shape, float density, float friction,
                                          float bounce, float gravity, Uuid agent) {
    auto x = start("ObjectPhysicsProperties");
    x.arraymap("ObjectData");
    x.elem("LocalID", std::bit_cast<int32_t>(local));
    x.elem("Density", density);
    x.elem("Friction", friction);
    x.elem("GravityMultiplier", gravity);
    x.elem("Restitution", bounce);
    x.elem("PhysicsShapeType", int32_t(shape));
    x.endmaparray();
    enqueue(finish(x), agent);
}
void EventQueueModule::windlight_refresh(int interpolate, Uuid agent) {
    auto x = start("WindLightRefresh");
    x.elem("Interpolate", int32_t(interpolate > 0 ? 1 : 0));
    enqueue(finish(x), agent);
}
void EventQueueModule::bulk_inventory(const InventoryItem* item, Uuid agent,
                                      std::optional<Uuid> transaction) {
    auto x = start("BulkUpdateInventory");
    x.arraymap("AgentData");
    x.elem("AgentID", agent);
    x.elem("TransactionID", transaction ? *transaction : backend_.random_uuid());
    x.endmaparray();
    x.s +=
        "<key>FolderData</key><array><map>"
        "<key>FolderID</key><uuid>00000000-0000-0000-0000-000000000000</uuid>"
        "<key>Name</key><string></string>"
        "<key>ParentID</key><uuid>00000000-0000-0000-0000-000000000000</uuid >"
        "<key>Type</key ><integer>-1</integer></map ></array>";
    auto& v = required(item);
    auto trim = [](const std::string& s) {
        auto text = framework::utf8(framework::utf16_replacing(s));
        auto nul = text.find(char(0));
        if (nul != std::string::npos)
            text.resize(nul);
        if (text.size() > 255) {
            size_t end = 255;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
                --end;
            text.resize(end);
        }
        return text;
    };
    x.arraymap("ItemData");
    x.elem("ItemID", v.id);
    x.elem("AssetID", v.asset_id);
    Uuid creator;
    framework::try_parse_sdk_uuid(v.creator_id, creator);
    x.elem("CreatorID", creator);
    x.elem("BaseMask", uint32_t(v.base_permissions));
    x.elem("CreationDate", int32_t(v.creation_date));
    x.elem("Description", S(trim(v.description)));
    x.elem("EveryoneMask", uint32_t(v.everyone_permissions));
    x.elem("FolderID", v.parent);
    auto inv = std::bit_cast<int8_t>(uint8_t(v.inventory_type)),
         type = std::bit_cast<int8_t>(uint8_t(v.asset_type));
    x.elem("InvType", int32_t(inv));
    x.elem("Name", S(trim(v.name)));
    x.elem("NextOwnerMask", uint32_t(v.next_permissions));
    x.elem("GroupID", v.group_id);
    x.elem("GroupMask", uint32_t(v.group_permissions));
    x.elem("GroupOwned", v.group_owned != 0);
    x.elem("OwnerID", v.owner);
    x.elem("OwnerMask", uint32_t(v.current_permissions));
    x.elem("SalePrice", int32_t(v.sale_price));
    x.elem("SaleType", int32_t(uint8_t(v.sale_type)));
    x.elem("Type", int32_t(type));
    x.elem("CallbackID", uint32_t(0));
    x.elem("Flags", uint32_t(v.flags) & 0x2000ff);
    uint32_t crc = uuid_crc(v.asset_id) + uuid_crc(v.parent) + uuid_crc(v.id) + uuid_crc(creator) +
                   uuid_crc(v.owner) + uuid_crc(v.group_id) + 4u * 0x7ffffffu + 1u + uint32_t(int32_t(inv)) +
                   uint32_t(int32_t(type)) + 1100u;
    x.elem("CRC", crc);
    x.endmaparray();
    enqueue(finish(x), agent);
}
void EventQueueModule::large_message(Uuid agent, std::optional<Uuid> transaction, std::optional<Uuid> session,
                                     const S& method, Uuid invoice, const std::vector<S>* messages) {
    auto x = start("LargeGenericMessage");
    generic_head(x, agent, transaction, session, method, invoice);
    for (auto& message : required(messages))
        x.elem("Parameter", message);
    x.endmaparray();
    enqueue(finish(x), agent);
}
void EventQueueModule::large_message_bytes(Uuid agent, std::optional<Uuid> transaction,
                                           std::optional<Uuid> session, const S& method, Uuid invoice,
                                           const std::vector<PollBytes>* messages) {
    auto x = start("LargeGenericMessage");
    generic_head(x, agent, transaction, session, method, invoice);
    for (auto& message : required(messages)) {
        x.s += "<key>Parameter</key><string>";
        auto& data = required(message);
        x.s.append(data.begin(), data.end());
        x.s += "</string>";
    }
    x.endmaparray();
    enqueue(finish(x), agent);
}
}
