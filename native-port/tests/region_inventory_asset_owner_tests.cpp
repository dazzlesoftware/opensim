#include "opensim/framework/agent_data.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/texture_entry.hpp"
// Actual region admission/UDP/scene-owner asset update/VM retention and shutdown.
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/scene/region_runtime.hpp"
#include "opensim/services/asset_store.hpp"
#include "opensim/services/avatar_service.hpp"
#include "opensim/services/sqlite_avatar_plugin.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>
#include <future>
#include <iostream>
#include <set>
#include <source_location>
#include <thread>
using namespace opensim;
using namespace scene;
using namespace services;
using framework::Uuid;
namespace {
void check(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
std::vector<std::uint8_t> serialize_object_asset(const ObjectAssetPart& root,
                                                 std::span<const ObjectAssetPart> children = {}) {
    std::u16string xml;
    framework::XmlTextWriterIo io;
    io.encoding = [] { return std::shared_ptr<framework::NativeXmlTextEncoding>{}; };
    io.write = [&](auto, auto text) {
        if (text)
            xml.append(*text);
    };
    io.write_array = [&](auto data, int offset, int count) {
        xml.append(data->data() + offset, count);
    };
    io.write_whole_array = [&](auto data) {
        xml.append(data->data(), data->size());
    };
    framework::NativeXmlTextWriter writer(std::move(io));
    write_object_asset_xml(writer, root, children);
    const auto utf8 = framework::utf8(xml);
    return {utf8.begin(), utf8.end()};
}
void check_attachment_asset_round_trip() {
    ObjectAssetPart attachment;
    attachment.object.id = Uuid::random();
    attachment.object.shape.state = 5;
    attachment.last_attachment_point = 6;
    attachment.attachment_position = {.25f, -.5f, .75f};
    const auto restored = read_object_asset_xml(serialize_object_asset(attachment));
    check(restored && restored->linksets.size() == 1 && restored->linksets.front().parts.size() == 1,
          "read serialized attachment");
    const auto& part = restored->linksets.front().parts.front();
    check(part.object.shape.state == 5, "preserve current attachment point");
    check(part.last_attachment_point == 6, "preserve last attachment point");
    check(part.attachment_position == attachment.attachment_position, "preserve saved attachment offset");
}
std::string path(const std::filesystem::path &p) {
    auto u = p.u8string();
    return {reinterpret_cast<const char *>(u.data()), u.size()};
}
std::vector<std::uint8_t> wire(const std::string &text) {
    std::vector<std::uint8_t> data(text.begin(), text.end());
    data.push_back(0);
    return data;
}
std::string field(const network::lludp::Packet &p, const char *block, const char *key) {
    auto b = std::get<std::vector<std::uint8_t>>(p.block(block).at(key));
    return {b.begin(), std::find(b.begin(), b.end(), 0)};
}
void run(bool wear_replaces_all, bool attachments_enabled = true) {
    auto directory =
        std::filesystem::temp_directory_path() / ("opensim-region-asset-owner-" + Uuid::random().str());
    check(std::filesystem::create_directory(directory), "unique directory");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            if (path.parent_path() == std::filesystem::temp_directory_path() &&
                path.filename().string().starts_with("opensim-region-asset-owner-")) {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        }
    } cleanup{directory};
    InventoryFolderStore folders(path(directory / "inventory.db"), true);
    InventoryService inventory(folders);
    StoredAssetService assets(std::make_shared<SqliteAssetStore>(path(directory / "assets.db"), true));
    auto owner = std::this_thread::get_id();
    int task_stores = 0, agent_stores = 0, script_checks = 0;
    Uuid script;
    auto circuit = std::make_shared<framework::AgentCircuitData>();
    circuit->agent_id = Uuid::random();
    circuit->session_id = Uuid::random();
    circuit->secure_session_id = Uuid::random();
    circuit->circuit_code = 42;
    circuit->caps_path = "asset-owner";
    circuit->start_position = {128, 128, 70};
    circuit->ip_address = "127.0.0.1";
    circuit->first_name = "Native";
    circuit->last_name = "Owner";
    circuit->appearance = std::make_shared<framework::AvatarAppearance>();
    circuit->appearance->set_size({0.5f, 0.75f, 1.875f});
    inventory.create_user_inventory(circuit->agent_id);
    InventoryItem note;
    note.id = Uuid::random();
    note.owner = circuit->agent_id;
    note.creator_id = circuit->agent_id.str();
    note.parent = inventory.root(circuit->agent_id)->id;
    note.name = "Agent note";
    note.description = "Description";
    note.asset_type = 7;
    note.inventory_type = 263;
    note.current_permissions = 0x9e000;
    check(inventory.add_item(note), "real note");
    InventoryItem disabled_wear;
    if (!attachments_enabled) {
        const std::string xml =
            "<SceneObjectGroup><SceneObjectPart><UUID>" + Uuid::random().str() +
            "</UUID><Shape><PCode>9</PCode><LastAttachPoint>5</LastAttachPoint></Shape>"
            "</SceneObjectPart></SceneObjectGroup>";
        auto asset = std::make_shared<framework::AssetBase>(Uuid::random(), "Disabled wear fixture", 6, "");
        asset->data = std::make_shared<std::vector<std::uint8_t>>(xml.begin(), xml.end());
        check(assets.store(asset).has_value(), "store disabled attachment asset");
        disabled_wear.id = Uuid::random();
        disabled_wear.owner = circuit->agent_id;
        disabled_wear.parent = note.parent;
        disabled_wear.asset_id = asset->full_id();
        disabled_wear.asset_type = disabled_wear.inventory_type = 6;
        check(inventory.add_item(disabled_wear), "store disabled viewer wear item");
        auto login_wear = disabled_wear;
        login_wear.id = Uuid::random();
        check(inventory.add_item(login_wear), "store disabled login wear item");
        circuit->appearance->set_attachment(5, login_wear.id, login_wear.asset_id);
    }
    const std::string source =
        R"LSL(
integer saw_camera;
integer viewer_listener;
integer wear_count;
integer inventory_rez;

default
{
    on_rez(integer parameter)
    {
        inventory_rez = parameter == 0 && llGetStartParameter() == 0 && llGetAttached() == 5;
    }

    attach(key avatar)
    {
        if (avatar == NULL_KEY && llGetAttached() == 6 && llGetLinkNumber() == 2)
            llSay(0, "Child received detach");
        if (avatar == NULL_KEY && llGetAttached() == 0 && llGetLinkNumber() == 2)
            llSay(0, "Child received drop");
        if (avatar == NULL_KEY && llGetAttached() == 5 && llGetLinkNumber() == 2)
        {
            ++wear_count;
            llSetObjectDesc("Edited by detach event");
            llSay(0, "Inventory attachment child received detach");
        }
        if (avatar == llGetOwner() && llGetAttached() == 5 && llGetLinkNumber() == 2)
        {
            if (inventory_rez)
                llSay(0, "Inventory on_rez preceded attach");
            inventory_rez = FALSE;
            ++wear_count;
            if (wear_count == 2)
                llSay(0, "Inventory attachment resumed its script");
            else if (wear_count == 4)
                llSay(0, "Inventory attachment resumed after manual detach");
            else if (wear_count == 6)
                llSay(0, "Inventory attachment resumed after replacement");
            else
                llSay(0, "Inventory attachment child received wear");
        }
    }

    listen(integer channel, string name, key sender, string message)
    {
        if (channel == 4711 && sender == llGetOwner())
        {
            llListenRemove(viewer_listener);
            llRegionSay(4712, "Region listener reply");
            llRegionSayTo(sender, 0, "Direct viewer chat");
            llRegionSayTo("00000000-0000-4000-8000-000000000127", 0, "Direct prim chat");
            llRegionSayTo(sender, DEBUG_CHANNEL, "Rejected direct debug");
        }
        if (channel == 4712)
            llSay(0, "Region listener received");
        if (channel == 0 && message == "Direct prim chat")
            llSay(0, "Direct prim listener received");
        if (channel == 4714)
            llRegionSayTo(sender, 4713, "Direct attachment chat");
        if (channel == 4713)
        {
            if (llGetAttached() == 6 && llGetLinkNumber() == 2)
                llSay(0, "Direct attachment listener received");
            else
                llSay(0, "Direct attachment listener leaked");
        }
    }

    state_entry()
    {
        if (llGetAttached() == 6)
            llSetTimerEvent(0.05);

        if (llGetAttached() == 0
            && llGetLinkNumber() == 0
            && llGetNumberOfPrims() == 1
            && llGetLocalPos() == llGetPos()
            && llGetRootPosition() == llGetPos()
            && llGetListLength(llGetAttachedList(llGetOwner())) == 1
            && llList2String(llGetAttachedList(llGetOwner()), 0) == "00000000-0000-4000-8000-000000000124"
            && llList2String(llGetAttachedList(NULL_KEY), 0) == "NOT_FOUND"
            && llGetListEntryType(llGetAttachedList(llGetOwner()), 0) == TYPE_KEY
            && llGetListEntryType(llGetAttachedList(NULL_KEY), 0) == TYPE_STRING
            && llList2String(llGetAttachedList("00000000-0000-4000-8000-000000000456"), 0) == "NOT_FOUND")
        {
            llListen(4712, "", NULL_KEY, "Region listener reply");
            llListen(0, "", NULL_KEY, "Direct prim chat");
            llListen(4713, "", NULL_KEY, "Direct attachment chat");
            llWhisper(0, "Out of whisper range");
            llShout(0, "In shout range");
            llSay(0, "Script running");
        }
        else if (llGetAttached() == 6
                 && llGetObjectPrimCount(llGetKey()) == 0
                 && llGetLocalPos() == ZERO_VECTOR
                 && llGetLocalRot() == <0, 0, 1, 0>
                 && llGetRot() == ZERO_ROTATION
                 && llGetRootRotation() == llGetRot()
                 && llGetPos() != ZERO_VECTOR
                 && llGetRootPosition() == llGetPos()
                 && !llSameGroup(llGetKey()))
            llSay(0, "Attachment point six");

        if (llGetLinkNumber() == 2
            && llGetLinkKey(2) == llGetKey()
            && llGetLinkKey(LINK_THIS) == llGetKey()
            && llGetLinkKey(1) == "00000000-0000-4000-8000-000000000123"
            && llGetLinkKey(3) == NULL_KEY
            && llGetLinkName(1) == "Linked root"
            && llGetNumberOfSides() == 6
            && llGetLinkNumberOfSides(1) == 6
            && llGetLinkNumberOfSides(3) == 0
            && llGetLinkName(LINK_THIS) == "Linked child"
            && llGetLinkName(3) == (string)NULL_KEY
            && llGetNumberOfPrims() == 2
            && llGetVel() == <0, 2, 0>
            && llGetObjectPrimCount(llGetKey()) == 2
            && llGetObjectPrimCount(NULL_KEY) == 0
            && llGetLocalPos() == <1, 0, 0>
            && llGetPos() == llGetRootPosition() + <-1, 0, 0>
            && llGetLocalRot() == <1, 0, 0, 0>
            && llGetRootRotation() == <0, 0, 1, 0>
            && llGetRot() == <0, 1, 0, 0>)
        {
            viewer_listener = llListen(4711, "", llGetOwner(), "Viewer listener input");
            llListenControl(viewer_listener, 0);
            llListenControl(viewer_listener, 1);
            if (viewer_listener != llListen(4711, "", llGetOwner(), "Viewer listener input"))
                return;
            llListen(4714, "", llGetOwner(), "Send to my attachments");
            llWhisper(0, "Nearby whisper");
            llSay(0, "Linked child position");
        }

        llSay(0, (string)llGetAgentSize(llGetOwner()));
        if (llGetAgentSize(NULL_KEY) == ZERO_VECTOR
            && llGetAgentSize("invalid") == ZERO_VECTOR
            && llGetAgentSize("00000000-0000-4000-8000-000000000123") == ZERO_VECTOR)
            llSay(0, "Absent avatar size");
        if (llSameGroup("00000000-0000-4000-8000-000000000123")
            && llSameGroup("00000000-0000-4000-8000-000000000125")
            && llSameGroup(llGetOwner())
            && !llSameGroup(NULL_KEY))
            llSay(0, "Same object group");
        if (llSameGroup("00000000-0000-4000-8000-000000000124"))
            llSay(0, "Attachment uses wearer group");
        if (llIsFriend("00000000-0000-4000-8000-000000000456") && !llIsFriend(llGetOwner()))
            llSay(0, "Owner friendship lookup");
        if (llGetAttached() == 6
            && llGetLinkNumber() == 2
            && llGetNumberOfPrims() == 2
            && llGetObjectPrimCount(llGetKey()) == 0
            && llGetVel() == <3, 0, 0>
            && llGetLocalPos() == <1, 0, 0>
            && llGetPos() == llGetRootPosition() + <-1, 0, 0>
            && llGetLocalRot() == <1, 0, 0, 0>
            && llGetRootRotation() == ZERO_ROTATION
            && llGetRot() == <0, 1, 0, 0>
            && !llSameGroup(llGetKey()))
            llSay(0, "Linked attachment transform");
        if (llGetAttached() != 0 && llGetLinkNumber() == 2)
        {
            if (llGetAttached() == 6)
                llListen(4713, "", NULL_KEY, "Direct attachment chat");
            llLinkPlaySound(LINK_THIS, "12345678-1234-4234-8234-123456789abc", 0.5);
            llLinkPlaySound(LINK_THIS, "12345678-1234-4234-8234-123456789abc", 0.5, SOUND_TRIGGER);
            llSay(0, "Sound calls finished " + (string)llGetAttached());
        }
        llSay(0, (string)llGetOwnerKey("00000000-0000-4000-8000-000000000123"));
    }

    changed(integer mask)
    {
        if ((mask & CHANGED_LINK)
            && llGetKey() == "00000000-0000-4000-8000-000000000125"
            && llGetNumberOfPrims() == 3)
            llSay(0, "Sibling link changed");
        else
            llSay(0, "Inventory changed");
    }

    timer()
    {
        rotation q = llGetRot();
        if (q.z > 0.707 && q.s > 0.707 && q == llGetRootRotation() && !saw_camera)
        {
            saw_camera = 1;
            llSay(0, "Attachment sees camera");
        }
        if (saw_camera && q == ZERO_ROTATION)
        {
            llSay(0, "Attachment sees body again");
            llSetTimerEvent(0.0);
        }
    }
}
)LSL";
    auto initial = std::make_shared<framework::AssetBase>(Uuid("2074003b-8d5f-40e5-8b20-581c1c50aedb"),
                                                          "Default script", 10, circuit->agent_id.str());
    initial->description("");
    initial->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    check(assets.store(initial).has_value(), "actual default script asset");
    std::atomic<bool> allow_edit = true;
    std::atomic<bool> mute_avatar_sounds = false;
    std::atomic<bool> god_mode = false;
    RegionPolicies policies;
    policies.auto_grant_attach_permissions = wear_replaces_all;
    policies.auto_grant_all_script_permissions = !wear_replaces_all;
    policies.physical_linkset_capacity = 2;
    bool allow_drop_rez = false;
    int drop_permission_checks = 0;
    std::atomic<int> drop_parcel_capacity = 15000;
    policies.prim_limits_enabled = true;
    policies.enforce_parcel_rez_permissions = true;
    policies.max_prims_per_user = 1;
    policies.script_parcel_capacity = [&](std::int32_t) { return drop_parcel_capacity.load(); };
    policies.can_rez = [&](Uuid, framework::Vector3, std::string&) {
        ++drop_permission_checks;
        return allow_drop_rez;
    };
    policies.attachments_enabled = attachments_enabled;
    policies.wear_replaces_all = wear_replaces_all;
    policies.log = [](const auto &text) { std::cerr << text << '\n'; };
    policies.access = [](auto, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.can_edit = [&](auto agent, const auto &object) { return allow_edit && agent == object.owner; };
    policies.object_whisper_range = 3;
    auto open_parcel = std::make_shared<ScriptParcel>();
    open_parcel->id = Uuid::random();
    open_parcel->owner = circuit->agent_id;
    open_parcel->area = 65536;
    auto quiet_parcel = std::make_shared<ScriptParcel>(*open_parcel);
    quiet_parcel->any_avatar_sounds = false;
    quiet_parcel->group_avatar_sounds = false;
    auto restricted_parcel = std::make_shared<ScriptParcel>(*open_parcel);
    restricted_parcel->owner = Uuid::random();
    auto public_parcel = std::make_shared<ScriptParcel>(*restricted_parcel);
    public_parcel->id = Uuid::random();
    public_parcel->flags |= 1u << 6;
    std::atomic<int> drop_parcel_mode = 0;
    auto adjacent_parcel = std::make_shared<ScriptParcel>(*open_parcel);
    adjacent_parcel->id = Uuid::random();
    std::atomic<bool> split_parcel = false;
    policies.script_parcel_at = [&](framework::Vector3 position) -> std::shared_ptr<const ScriptParcel> {
        if (split_parcel && position.x >= 180)
            return adjacent_parcel;
        if (const auto mode = drop_parcel_mode.load(); mode) {
            if (mode == 3)
                return position.x >= 240 ? public_parcel : open_parcel;
            return mode == 1 ? restricted_parcel : public_parcel;
        }
        return mute_avatar_sounds ? quiet_parcel : open_parcel;
    };
    policies.can_move = [](auto agent, const auto &object) { return agent == object.owner; };
    policies.inventory_service = &inventory;
    policies.store_asset = [&](auto asset) {
        check(std::this_thread::get_id() == owner, "task store runs on owner");
        ++task_stores;
        assets.store(std::move(asset));
    };
    policies.store_inventory_asset = [&](auto asset) {
        check(std::this_thread::get_id() == owner, "agent store runs on owner");
        ++agent_stores;
        return assets.store(std::move(asset));
    };
    policies.load_asset = [&](auto key) { return assets.get_asset(key.str()); };
    policies.can_edit_script = [&](auto, const auto &, const auto &) {
        ++script_checks;
        return true;
    };
    policies.new_script = [&](auto, auto, auto key) {
        check(std::this_thread::get_id() == owner, "rez runs on owner");
        script = key;
    };
    policies.inventory_is_god = [&](Uuid agent) { return god_mode && agent == circuit->agent_id; };
    policies.is_friend = [&](Uuid principal, Uuid other) {
        return principal == circuit->agent_id && other == Uuid("00000000-0000-4000-8000-000000000456");
    };
    const auto avatar_database = path(directory / "avatars.db");
    SqliteAvatarStore avatar_store(avatar_database, true);
    AvatarService avatars(avatar_store);
    auto attachment_database = std::make_shared<SqliteDatabase>();
    attachment_database->open_path(avatar_database, false);
    SqliteTableOperations attachment_table([&] { return attachment_database; }, "native_attachment_scripts");
    const auto legacy_owner = Uuid::random();
    const auto legacy_item = Uuid::random();
    const auto legacy_asset = Uuid::random();
    const auto legacy_script = Uuid::random();
    attachment_table.execute("CREATE TABLE native_attachment_scripts ("
                             "owner_id TEXT NOT NULL, item_id TEXT NOT NULL, asset_id TEXT NOT NULL, "
                             "script_id TEXT NOT NULL, checkpoint BLOB NOT NULL, "
                             "PRIMARY KEY (owner_id,item_id,script_id))", {});
    attachment_table.execute("INSERT INTO native_attachment_scripts "
                             "VALUES (:owner,:item,:asset,:script,X'0001FF')",
                             {{":owner", legacy_owner.str()}, {":item", legacy_item.str()},
                              {":asset", legacy_asset.str()}, {":script", legacy_script.str()}});
    SqliteAttachmentScriptStore attachment_scripts(attachment_database);
    const services::AttachmentScriptStates legacy_states{{legacy_script, {0, 1, 255}}};
    check(attachment_scripts.load(legacy_owner, legacy_item, legacy_asset) == legacy_states,
          "migrate existing checkpoint bytes");
    const auto staged_asset = Uuid::random();
    attachment_scripts.save(legacy_owner, legacy_item, staged_asset, legacy_states);
    attachment_scripts.save(legacy_owner, legacy_item, staged_asset, {});
    check(attachment_scripts.load(legacy_owner, legacy_item, legacy_asset) == legacy_states &&
              attachment_scripts.load(legacy_owner, legacy_item, staged_asset).empty(),
          "staging and removing an asset revision preserves the previous checkpoint");
    policies.load_attachment_scripts = [&](Uuid agent, Uuid item, Uuid asset) {
        return attachment_scripts.load(agent, item, asset);
    };
    policies.save_attachment_scripts = [&](Uuid agent, Uuid item, Uuid asset, const auto& states) {
        attachment_scripts.save(agent, item, asset, states);
    };
    std::atomic<int> appearance_saves = 0;
    bool wear_fixture = false;
    policies.save_appearance = [&](Uuid agent, const framework::AvatarAppearance &appearance) {
        check(agent == circuit->agent_id && appearance.attachments().empty() != wear_fixture,
              "saved appearance reflects the attachment change");
        if (++appearance_saves == 1)
            return avatar_store.write_appearance([&] {
                check(avatar_store.erase_all(agent), "simulate failure after deleting appearance rows");
                return false;
            });
        return avatars.set_appearance(agent, &appearance);
    };
    NullPresenceStore presence_store;
    PresenceService presence(presence_store);
    GridRegion descriptor;
    descriptor.id = Uuid::random();
    descriptor.name = "Asset owner";
    descriptor.external_host = "127.0.0.1";
    descriptor.internal_endpoint = RegionEndpoint{"127.0.0.1", 0};
    std::atomic<bool> estate_access{false};
    policies.estate_manager = [&, owner = circuit->agent_id](Uuid agent) { return estate_access && agent == owner; };
    const auto permission_group = Uuid::random();
    const auto offline_owner = Uuid::random();
    policies.script_account = [&](Uuid id) -> std::optional<UserAccount> {
        if (id != offline_owner)
            return {};
        UserAccount account(id);
        account.first_name = "Offline";
        account.last_name = "Owner";
        return account;
    };
    policies.group_record_name = [&](Uuid id) -> std::optional<std::string> {
        if (id == permission_group)
            return "Permission Test Group";
        return {};
    };
    GridRegion permission_destination;
    permission_destination.id = Uuid::random();
    permission_destination.x = descriptor.x + 256;
    permission_destination.y = descriptor.y;
    std::atomic<bool> permission_teleport_cancelled = false;
    policies.teleport_destination = [&](std::uint64_t handle) -> std::optional<GridRegion> {
        if (handle == permission_destination.handle())
            return permission_destination;
        return {};
    };
    policies.teleport_access = [](auto&, const auto&, auto, bool, auto&) { return std::string{}; };
    policies.teleport_create = [](const auto&, auto&, auto& destination, const auto& viewer,
                                  auto, auto, auto&, const auto&, std::uint32_t flags,
                                  const auto*, auto&) -> std::optional<network::lludp::Packet> {
        auto finish = network::lludp::make_packet("TeleportFinish");
        auto& info = finish.block("Info");
        info["AgentID"] = viewer.circuit->agent_id;
        info["RegionHandle"] = destination.handle();
        info["TeleportFlags"] = std::uint64_t{flags};
        return finish;
    };
    policies.cancel_teleport_destination = [&](auto&, Uuid, Uuid) { permission_teleport_cancelled = true; };
    RegionRuntime runtime(descriptor, presence, std::move(policies));
    auto endpoint = runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 19009, 0, {}}));
    presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id);
    LoginTransferContext context;
    std::string reason;
    check(runtime.create_agent(nullptr, descriptor, circuit, 128, context, reason), "real admission");
    std::atomic<std::uint32_t> sequence = 0;
    std::mutex received_mutex;
    std::vector<network::lludp::Packet> received;
    network::UdpSocket *peer = nullptr;
    network::UdpSocket client({"127.0.0.1", 0}, [&](const auto &from, auto bytes) {
        auto p = network::lludp::decode_datagram(bytes, 65536, false);
        if (p.reliable) {
            auto ack = network::lludp::make_packet("PacketAck");
            ack.reliable = false;
            ack.sequence = ++sequence;
            auto b = network::lludp::make_block(ack.spec->blocks.at(0));
            b["ID"] = std::uint64_t(p.sequence);
            ack.blocks["Packets"].push_back(std::move(b));
            peer->send_to(from, network::lludp::encode_datagram(ack));
        }
        std::lock_guard lock(received_mutex);
        received.push_back(std::move(p));
    });
    peer = &client;
    client.start();
    auto make = [&](const char *name) {
        auto p = network::lludp::make_packet(name);
        p.sequence = ++sequence;
        if (p.blocks.contains("AgentData")) {
            p.block("AgentData")["AgentID"] = circuit->agent_id;
            p.block("AgentData")["SessionID"] = circuit->session_id;
        }
        return p;
    };
    auto send = [&](const auto &p) { client.send_to(endpoint, network::lludp::encode_datagram(p)); };
    auto pump = [&](auto predicate, std::source_location caller = std::source_location::current()) {
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (!predicate() && std::chrono::steady_clock::now() < until) {
            runtime.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!predicate())
            throw std::runtime_error("region owner/UDP deadline at fixture line " + std::to_string(caller.line()));
    };
    const auto await_ack = [&](std::uint32_t sequence,
                               std::source_location caller = std::source_location::current()) {
        pump([&] {
            std::lock_guard lock(received_mutex);
            return std::ranges::any_of(received, [&](const auto& packet) {
                if (std::ranges::find(packet.acknowledgements, sequence) != packet.acknowledgements.end())
                    return true;
                if (packet.spec->name != "PacketAck")
                    return false;
                return std::ranges::any_of(packet.blocks.at("Packets"), [&](const auto& row) {
                    return std::get<std::uint64_t>(row.at("ID")) == sequence;
                });
            });
        }, caller);
    };
    auto transferred = std::make_shared<framework::AgentData>();
    transferred->agent_id = circuit->agent_id;
    transferred->session_id = circuit->session_id;
    transferred->circuit_code = circuit->circuit_code;
    transferred->position = circuit->start_position;
    transferred->active_group_id = Uuid::random();
    if (attachments_enabled)
        check(runtime.update_agent(descriptor, transferred, context), "incoming active group update");
    auto initial_root = SceneObject::primitive(circuit->agent_id, {130, 130, 70});
    initial_root.id = Uuid("ffffffff-ffff-ffff-ffff-ffffffffffff");
    initial_root.group_mask = 0x4000;
    initial_root.sale_type = 2;
    initial_root.sale_price = 123;
    initial_root.last_owner = Uuid::random();
    initial_root.rezzer = Uuid::random();
    const auto initial_parent = runtime.world().add(initial_root, true, false);
    check(initial_parent.has_value(), "initial root");
    auto initial_child = SceneObject::primitive(circuit->agent_id, {1, 0, 0});
    initial_child.name = "Child property fixture";
    initial_child.id = Uuid("00000000-0000-4000-8000-000000000001");
    initial_child.parent_id = initial_parent->local_id;
    const auto initial_part = runtime.world().add(initial_child, true, false);
    check(initial_part.has_value(), "initial linked child");
    auto use = make("UseCircuitCode");
    use.block("CircuitCode")["ID"] = circuit->agent_id;
    use.block("CircuitCode")["SessionID"] = circuit->session_id;
    use.block("CircuitCode")["Code"] = std::uint64_t(42);
    send(use);
    pump([&] { return runtime.viewers().agent(circuit->agent_id).has_value(); });
    send(make("RegionHandshakeReply"));
    auto complete = make("CompleteAgentMovement");
    complete.block("AgentData")["CircuitCode"] = std::uint64_t(42);
    send(complete);
    pump([&] {
        auto actor = runtime.viewers().agent(circuit->agent_id);
        return actor && actor->root;
    });
    if (!attachments_enabled) {
        const auto viewer = runtime.viewers().agent(circuit->agent_id);
        auto attachment = SceneObject::primitive(circuit->agent_id, {});
        attachment.is_attachment = true;
        attachment.shape.state = 5;
        attachment.parent_id = viewer->local_id;
        const auto live = runtime.world().add(attachment, false);
        check(live.has_value(), "inject attachment to exercise disabled detach and snapshot");
        auto wear = make("RezSingleAttachmentFromInv");
        wear.block("ObjectData")["ItemID"] = disabled_wear.id;
        wear.block("ObjectData")["AttachmentPt"] = std::uint64_t(128 | 5);
        send(wear);
        auto detach = make("ObjectDetach");
        auto part = network::lludp::make_block(detach.spec->blocks.at(1));
        part["ObjectLocalID"] = std::uint64_t(live->local_id);
        detach.blocks["ObjectData"].push_back(std::move(part));
        send(detach);
        const auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
        pump([&] { return std::chrono::steady_clock::now() >= settle; });
        const auto worn = runtime.world().attachments(viewer->local_id);
        check(worn.size() == 1 && worn.front().id == live->id && appearance_saves == 0,
              "disabled attachments ignore login restoration, viewer wear, and viewer detach");
        framework::AgentData crossing;
        runtime.snapshot_attachments(*viewer, crossing);
        check(!crossing.attachment_objects || crossing.attachment_objects->size() == 0,
              "disabled attachments are omitted from crossing snapshots");
        client.stop();
        runtime.stop();
        return;
    }
    pump([&] {
        std::lock_guard lock(received_mutex);
        bool saw_root = false;
        for (const auto &packet : received)
            if (packet.spec->name == "ObjectUpdate") {
                for (const auto &object : packet.blocks.at("ObjectData")) {
                    const auto id = std::get<Uuid>(object.at("FullID"));
                    if (id == initial_root.id)
                        saw_root = true;
                    if (id == initial_child.id) {
                        check(saw_root, "initial root arrives before linked child");
                        return true;
                    }
                }
            }
        return false;
    });
    auto delink = make("ObjectDelink");
    auto delink_block = network::lludp::make_block(delink.spec->blocks.at(1));
    delink_block["ObjectLocalID"] = std::uint64_t(initial_part->local_id);
    delink.blocks["ObjectData"].push_back(std::move(delink_block));
    send(delink);
    pump([&] { return runtime.world().object(initial_child.id)->parent_id == 0; });
    check(runtime.world().object(initial_child.id)->position == framework::Vector3{131, 130, 70},
          "viewer delink preserves child world position");
    const auto detached_child = runtime.world().object(initial_child.id);
    check(detached_child->last_owner == initial_root.last_owner &&
              detached_child->rezzer == initial_root.rezzer,
          "viewer delink copies root ownership history");
    auto link = make("ObjectLink");
    for (const auto local : {initial_parent->local_id, initial_part->local_id}) {
        auto part = network::lludp::make_block(link.spec->blocks.at(1));
        part["ObjectLocalID"] = std::uint64_t(local);
        link.blocks["ObjectData"].push_back(std::move(part));
    }
    const auto foreign_part =
        runtime.world().add(SceneObject::primitive(Uuid::random(), {130, 130, 70}), false);
    const auto control_part =
        runtime.world().add(SceneObject::primitive(circuit->agent_id, {130, 131, 70}), false);
    check(foreign_part && control_part, "mixed-owner link test objects");
    auto mixed_link = link;
    auto foreign_block = network::lludp::make_block(link.spec->blocks.at(1));
    foreign_block["ObjectLocalID"] = std::uint64_t(foreign_part->local_id);
    mixed_link.blocks["ObjectData"].push_back(std::move(foreign_block));
    send(mixed_link);
    auto control_link = link;
    control_link.sequence = ++sequence;
    control_link.blocks["ObjectData"][1]["ObjectLocalID"] = std::uint64_t(control_part->local_id);
    send(control_link);
    pump([&] { return runtime.world().object(control_part->id)->parent_id == initial_parent->local_id; });
    check(runtime.world().object(initial_child.id)->parent_id == 0 &&
              runtime.world().object(foreign_part->id)->parent_id == 0,
          "mixed-owner request leaves even the first editable child unlinked");
    link.sequence = ++sequence;
    send(link);
    pump([&] { return runtime.world().object(initial_child.id)->parent_id == initial_parent->local_id; });
    check(runtime.world().object(initial_child.id)->position == framework::Vector3{1, 0, 0},
          "viewer link restores the child offset");
    auto family = make("RequestObjectPropertiesFamily");
    family.block("ObjectData")["ObjectID"] = initial_child.id;
    family.block("ObjectData")["RequestFlags"] = std::uint64_t(4);
    send(family);
    pump([&] {
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &packet) {
            if (packet.spec->name != "ObjectPropertiesFamily")
                return false;
            const auto &data = packet.block("ObjectData");
            return std::get<Uuid>(data.at("ObjectID")) == initial_root.id &&
                   std::get<std::uint64_t>(data.at("RequestFlags")) == 4 &&
                   std::get<Uuid>(data.at("LastOwnerID")) == initial_root.last_owner;
        });
    });
    auto select_child = make("ObjectSelect");
    auto selected = network::lludp::make_block(select_child.spec->blocks.at(1));
    selected["ObjectLocalID"] = std::uint64_t(initial_part->local_id);
    select_child.blocks["ObjectData"].push_back(std::move(selected));
    send(select_child);
    pump([&] {
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &packet) {
            if (packet.spec->name != "ObjectProperties")
                return false;
            const auto &data = packet.block("ObjectData");
            return std::get<Uuid>(data.at("ObjectID")) == initial_child.id &&
                   std::get<std::uint64_t>(data.at("GroupMask")) == initial_root.group_mask &&
                   std::get<std::int64_t>(data.at("SalePrice")) == initial_root.sale_price &&
                   field(packet, "ObjectData", "Name") == initial_child.name;
        });
    });
    auto listing = make("ObjectSaleInfo");
    auto listing_data = network::lludp::make_block(listing.spec->blocks.at(1));
    listing_data["LocalID"] = std::uint64_t(initial_part->local_id);
    listing_data["SaleType"] = std::uint64_t(1);
    listing_data["SalePrice"] = std::int64_t(42);
    listing.blocks["ObjectData"].push_back(std::move(listing_data));
    send(listing);
    pump([&] {
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &packet) {
            if (packet.spec->name != "ObjectProperties")
                return false;
            const auto &data = packet.block("ObjectData");
            return std::get<Uuid>(data.at("ObjectID")) == initial_root.id &&
                   std::get<std::uint64_t>(data.at("SaleType")) == 1 &&
                   std::get<std::int64_t>(data.at("SalePrice")) == 42;
        });
    });
    auto move_group = make("MultipleObjectUpdate");
    auto move_block = network::lludp::make_block(move_group.spec->blocks.at(1));
    move_block["ObjectLocalID"] = std::uint64_t(initial_part->local_id);
    move_group.blocks["ObjectData"].push_back(std::move(move_block));
    const auto send_transform = [&](std::uint64_t type, std::initializer_list<float> values) {
        auto &transform = move_group.block("ObjectData");
        transform["Type"] = type;
        std::vector<std::uint8_t> bytes;
        for (const float value : values) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
        }
        transform["Data"] = std::move(bytes);
        move_group.sequence = ++sequence;
        send(move_group);
    };
    send_transform(9, {133, 130, 70});
    pump([&] {
        return runtime.world().object(initial_root.id)->position == framework::Vector3{133, 130, 70};
    });
    check(runtime.world().object(initial_child.id)->position == framework::Vector3{1, 0, 0},
          "group movement addressed to child preserves the child offset");
    send_transform(10, {0, 0, 1});
    pump([&] {
        return runtime.world().object(initial_root.id)->rotation == framework::Quaternion{0, 0, 1, 0};
    });
    send_transform(11, {134, 131, 70, 0, 1, 0});
    pump([&] {
        const auto root = runtime.world().object(initial_root.id);
        return root->position == framework::Vector3{134, 131, 70} &&
               root->rotation == framework::Quaternion{0, 1, 0, 0};
    });
    const auto updated_child = runtime.world().object(initial_child.id);
    check(updated_child->position == framework::Vector3{1, 0, 0} &&
              updated_child->rotation == framework::Quaternion{0, 0, 0, 1},
          "group rotation preserves the child local transform");
    send_transform(12, {1, 2, 3});
    pump([&] { return runtime.world().object(initial_child.id)->scale == framework::Vector3{1, 2, 3}; });
    check(runtime.world().object(initial_root.id)->scale == initial_root.scale,
          "viewer group-scale opcode edits the addressed prim");
    send_transform(0x1c, {1, 1, 1});
    pump([&] { return runtime.world().object(initial_child.id)->scale == framework::Vector3{2, 4, 6}; });
    check(runtime.world().object(initial_root.id)->scale == framework::Vector3{1, 1, 1} &&
              runtime.world().object(initial_child.id)->position == framework::Vector3{2, 0, 0},
          "uniform group resize scales the root and child offset");
    send_transform(0x1d, {136, 132, 70, .5f, .5f, .5f});
    pump([&] {
        const auto root = runtime.world().object(initial_root.id);
        const auto child = runtime.world().object(initial_child.id);
        return root->position == framework::Vector3{136, 132, 70} &&
               root->scale == framework::Vector3{.5f, .5f, .5f} &&
               child->position == framework::Vector3{1, 0, 0} && child->scale == framework::Vector3{1, 2, 3};
    });
    pump([&] {
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &packet) {
            return packet.spec->name == "ObjectProperties" &&
                   std::get<Uuid>(packet.block("ObjectData").at("ObjectID")) == initial_root.id;
        });
    });
    auto control_physics = runtime.world().object(control_part->id)->physics;
    control_physics.shape = 1;
    check(runtime.world().update_extra_physics(control_part->id, control_physics).has_value(),
          "exclude the link-order control prim from the physics limit");
    auto flags = make("ObjectFlagUpdate");
    auto &flags_data = flags.block("AgentData");
    flags_data["ObjectLocalID"] = std::uint64_t(initial_part->local_id);
    flags_data["UsePhysics"] = true;
    flags_data["IsPhantom"] = true;
    flags_data["IsTemporary"] = true;
    send(flags);
    constexpr std::uint32_t expected_flags = 0x20000401u;
    pump([&] { return (runtime.world().object(initial_root.id)->flags & expected_flags) == expected_flags; });
    check((runtime.world().object(initial_child.id)->flags & expected_flags) == expected_flags,
          "viewer updates root and child prim flags");
    auto extra_flags = make("ObjectFlagUpdate");
    extra_flags.block("AgentData")["ObjectLocalID"] = std::uint64_t(initial_part->local_id);
    auto extra = network::lludp::make_block(extra_flags.spec->blocks.at(1));
    extra["PhysicsShapeType"] = std::uint64_t(1);
    extra["Density"] = 1200.;
    extra["Friction"] = .25;
    extra["Restitution"] = 2.;
    extra["GravityMultiplier"] = -.5;
    extra_flags.blocks["ExtraPhysics"].push_back(std::move(extra));
    send(extra_flags);
    pump([&] { return runtime.world().object(initial_child.id)->physics.density == 1200.f; });
    const auto physics_part = runtime.world().object(initial_child.id);
    check(physics_part->physics == scene::PrimPhysicsProperties{1, 1200.f, .25f, .5f, -.5f} &&
              (physics_part->flags & expected_flags) == expected_flags,
          "extra physics edits only the addressed part and preserves invalid fields");
    send_transform(0x1c, {20, 20, 20});
    pump([&] {
        const auto z = runtime.world().object(initial_child.id)->scale.z;
        return z > 9.999f && z < 10.001f;
    });
    check(runtime.world().object(initial_root.id)->scale.z < 10.f,
          "physical resize reduces group scale to respect child dimensions");
    const auto offset_before = runtime.world().object(initial_child.id)->position.x;
    move_group.block("ObjectData")["ObjectLocalID"] = std::uint64_t(initial_parent->local_id);
    send_transform(1, {137, 132, 70});
    pump([&] {
        return runtime.world().object(initial_root.id)->position == framework::Vector3{137, 132, 70};
    });
    const auto offset_after = runtime.world().object(initial_child.id)->position.x;
    check(offset_after > offset_before + .999f && offset_after < offset_before + 1.001f,
          "root-only position edit offsets the child to preserve its world position");
    send_transform(2, {0, 0, 0});
    pump([&] {
        return runtime.world().object(initial_root.id)->rotation == framework::Quaternion{0, 0, 0, 1};
    });
    const auto rotated_child = runtime.world().object(initial_child.id);
    check(rotated_child->position == framework::Vector3{-offset_after, 0, 0} &&
              rotated_child->rotation == framework::Quaternion{0, 1, 0, 0},
          "root-only rotation preserves child world position and orientation");
    const auto send_legacy_transform = [&](const char *name, const char *field, std::vector<double> value) {
        auto packet = make(name);
        auto part = network::lludp::make_block(packet.spec->blocks.at(1));
        part["ObjectLocalID"] = std::uint64_t(initial_part->local_id);
        part[field] = std::move(value);
        packet.blocks["ObjectData"].push_back(std::move(part));
        send(packet);
    };
    send_legacy_transform("ObjectScale", "Scale", {.5, .5, .5});
    pump([&] {
        const auto root = runtime.world().object(initial_root.id);
        const auto child = runtime.world().object(initial_child.id);
        return root->scale.x > .499f && root->scale.x < .501f && child->scale.z > 2.999f &&
               child->scale.z < 3.001f;
    });
    const auto legacy_child = runtime.world().object(initial_child.id);
    send_legacy_transform("ObjectPosition", "Position", {138, 133, 70});
    pump([&] {
        return runtime.world().object(initial_root.id)->position == framework::Vector3{138, 133, 70};
    });
    send_legacy_transform("ObjectRotation", "Rotation", {0, 0, 1, 0});
    pump([&] {
        return runtime.world().object(initial_root.id)->rotation == framework::Quaternion{0, 0, 1, 0};
    });
    const auto legacy_result = runtime.world().object(initial_child.id);
    check(legacy_result->position == legacy_child->position &&
              legacy_result->rotation == legacy_child->rotation,
          "legacy group movement and rotation preserve the child local transform");
    const auto move_only_root = runtime.world().object(initial_root.id);
    allow_edit = false;
    move_group.block("ObjectData")["ObjectLocalID"] = std::uint64_t(initial_part->local_id);
    send_transform(13, {139, 134, 70, 2, 2, 2});
    pump([&] {
        return runtime.world().object(initial_root.id)->position == framework::Vector3{139, 134, 70};
    });
    const auto move_only_child = runtime.world().object(initial_child.id);
    check(runtime.world().object(initial_root.id)->scale == move_only_root->scale &&
              move_only_child->scale == legacy_result->scale &&
              move_only_child->position == legacy_result->position,
          "move-only permission strips scaling and applies group position");
    allow_edit = true;
    auto metadata_child = SceneObject::primitive(Uuid::random(), {0, 1, 0});
    metadata_child.parent_id = initial_parent->local_id;
    const auto metadata_part = runtime.world().add(metadata_child);
    check(metadata_part.has_value(), "linked child metadata fixture");
    auto description = make("ObjectDescription");
    auto description_part = network::lludp::make_block(description.spec->blocks.at(1));
    description_part["LocalID"] = std::uint64_t(metadata_part->local_id);
    const std::string prefix = "Description set through root permission";
    description_part["Description"] = wire(prefix + "\nignored");
    description.blocks["ObjectData"].push_back(std::move(description_part));
    send(description);
    pump([&] { return runtime.world().object(metadata_part->id)->description == prefix; });
    for (const auto &edit :
         {std::pair{"ObjectMaterial", "Material"}, std::pair{"ObjectClickAction", "ClickAction"}}) {
        auto packet = make(edit.first);
        auto part = network::lludp::make_block(packet.spec->blocks.at(1));
        part["ObjectLocalID"] = std::uint64_t(metadata_part->local_id);
        part[edit.second] = std::uint64_t(2);
        packet.blocks["ObjectData"].push_back(std::move(part));
        send(packet);
    }
    pump([&] {
        const auto part = runtime.world().object(metadata_part->id);
        return part->material == 2 && part->click_action == 2;
    });
    auto shape_edit = make("ObjectShape");
    auto shape = network::lludp::make_block(shape_edit.spec->blocks.at(1));
    shape["ObjectLocalID"] = std::uint64_t(metadata_part->local_id);
    shape["PathCurve"] = std::uint64_t(16);
    shape["ProfileCurve"] = std::uint64_t(1);
    shape["PathScaleX"] = shape["PathScaleY"] = std::uint64_t(100);
    shape["PathTwist"] = std::int64_t(-12);
    shape_edit.blocks["ObjectData"].push_back(std::move(shape));
    send(shape_edit);
    pump([&] { return runtime.world().object(metadata_part->id)->shape.path_twist == -12; });
    auto image_edit = make("ObjectImage");
    auto image = network::lludp::make_block(image_edit.spec->blocks.at(1));
    image["ObjectLocalID"] = std::uint64_t(metadata_part->local_id);
    const auto texture = framework::TextureEntry(framework::TextureEntry::white_texture).bytes();
    image["TextureEntry"] = texture;
    image_edit.blocks["ObjectData"].push_back(std::move(image));
    send(image_edit);
    pump([&] { return runtime.world().object(metadata_part->id)->texture_entry == texture; });
    const auto unchanged_root = runtime.world().object(initial_root.id);
    check(unchanged_root->shape.path_twist == initial_root.shape.path_twist &&
              unchanged_root->texture_entry == initial_root.texture_entry,
          "linked shape and texture edits affect only the selected child");
    auto update = [&](Uuid actor, Uuid item, Uuid object, const std::vector<std::uint8_t> &data) {
        auto future = std::async(std::launch::async, [&runtime, actor, item, object, data] {
            return runtime.update_inventory_asset(actor, item, object, data);
        });
        try {
            pump([&] { return future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; });
        } catch (...) {
            runtime.quiesce_script_updates();
            future.wait();
            throw;
        }
        return future.get();
    };
    std::vector<std::uint8_t> raw{'N', 0, 255, 'Z'};
    auto asset = update(circuit->agent_id, note.id, {}, raw);
    check(!asset.is_zero() && agent_stores == 1 &&
              inventory.item(circuit->agent_id, note.id)->asset_id == asset &&
              *assets.get_data(asset.str()) == raw,
          "queued real agent update persists exact raw data");
    check(inventory.item(circuit->agent_id, note.id)->inventory_type == 263,
          "raw stored InventoryType retained");
    auto packet_seen = [&](const char *name, const char *text) {
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &p) {
            return p.spec->name == name && field(p, "AlertData", "Message") == text;
        });
    };
    pump([&] { return packet_seen("AlertMessage", "Notecard updated"); });
    auto deny = *inventory.item(circuit->agent_id, note.id);
    deny.current_permissions = 0;
    inventory.update_item(deny);
    check(update(circuit->agent_id, note.id, {}, raw).is_zero() && agent_stores == 1,
          "owner policy denial before store");
    pump([&] { return packet_seen("AgentAlertMessage", "/Insufficient permissions to edit notecard"); });
    check(update(Uuid::random(), note.id, {}, raw).is_zero() && agent_stores == 1,
          "missing scene presence rejected");
    const Uuid other_owner("00000000-0000-4000-8000-000000000456");
    auto target = SceneObject::primitive(other_owner, runtime.viewers().agent(circuit->agent_id)->position);
    target.group = transferred->active_group_id;
    target.rotation = {0, 0, 1, 0};
    target.velocity = {0, 2, 0};
    target.name = "Linked root";
    target.id = Uuid("00000000-0000-4000-8000-000000000123");
    const auto target_in_scene = runtime.world().add(target);
    check(target_in_scene.has_value(), "owner lookup target is present in the live scene");
    auto linked_child = SceneObject::primitive(circuit->agent_id, {1, 0, 0});
    linked_child.rotation = {1, 0, 0, 0};
    linked_child.name = "Linked child";
    linked_child.id = Uuid("00000000-0000-4000-8000-000000000125");
    linked_child.parent_id = target_in_scene->local_id;
    check(runtime.world().add(linked_child).has_value(), "ungrouped linked child of grouped root");
    auto attachment = SceneObject::primitive(circuit->agent_id, {});
    attachment.id = Uuid("00000000-0000-4000-8000-000000000124");
    attachment.parent_id = runtime.viewers().agent(circuit->agent_id)->local_id;
    attachment.is_attachment = true;
    attachment.shape.state = 6;
    attachment.rotation = {0, 0, 1, 0};
    check(bool(runtime.world().add(attachment, false)), "ungrouped attachment on grouped avatar");

    auto hud = attachment;
    hud.id = Uuid::random();
    hud.shape.state = 31;
    check(runtime.world().add(hud, false).has_value(), "private attachment for script query");
    auto script_position = runtime.viewers().agent(circuit->agent_id)->position;
    script_position.x += 5;
    auto scripted_prim = SceneObject::primitive(circuit->agent_id, script_position);
    scripted_prim.id = Uuid("00000000-0000-4000-8000-000000000127");
    scripted_prim.group = transferred->active_group_id;
    auto object = runtime.world().add(scripted_prim);
    check(bool(object), "actual task part");
    auto rez = make("RezScript");
    auto &block = rez.block("InventoryBlock");
    block["FolderID"] = object->id;
    block["OwnerID"] = block["CreatorID"] = circuit->agent_id;
    block["BaseMask"] = block["OwnerMask"] = std::uint64_t(0x9e000);
    block["NextOwnerMask"] = std::uint64_t(0x82000);
    block["Type"] = block["InvType"] = std::int64_t(10);
    block["Name"] = wire("Live script");
    block["Description"] = wire("Default");
    send(rez);
    pump([&] { return !script.is_zero(); });
    check(runtime.active_script_count() == 1, "real native script instance");
    runtime.set_scripts_enabled(false);
    check(runtime.active_script_count() == 0, "region-wide disable hides active scripts");
    runtime.set_scripts_enabled(true);
    check(runtime.active_script_count() == 1, "re-enable restores active script count");

    auto chats = [&] {
        std::set<std::uint32_t> ids;
        std::lock_guard lock(received_mutex);
        for (const auto &p : received)
            if (p.spec->name == "ChatFromSimulator" && field(p, "ChatData", "Message") == "Script running")
                ids.insert(p.sequence);
        return ids.size();
    };
    pump([&] { return chats() > 0; });
    auto previous = chats();
    auto stores_before = task_stores;
    auto task_asset = update(circuit->agent_id, script, object->id, raw);
    check(!task_asset.is_zero() && task_stores == stores_before + 1 &&
              *assets.get_data(task_asset.str()) == raw && runtime.active_script_count() == 1 &&
              script_checks == 0,
          "generic task update does not invoke script editor policy or restart native VM");
    auto reset = make("ScriptReset");
    reset.block("Script")["ObjectID"] = object->id;
    reset.block("Script")["ItemID"] = script;
    send(reset);
    pump([&] { return chats() > previous; });
    check(runtime.active_script_count() == 1,
          "reset still executes original compiled script after raw inventory asset edit");
    pump([&] {
        std::lock_guard lock(received_mutex);
        for (const auto &packet : received)
            if (packet.spec->name == "ChatFromSimulator" &&
                field(packet, "ChatData", "Message") == other_owner.str())
                return true;
        return false;
    });
    auto inventory_changes = [&] {
        std::set<std::uint32_t> ids;
        std::lock_guard lock(received_mutex);
        for (const auto &packet : received)
            if (packet.spec->name == "ChatFromSimulator" &&
                field(packet, "ChatData", "Message") == "Inventory changed")
                ids.insert(packet.sequence);
        return ids.size();
    };
    pump([&] { return inventory_changes() > 0; });
    const auto changes_before_disable = inventory_changes();
    pump([&] {
        std::lock_guard lock(received_mutex);
        bool dimensions = false, absent = false, same_group = false, wearer_group = false, friendship = false;
        for (const auto &packet : received)
            if (packet.spec->name == "ChatFromSimulator") {
                const auto message = field(packet, "ChatData", "Message");
                dimensions |= message == "<0.500000, 0.750000, 1.875000>";
                absent |= message == "Absent avatar size";
                same_group |= message == "Same object group";
                wearer_group |= message == "Attachment uses wearer group";
                friendship |= message == "Owner friendship lookup";
            }
        return dimensions && absent && same_group && wearer_group && friendship;
    });
    const auto original_script = script;
    runtime.set_scripts_enabled(false);
    rez.sequence = ++sequence;
    send(rez);
    pump([&] { return script != original_script; });
    auto remove = make("RemoveTaskInventory");
    remove.block("InventoryData")["LocalID"] = std::uint64_t(object->local_id);
    remove.block("InventoryData")["ItemID"] = script;
    send(remove);
    pump([&] {
        std::lock_guard lock(received_mutex);
        for (const auto &packet : received)
            if (packet.spec->name == "ObjectProperties")
                for (const auto &entry : packet.blocks.at("ObjectData"))
                    if (std::get<Uuid>(entry.at("ObjectID")) == object->id &&
                        std::get<std::int64_t>(entry.at("InventorySerial")) == 4)
                        return true;
        return false;
    });
    previous = chats();
    runtime.set_scripts_enabled(true);
    runtime.poll();
    check(runtime.active_script_count() == 1 && chats() == previous,
          "removing a deferred script leaves the original instance count intact");
    const auto delivery_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (std::chrono::steady_clock::now() < delivery_deadline) {
        runtime.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(inventory_changes() == changes_before_disable,
          "disabled inventory changes must not replay after enabling scripts");

    rez.sequence = ++sequence;
    rez.block("InventoryBlock")["FolderID"] = attachment.id;
    send(rez);
    pump([&] {
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &packet) {
            return packet.spec->name == "ChatFromSimulator" &&
                   field(packet, "ChatData", "Message") == "Attachment point six";
        });
    });
    rez.sequence = ++sequence;
    rez.block("InventoryBlock")["FolderID"] = linked_child.id;
    send(rez);
    pump([&] {
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &packet) {
            return packet.spec->name == "ChatFromSimulator" &&
                   field(packet, "ChatData", "Message") == "Linked child position";
        });
    });
    const auto sibling = runtime.world().add(SceneObject::primitive(other_owner, target.position), false);
    check(sibling && runtime.world().link_prim(sibling->id, target.id),
          "link another prim while child script runs");
    pump([&] {
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &packet) {
            return packet.spec->name == "ChatFromSimulator" &&
                   field(packet, "ChatData", "Message") == "Sibling link changed";
        });
    });
    auto camera = make("AgentUpdate");
    auto &camera_data = camera.block("AgentData");
    camera_data["BodyRotation"] = std::vector<double>{0, 0, 0, 1};
    camera_data["CameraAtAxis"] = std::vector<double>{0, 1, 0};
    camera_data["CameraLeftAxis"] = std::vector<double>{-1, 0, 0};
    camera_data["CameraUpAxis"] = std::vector<double>{0, 0, 1};
    camera_data["ControlFlags"] = std::uint64_t(0x40000);
    send(camera);
    const auto heard = [&](const char *message) {
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &packet) {
            return packet.spec->name == "ChatFromSimulator" &&
                   field(packet, "ChatData", "Message") == message;
        });
    };
    pump([&] { return heard("Attachment sees camera"); });
    camera.sequence = ++sequence;
    camera_data["ControlFlags"] = std::uint64_t(0);
    send(camera);
    pump([&] { return heard("Attachment sees body again"); });
    auto viewer_chat = make("ChatFromViewer");
    auto& viewer_chat_data = viewer_chat.block("ChatData");
    viewer_chat_data["Type"] = std::uint64_t(1);
    viewer_chat_data["Channel"] = std::int64_t(4711);
    viewer_chat_data["Message"] = wire("Viewer listener input");
    send(viewer_chat);
    pump([&] { return heard("Region listener received"); });
    pump([&] { return heard("Direct viewer chat") && heard("Direct prim listener received"); });
    {
        std::lock_guard lock(received_mutex);
        bool nearby_whisper = false;
        bool distant_shout = false;
        bool direct_viewer = false;
        for (const auto& packet : received) {
            if (packet.spec->name != "ChatFromSimulator")
                continue;
            const auto message = field(packet, "ChatData", "Message");
            const auto& chat = packet.blocks.at("ChatData").front();
            const auto type = std::get<std::uint64_t>(chat.at("ChatType"));
            const auto owner = std::get<Uuid>(chat.at("OwnerID"));
            nearby_whisper |= message == "Nearby whisper" && type == 0 && owner == circuit->agent_id;
            distant_shout |= message == "In shout range" && type == 2 && owner.is_zero();
            direct_viewer |= message == "Direct viewer chat" && type == 9 && owner == circuit->agent_id;
            check(message != "Out of whisper range", "out-of-range whisper is suppressed");
            check(message != "Direct prim chat" && message != "Rejected direct debug",
                  "targeted prim and debug messages do not appear in the viewer");
        }
        check(nearby_whisper && distant_shout && direct_viewer, "viewer receives managed chat packet types");
    }

    const auto wearer = runtime.viewers().agent(circuit->agent_id);
    check(wearer && runtime.viewers().update_physics(circuit->agent_id, circuit->session_id,
                                                    wearer->position, {3, 0, 0}, {}, false),
          "wearer has world velocity");
    auto worn_child = SceneObject::primitive(circuit->agent_id, {1, 0, 0});
    worn_child.parent_id = runtime.world().object(attachment.id)->local_id;
    worn_child.rotation = {1, 0, 0, 0};
    check(runtime.world().add(worn_child, false).has_value(), "child of worn attachment");
    mute_avatar_sounds = true;
    rez.sequence = ++sequence;
    rez.block("InventoryBlock")["FolderID"] = worn_child.id;
    send(rez);
    pump([&] { return heard("Linked attachment transform"); });
    pump([&] { return heard("Sound calls finished 6"); });
    viewer_chat.sequence = ++sequence;
    viewer_chat_data["Channel"] = std::int64_t(4714);
    viewer_chat_data["Message"] = wire("Send to my attachments");
    send(viewer_chat);
    pump([&] { return heard("Direct attachment listener received"); });
    check(!heard("Direct attachment listener leaked"), "avatar-targeted chat stays within its attachments");

    auto hud_child = SceneObject::primitive(circuit->agent_id, {});
    hud_child.parent_id = runtime.world().object(hud.id)->local_id;
    check(runtime.world().add(hud_child, false).has_value(), "child of private HUD");
    rez.sequence = ++sequence;
    rez.block("InventoryBlock")["FolderID"] = hud_child.id;
    send(rez);
    pump([&] { return heard("Sound calls finished 31"); });
    const auto heard_sound = [&](std::string_view packet_name, Uuid part) {
        const auto block = packet_name == "SoundTrigger" ? "SoundData" : "DataBlock";
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &packet) {
            return packet.spec->name == packet_name &&
                   std::get<Uuid>(packet.blocks.at(block).front().at("ObjectID")) == part;
        });
    };
    pump([&] { return heard_sound("AttachedSound", hud_child.id); });
    check(!heard_sound("AttachedSound", worn_child.id) && !heard_sound("SoundTrigger", worn_child.id) &&
              !heard_sound("SoundTrigger", hud_child.id),
          "quiet parcel mutes worn prims while private attached sound reaches the HUD wearer");
    mute_avatar_sounds = false;

    const auto before_restricted_script = script;
    rez.sequence = ++sequence;
    auto &restricted = rez.block("InventoryBlock");
    restricted["FolderID"] = initial_child.id;
    restricted["OwnerMask"] = std::uint64_t(0x96000);
    send(rez);
    pump([&] { return script != before_restricted_script; });
    listing.sequence = ++sequence;
    listing.block("ObjectData")["SaleType"] = std::uint64_t(2);
    listing.block("ObjectData")["SalePrice"] = std::int64_t(99);
    send(listing);
    pump(
        [&] { return packet_seen("AgentAlertMessage", "/You don't have permission to set object on sale"); });
    check(runtime.world().object(initial_root.id)->sale_price == 42,
          "no-copy inventory in a child prevents changing the root to a copy sale");
    god_mode = true;
    listing.sequence = ++sequence;
    send(listing);
    pump([&] { return runtime.world().object(initial_root.id)->sale_price == 99; });
    god_mode = false;
    listing.sequence = ++sequence;
    listing.block("ObjectData")["SaleType"] = std::uint64_t(1);
    listing.block("ObjectData")["SalePrice"] = std::int64_t(-7);
    send(listing);
    pump([&] {
        std::lock_guard lock(received_mutex);
        return std::any_of(received.begin(), received.end(), [&](const auto &packet) {
            if (packet.spec->name != "ObjectProperties")
                return false;
            const auto &data = packet.block("ObjectData");
            return std::get<Uuid>(data.at("ObjectID")) == initial_root.id &&
                   std::get<std::uint64_t>(data.at("SaleType")) == 1 &&
                   std::get<std::int64_t>(data.at("SalePrice")) == -7;
        });
    });
    const std::string restored_source =
        "default { state_entry() { llSay(0, \"Restored task script runs\"); } }";
    auto restored_asset = std::make_shared<framework::AssetBase>(Uuid::random(), "Restored script", 10,
                                                                 circuit->agent_id.str());
    restored_asset->data =
        std::make_shared<std::vector<std::uint8_t>>(restored_source.begin(), restored_source.end());
    check(assets.store(restored_asset).has_value(), "stored asset for restored script");
    auto restored_task = std::make_shared<framework::TaskInventoryItem>();
    restored_task->item_id = Uuid::random();
    restored_task->asset_id = restored_asset->full_id();
    restored_task->parent_id = restored_task->parent_part_id = object->id;
    restored_task->owner_id = restored_task->creator_id = circuit->agent_id;
    restored_task->type = restored_task->inventory_type = 10;
    restored_task->name = "Restored script";
    runtime.restore_task_inventory(object->id, {restored_task});
    pump([&] { return heard("Restored task script runs"); });
    std::vector<std::future<bool>> pending;
    for (int n = 0; n < 16; ++n)
        pending.push_back(std::async(std::launch::async, [&] {
            try {
                runtime.update_inventory_asset(circuit->agent_id, note.id, {}, raw);
                return false;
            } catch (const std::exception &) {
                return true;
            }
        }));
    runtime.quiesce_script_updates();
    for (auto &future : pending)
        check(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready && future.get(),
              "quiesce cancels or rejects every queued asset update");
    bool rejected = false;
    try {
        runtime.update_inventory_asset(circuit->agent_id, note.id, {}, raw);
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "post-quiesce rejection");
    auto temporary = attachment;
    temporary.id = Uuid::random();
    const auto worn = runtime.world().add(temporary, false);
    check(worn.has_value(), "temporary attachment for viewer detach");
    temporary.id = Uuid::random();
    const auto second_worn = runtime.world().add(temporary, false);
    check(second_worn.has_value(), "second temporary attachment for viewer detach");
    temporary.id = Uuid::random();
    temporary.from_inventory_item = Uuid::random();
    circuit->appearance->set_attachment(6, temporary.from_inventory_item, Uuid::random());
    check(avatars.set_appearance(circuit->agent_id, circuit->appearance.get()),
          "persist initial worn attachment");
    const auto inventory_worn = runtime.world().add(temporary, false);
    check(inventory_worn.has_value(), "inventory-backed attachment for viewer detach");
    auto detach = make("ObjectDetach");
    for (const auto local : {worn->local_id, target_in_scene->local_id, second_worn->local_id}) {
        auto detach_block = network::lludp::make_block(detach.spec->blocks.at(1));
        detach_block["ObjectLocalID"] = std::uint64_t(local);
        detach.blocks["ObjectData"].push_back(std::move(detach_block));
    }
    send(detach);
    pump([&] { return !runtime.world().object(worn->id) && !runtime.world().object(second_worn->id); });
    auto detach_item = make("DetachAttachmentIntoInv");
    detach_item.block("ObjectData")["AgentID"] = circuit->agent_id;
    detach_item.block("ObjectData")["ItemID"] = inventory_worn->from_inventory_item;
    send(detach_item);
    pump([&] { return appearance_saves == 1; });
    check(runtime.world().object(inventory_worn->id).has_value() &&
              circuit->appearance->attachments().size() == 1,
          "failed appearance save preserves worn attachment");
    check(avatars.get_appearance(circuit->agent_id)->attachments().size() == 1,
          "rollback preserves previously saved worn attachment");
    detach_item.sequence = ++sequence;
    send(detach_item);
    pump([&] { return !runtime.world().object(inventory_worn->id); });
    const auto detached_viewer = runtime.viewers().agent(circuit->agent_id);
    check(!runtime.world().object(inventory_worn->id) && appearance_saves == 2 &&
              detached_viewer && detached_viewer->appearance()->attachments().empty(),
          "unchanged inventory attachment detaches and persists appearance");
    SqliteAvatarStore reopened_store(avatar_database);
    AvatarService reopened_avatars(reopened_store);
    const auto reloaded = reopened_avatars.get_appearance(circuit->agent_id);
    check(reloaded && reloaded->attachments().empty(), "detached item stays unworn after reopening SQLite");
    // Exercise the verified visitor branch on the admitted test circuit.
    circuit->verified_foreign_login = true;
    temporary.id = Uuid::random();
    temporary.from_inventory_item = Uuid::random();
    temporary.changed = true;
    auto visitor_appearance = std::make_shared<framework::AvatarAppearance>(*detached_viewer->appearance());
    visitor_appearance->set_attachment(6, temporary.from_inventory_item, Uuid::random());
    check(runtime.viewers().update_appearance(circuit->agent_id, circuit->session_id,
                                              std::move(visitor_appearance)).has_value(),
          "set the live visitor appearance");
    const auto visitor_worn = runtime.world().add(temporary, false);
    check(visitor_worn.has_value(), "edited visitor attachment for detach");
    detach_item.sequence = ++sequence;
    detach_item.block("ObjectData")["ItemID"] = visitor_worn->from_inventory_item;
    send(detach_item);
    pump([&] { return !runtime.world().object(visitor_worn->id); });
    const auto visitor = runtime.viewers().agent(circuit->agent_id);
    check(appearance_saves == 2 && visitor && visitor->appearance()->attachments().empty(),
          "visitor detach updates live appearance without saving locally");
    circuit->verified_foreign_login = false;
    auto detach_scripted = make("ObjectDetach");
    auto detach_scripted_block = network::lludp::make_block(detach_scripted.spec->blocks.at(1));
    detach_scripted_block["ObjectLocalID"] = std::uint64_t(runtime.world().object(attachment.id)->local_id);
    detach_scripted.blocks["ObjectData"].push_back(std::move(detach_scripted_block));
    send(detach_scripted);
    pump([&] { return heard("Child received detach") && !runtime.world().object(attachment.id); });

    wear_fixture = true;
    auto wear_asset = std::make_shared<framework::AssetBase>(Uuid::random(), "Wear fixture", 6, "");
    const auto owner_key = circuit->agent_id.str();
    const auto xml = "<SceneObjectGroup><SceneObjectPart><UUID>" + Uuid::random().str() +
        "</UUID><OwnerID>" + owner_key + "</OwnerID><Shape><PCode>9</PCode><LastAttachPoint>5</LastAttachPoint>"
        "</Shape><AttachedPos><X>0.25</X><Y>0</Y><Z>0</Z></AttachedPos></SceneObjectPart>"
        "<OtherParts><SceneObjectPart><UUID>" + Uuid::random().str() + "</UUID><OwnerID>" + owner_key +
        "</OwnerID><Shape><PCode>9</PCode></Shape><TaskInventory><TaskInventoryItem><ItemID>" +
        Uuid::random().str() + "</ItemID><AssetID>" + initial->full_id().str() + "</AssetID><OwnerID>" +
        owner_key + "</OwnerID><Type>10</Type><InvType>10</InvType><Name>Wear script</Name>"
        "</TaskInventoryItem></TaskInventory></SceneObjectPart></OtherParts></SceneObjectGroup>";
    wear_asset->data = std::make_shared<std::vector<std::uint8_t>>(xml.begin(), xml.end());
    assets.store(wear_asset);
    InventoryItem wear_item;
    wear_item.id = Uuid::random();
    wear_item.owner = circuit->agent_id;
    wear_item.parent = note.parent;
    wear_item.asset_id = wear_asset->full_id();
    wear_item.asset_type = wear_item.inventory_type = 6;
    wear_item.name = "Wear fixture";
    check(inventory.add_item(wear_item), "store wearable object in inventory");
    auto wear = make("RezSingleAttachmentFromInv");
    wear.block("ObjectData")["ItemID"] = wear_item.id;
    send(wear);
    pump([&] { return heard("Inventory attachment child received wear"); });
    check(heard("Inventory on_rez preceded attach"), "inventory wear posts on_rez before attach");
    const auto wearer_after_rez = runtime.viewers().agent(circuit->agent_id);
    const auto saved_wear = reopened_avatars.get_appearance(circuit->agent_id)->attachment_for_item(wear_item.id);
    const auto live_wear = wearer_after_rez->appearance()->attachment_for_item(wear_item.id);
    check(saved_wear && saved_wear->point == 5 && live_wear && live_wear->asset == wear_asset->full_id(),
          "wear persists the attachment point and tracks the live asset");
    const auto worn_objects = runtime.world().attachments(wearer_after_rez->local_id);
    const auto edited_attachment = std::ranges::find(worn_objects, wear_item.id, &SceneObject::from_inventory_item);
    check(edited_attachment != worn_objects.end() && edited_attachment->position == framework::Vector3{.25f, 0, 0},
          "wear restores the asset attachment offset");
    const std::string worn_description = "Edited while worn";
    auto worn_edit = make("ObjectDescription");
    auto worn_edit_part = network::lludp::make_block(worn_edit.spec->blocks.at(1));
    worn_edit_part["LocalID"] = std::uint64_t(edited_attachment->local_id);
    worn_edit_part["Description"] = wire(worn_description);
    worn_edit.blocks["ObjectData"].push_back(std::move(worn_edit_part));
    send(worn_edit);
    pump([&] { return runtime.world().object(edited_attachment->id)->description == worn_description; });

    auto second_item = wear_item;
    auto third_item = wear_item;
    second_item.id = Uuid::random();
    third_item.id = Uuid::random();
    check(inventory.add_item(second_item) && inventory.add_item(third_item), "store batch wear items");
    auto wear_batch = make("RezMultipleAttachmentsFromInv");
    wear_batch.block("HeaderData")["CompoundMsgID"] = Uuid::random();
    wear_batch.block("HeaderData")["TotalObjects"] = std::uint64_t(5);
    wear_batch.block("HeaderData")["FirstDetachAll"] = true;
    for (const auto id : {second_item.id, Uuid::random(), wear_item.id, third_item.id}) {
        auto block = network::lludp::make_block(wear_batch.spec->blocks.at(2));
        block["ItemID"] = id;
        block["AttachmentPt"] = std::uint64_t(128 | 5);
        wear_batch.blocks["ObjectData"].push_back(std::move(block));
    }
    const auto saves_before_batch = appearance_saves.load();
    send(wear_batch);
    pump([&] { return appearance_saves == saves_before_batch + 2; });
    const auto batch_appearance = reopened_avatars.get_appearance(circuit->agent_id);
    check(bool(batch_appearance), "read persisted batch appearance");
    const auto batch_second = batch_appearance->attachment_for_item(second_item.id);
    const auto batch_third = batch_appearance->attachment_for_item(third_item.id);
    check(batch_appearance->attachments().size() == 3 &&
              batch_appearance->attachment_for_item(wear_item.id) &&
              batch_second && batch_second->point == 5 && batch_third && batch_third->point == 5,
          "batch appends both valid items and ignores duplicate, missing, and compound header fields");
    check(runtime.close_agent_session(circuit->agent_id, circuit->session_id.str()), "close wearer's first session");
    pump([&] {
        const auto item = inventory.item(circuit->agent_id, wear_item.id);
        return item && item->asset_id != wear_asset->full_id() && item->description == worn_description &&
               attachment_scripts.load(circuit->agent_id, wear_item.id, item->asset_id).size() == 1;
    });
    auto next_circuit = std::make_shared<framework::AgentCircuitData>(*circuit);
    next_circuit->session_id = Uuid::random();
    next_circuit->secure_session_id = Uuid::random();
    next_circuit->circuit_code = 43;
    next_circuit->caps_path = "asset-owner-relogin";
    next_circuit->appearance = batch_appearance;
    circuit = std::move(next_circuit);
    presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id);
    check(runtime.create_agent(nullptr, descriptor, circuit, 128, context, reason), "readmit wearer");
    {
        std::lock_guard lock(received_mutex);
        received.clear();
    }
    auto reconnect = make("UseCircuitCode");
    reconnect.block("CircuitCode")["ID"] = circuit->agent_id;
    reconnect.block("CircuitCode")["SessionID"] = circuit->session_id;
    reconnect.block("CircuitCode")["Code"] = std::uint64_t(circuit->circuit_code);
    send(reconnect);
    pump([&] { return runtime.viewers().agent(circuit->agent_id).has_value(); });
    auto enter_again = make("CompleteAgentMovement");
    enter_again.block("AgentData")["CircuitCode"] = std::uint64_t(circuit->circuit_code);
    send(enter_again);
    pump([&] {
        const auto avatar = runtime.viewers().agent(circuit->agent_id);
        if (!avatar || !avatar->root || !heard("Inventory attachment resumed its script"))
            return false;
        const auto attachments = runtime.world().attachments(avatar->local_id);
        return attachments.size() == 3 && std::ranges::any_of(attachments, [&](const auto& part) {
            return part.from_inventory_item == wear_item.id && part.description == worn_description &&
                   part.position == framework::Vector3{.25f, 0, 0};
        });
    });
    check(appearance_saves == saves_before_batch + 2, "login restores attachments without rewriting saved appearance");
    check(inventory.item(circuit->agent_id, second_item.id)->asset_id == wear_asset->full_id(),
          "unchanged attachment retains its original asset");
    const auto before_detach = inventory.item(circuit->agent_id, wear_item.id)->asset_id;
    const auto reconnected = runtime.viewers().agent(circuit->agent_id);
    const auto relogin_attachments = runtime.world().attachments(reconnected->local_id);
    const auto detach_target = std::ranges::find(relogin_attachments, wear_item.id, &SceneObject::from_inventory_item);
    check(detach_target != relogin_attachments.end(), "find restored attachment for manual detach");
    auto manual_detach = make("DetachAttachmentIntoInv");
    manual_detach.block("ObjectData")["AgentID"] = circuit->agent_id;
    manual_detach.block("ObjectData")["ItemID"] = wear_item.id;
    send(manual_detach);
    pump([&] {
        return heard("Inventory attachment child received detach") && !runtime.world().object(detach_target->id);
    });
    const auto detached_item = inventory.item(circuit->agent_id, wear_item.id);
    check(detached_item && detached_item->asset_id != before_detach &&
              attachment_scripts.load(circuit->agent_id, wear_item.id, detached_item->asset_id).size() == 1,
          "manual detach saves edits and the script checkpoint after the detach event");
    pump([&] {
        std::lock_guard lock(received_mutex);
        return std::ranges::any_of(received, [&](const auto& packet) {
            if (packet.spec->name != "UpdateCreateInventoryItem")
                return false;
            const auto& data = packet.block("InventoryData");
            return std::get<Uuid>(data.at("ItemID")) == wear_item.id &&
                   std::get<Uuid>(data.at("AssetID")) == detached_item->asset_id &&
                   std::get<std::uint64_t>(data.at("CallbackID")) == 0;
        });
    });
    const auto detached_appearance = reopened_avatars.get_appearance(circuit->agent_id);
    check(detached_appearance && !detached_appearance->attachment_for_item(wear_item.id),
          "manual detach removes the item from the saved appearance");
    auto rewear = make("RezSingleAttachmentFromInv");
    rewear.block("ObjectData")["ItemID"] = wear_item.id;
    rewear.block("ObjectData")["AttachmentPt"] = std::uint64_t(128 | 5);
    send(rewear);
    pump([&] { return heard("Inventory attachment resumed after manual detach"); });
    const auto current_parts = runtime.world().objects();
    check(std::ranges::any_of(current_parts, [](const auto& part) {
              return part.description == "Edited by detach event";
          }), "rewear restores the edit made by the detach event");
    auto replacement_item = wear_item;
    replacement_item.id = Uuid::random();
    check(inventory.add_item(replacement_item), "store replacement attachment");
    auto replace = make("RezSingleAttachmentFromInv");
    replace.block("ObjectData")["ItemID"] = replacement_item.id;
    replace.block("ObjectData")["AttachmentPt"] = std::uint64_t(5);
    send(replace);
    const auto expected_worn_count = wear_replaces_all ? 1u : 3u;
    pump([&] {
        const auto viewer = runtime.viewers().agent(circuit->agent_id);
        const auto worn = runtime.world().attachments(viewer->local_id);
        return worn.size() == expected_worn_count &&
               std::ranges::find(worn, replacement_item.id, &SceneObject::from_inventory_item) != worn.end() &&
               std::ranges::find(worn, second_item.id, &SceneObject::from_inventory_item) == worn.end();
    });
    const auto replaced_appearance = reopened_avatars.get_appearance(circuit->agent_id);
    check(replaced_appearance && replaced_appearance->attachments().size() == 1 &&
              replaced_appearance->attachment_for_item(replacement_item.id),
          "non-append wear replaces the saved appearance slot");
    if (!wear_replaces_all) {
        const auto still_worn = runtime.world().attachments(reconnected->local_id);
        const auto survivor = std::ranges::find(still_worn, wear_item.id, &SceneObject::from_inventory_item);
        check(survivor != still_worn.end() &&
                  std::ranges::find(still_worn, third_item.id, &SceneObject::from_inventory_item) != still_worn.end(),
              "single replacement keeps the other objects attached in the scene");
        manual_detach.sequence = ++sequence;
        send(manual_detach);
        pump([&] { return !runtime.world().object(survivor->id); });
    }
    rewear.sequence = ++sequence;
    send(rewear);
    pump([&] { return heard("Inventory attachment resumed after replacement"); });
    const auto after_replacement = runtime.world().attachments(reconnected->local_id);
    const auto worn_again = std::ranges::find(after_replacement, wear_item.id, &SceneObject::from_inventory_item);
    check(worn_again != after_replacement.end(), "find reworn attachment for failed save");
    const auto drop_target = std::ranges::find(after_replacement, replacement_item.id, &SceneObject::from_inventory_item);
    check(drop_target != after_replacement.end(), "find replacement attachment for drop");
    const auto drop_child = runtime.world().link_key(drop_target->id, 2);
    auto drop = make("ObjectDrop");
    auto drop_part = network::lludp::make_block(drop.spec->blocks.at(1));
    drop_part["ObjectLocalID"] = std::uint64_t(runtime.world().object(drop_child)->local_id);
    drop.blocks["ObjectData"].push_back(std::move(drop_part));
    const auto checks_before_drop = drop_permission_checks;
    send(drop);
    pump([&] { return drop_permission_checks > checks_before_drop; });
    check(runtime.world().object(drop_target->id)->is_attachment &&
              inventory.item(circuit->agent_id, replacement_item.id).has_value(),
          "parcel denial preserves the attachment and inventory item");
    allow_drop_rez = true;
    const auto expect_drop_denied = [&](const char* message) {
        const auto checks = drop_permission_checks;
        drop.sequence = ++sequence;
        send(drop);
        pump([&] { return drop_permission_checks > checks; });
        check(runtime.world().object(drop_target->id)->is_attachment &&
                  inventory.item(circuit->agent_id, replacement_item.id).has_value(), message);
    };
    int parcel_prims = 0;
    for (const auto& group : runtime.world().land_prim_groups())
        parcel_prims += group.prims;
    drop_parcel_capacity = parcel_prims + 1;
    expect_drop_denied("one free prim does not admit a two-prim attachment");
    drop_parcel_capacity = parcel_prims + 2;
    drop_parcel_mode = 1;
    expect_drop_denied("parcel creation flags deny an attachment drop by a non-owner");
    drop_parcel_mode = 2;
    expect_drop_denied("public rez permission still respects the per-user prim limit");
    drop_parcel_mode = 0;
    drop.sequence = ++sequence;
    send(drop);
    pump([&] { return heard("Child received drop"); });
    const auto dropped = runtime.world().object(drop_target->id);
    const auto child = runtime.world().object(drop_child);
    check(dropped && child && child->parent_id == dropped->local_id && dropped->parent_id == 0 &&
              !dropped->is_attachment && dropped->attached_to_backup && dropped->shape.state == 0 &&
              dropped->last_attachment_point == 5 && dropped->saved_attachment_position == drop_target->position &&
              !inventory.item(circuit->agent_id, replacement_item.id),
          "drop preserves the linked scene object and consumes its inventory item");
    check(!reopened_avatars.get_appearance(circuit->agent_id)->attachment_for_item(replacement_item.id),
          "drop removes the item from the saved appearance");
    pump([&] {
        std::lock_guard lock(received_mutex);
        return std::ranges::any_of(received, [&](const auto& packet) {
            return packet.spec->name == "RemoveInventoryItem" &&
                   std::get<Uuid>(packet.block("InventoryData").at("ItemID")) == replacement_item.id;
        });
    });
    auto attach_ground = make("ObjectAttach");
    attach_ground.block("AgentData")["AttachmentPoint"] = std::uint64_t(128);
    auto attach_ground_part = network::lludp::make_block(attach_ground.spec->blocks.at(1));
    attach_ground_part["ObjectLocalID"] = std::uint64_t(child->local_id);
    attach_ground.blocks["ObjectData"].push_back(std::move(attach_ground_part));
    {
        std::lock_guard lock(received_mutex);
        received.clear();
    }
    send(attach_ground);
    pump([&] {
        return runtime.world().object(dropped->id)->is_attachment && heard("Inventory attachment resumed its script");
    });
    const auto attached = runtime.world().object(dropped->id);
    const auto ground_item = inventory.item(circuit->agent_id, attached->from_inventory_item);
    const auto ground_appearance = reopened_avatars.get_appearance(circuit->agent_id);
    check(ground_item && ground_item->id != replacement_item.id && attached->local_id == dropped->local_id &&
              !attached->attached_to_backup && attached->shape.state == 5 &&
              attached->position == drop_target->position &&
              runtime.world().object(drop_child)->parent_id == attached->local_id &&
              ground_appearance->attachment_for_item(ground_item->id),
          "ground attach keeps the live linkset, restores the point and offset, and creates a worn inventory item");
    auto duplicate_source = runtime.world().add(SceneObject::primitive(circuit->agent_id, {170, 170, 30}));
    check(duplicate_source.has_value(), "add duplication capacity fixture");
    auto duplicate_child = SceneObject::primitive(circuit->agent_id, {1, 0, 0});
    duplicate_child.parent_id = duplicate_source->local_id;
    check(runtime.world().add(duplicate_child).has_value(), "link second prim to duplication capacity fixture");
    int occupied_prims = 0;
    for (const auto& group : runtime.world().land_prim_groups())
        occupied_prims += group.prims;
    const auto expect_rez_denial = [&](const auto& packet, const char* message,
                                      const char* alert_message = "Unable to rez object because the parcel is full") {
        const auto object_count = runtime.world().size();
        {
            std::lock_guard lock(received_mutex);
            received.clear();
        }
        send(packet);
        pump([&] { return packet_seen("AlertMessage", alert_message); });
        check(runtime.world().size() == object_count, message);
    };
    drop_parcel_capacity = occupied_prims + 1;
    auto duplicate = make("ObjectDuplicate");
    duplicate.block("SharedData")["Offset"] = std::vector<double>{0, 4, 0};
    duplicate.blocks["ObjectData"] = {{{"ObjectLocalID", std::uint64_t{duplicate_source->local_id}}}};
    expect_rez_denial(duplicate, "one slot cannot duplicate a two-prim linkset");
    auto rez_linkset = make("RezObject");
    rez_linkset.block("InventoryData")["ItemID"] = ground_item->id;
    rez_linkset.block("RezData")["RayStart"] = std::vector<double>{170, 170, 40};
    rez_linkset.block("RezData")["RayEnd"] = std::vector<double>{170, 170, 30};
    rez_linkset.block("RezData")["RayEndIsIntersection"] = true;
    expect_rez_denial(rez_linkset, "one slot cannot rez a two-prim inventory object");
    check(inventory.item(circuit->agent_id, ground_item->id).has_value(), "capacity denial preserves inventory");
    drop_parcel_capacity = occupied_prims;
    auto add = make("ObjectAdd");
    auto& add_shape = add.block("ObjectData");
    add_shape["PCode"] = std::uint64_t{9};
    add_shape["PathCurve"] = std::uint64_t{16};
    add_shape["ProfileCurve"] = std::uint64_t{1};
    add_shape["PathScaleX"] = add_shape["PathScaleY"] = std::uint64_t{100};
    add_shape["Scale"] = std::vector<double>{.5, .5, .5};
    add_shape["Rotation"] = std::vector<double>{0, 0, 0, 1};
    add_shape["RayStart"] = std::vector<double>{170, 170, 40};
    add_shape["RayEnd"] = std::vector<double>{170, 170, 30};
    add_shape["RayEndIsIntersection"] = std::uint64_t{1};
    expect_rez_denial(add, "full parcel rejects new prim");
    drop_parcel_capacity = 15000;
    auto tree = add;
    tree.sequence = ++sequence;
    tree.block("ObjectData")["PCode"] = std::uint64_t{111};
    tree.block("ObjectData")["State"] = std::uint64_t{7};
    send(tree);
    std::optional<SceneObject> rezzed_tree;
    pump([&] {
        const auto objects = runtime.world().objects();
        const auto found = std::ranges::find_if(objects, [](const auto& object) {
            return object.shape.pcode == 111;
        });
        if (found == objects.end())
            return false;
        rezzed_tree = *found;
        std::lock_guard lock(received_mutex);
        return std::ranges::any_of(received, [&](const auto& packet) {
            if (packet.spec->name != "ObjectUpdate")
                return false;
            return std::ranges::any_of(packet.blocks.at("ObjectData"), [&](const auto& row) {
                return std::get<Uuid>(row.at("FullID")) == rezzed_tree->id &&
                       std::get<std::vector<std::uint8_t>>(row.at("Data")) == std::vector<std::uint8_t>{7};
            });
        });
    });
    check(rezzed_tree->scale == framework::Vector3{4, 4, 10} && (rezzed_tree->flags & 0x400u) &&
              rezzed_tree->last_attachment_point == 7,
          "Cypress rez preserves the managed scale, phantom flag, and last attachment point");
    auto rez_above_tree = add;
    rez_above_tree.sequence = ++sequence;
    auto& ray = rez_above_tree.block("ObjectData");
    ray["RayTargetID"] = Uuid::random(); // An unavailable target triggers the scene-wide fallback.
    ray["RayEndIsIntersection"] = std::uint64_t{0};
    ray["RayStart"] = std::vector<double>{170, 170, 48};
    ray["RayEnd"] = std::vector<double>{170, 170, 26};
    const auto before_tree_ray = runtime.world().objects();
    send(rez_above_tree);
    Uuid ray_prim;
    pump([&] {
        const auto objects = runtime.world().objects();
        for (const auto& object : objects) {
            if (std::ranges::find(before_tree_ray, object.id, &SceneObject::id) != before_tree_ray.end())
                continue;
            check(object.position.z == rezzed_tree->position.z + 5.f,
                  "scene-wide rez ray intersects the top of the phantom Cypress");
            ray_prim = object.id;
            return true;
        }
        return false;
    });
    runtime.world().remove(ray_prim);
    runtime.world().remove(rezzed_tree->id);
    ObjectAssetPart plant_asset_part;
    plant_asset_part.object = *rezzed_tree;
    const auto plant_asset = std::make_shared<framework::AssetBase>(
        Uuid::random(), "Cypress", 6, circuit->agent_id.str());
    plant_asset->data = std::make_shared<std::vector<std::uint8_t>>(serialize_object_asset(plant_asset_part));
    const auto stored_plant = assets.store(plant_asset);
    check(stored_plant.has_value(), "store Cypress asset");
    const auto parsed_plant = read_object_asset_xml(*plant_asset->data);
    check(parsed_plant && parsed_plant->linksets.front().parts.front().object.shape.pcode == 111 &&
              parsed_plant->linksets.front().parts.front().object.scale == framework::Vector3{4, 4, 10},
          "serialized Cypress preserves vegetation pcode and scale");
    auto plant_item = wear_item;
    plant_item.id = Uuid::random();
    plant_item.asset_id = Uuid(*stored_plant);
    plant_item.parent = inventory.root(circuit->agent_id)->id;
    plant_item.name = "Cypress";
    plant_item.current_permissions |= 1u << 15;
    check(inventory.add_item(plant_item), "store Cypress inventory item");
    const auto loaded_plant = inventory.item(circuit->agent_id, plant_item.id);
    check(loaded_plant && loaded_plant->asset_type == 6 &&
              assets.get_asset(loaded_plant->asset_id.str()),
          "Cypress inventory item resolves to a stored object asset");
    auto rez_plant = make("RezObject");
    rez_plant.block("InventoryData")["ItemID"] = plant_item.id;
    rez_plant.block("RezData")["RayStart"] = std::vector<double>{128, 128, 70};
    rez_plant.block("RezData")["RayEnd"] = std::vector<double>{128, 128, 21};
    rez_plant.block("RezData")["RayEndIsIntersection"] = true;
    {
        std::lock_guard lock(received_mutex);
        received.clear();
    }
    send(rez_plant);
    await_ack(rez_plant.sequence);
    Uuid inventory_plant_id;
    pump([&] {
        {
            std::lock_guard lock(received_mutex);
            for (const auto& packet : received) {
                if (packet.spec->name != "AlertMessage")
                    continue;
                const auto& bytes = std::get<std::vector<std::uint8_t>>(packet.block("AlertData").at("Message"));
                throw std::runtime_error("Cypress inventory rez: " + std::string(bytes.begin(), bytes.end()));
            }
        }
        const auto objects = runtime.world().objects();
        const auto found = std::ranges::find_if(objects, [&](const auto& object) {
            return object.shape.pcode == 111 && object.id != rezzed_tree->id;
        });
        if (found == objects.end())
            return false;
        check(found->shape.state == 7 && found->scale == framework::Vector3{4, 4, 10},
              "inventory rez preserves Cypress species and stored scale");
        inventory_plant_id = found->id;
        return true;
    });
    runtime.world().remove(inventory_plant_id);
    auto wear_plant = make("RezSingleAttachmentFromInv");
    wear_plant.block("ObjectData")["ItemID"] = plant_item.id;
    send(wear_plant);
    pump([&] {
        return packet_seen("AgentAlertMessage",
                           "/You cannot wear system plants. They could grow roots inside your avatar");
    });
    const auto plant_wearer = runtime.viewers().agent(circuit->agent_id);
    check(plant_wearer && !plant_wearer->appearance()->attachment_for_item(plant_item.id) &&
              inventory.item(circuit->agent_id, plant_item.id).has_value(),
          "wear denial preserves the plant inventory item and leaves it unworn");
    drop_parcel_mode = 1;
    add.sequence = ++sequence;
    expect_rez_denial(add, "restricted parcel rejects creation", "You cannot create objects here.");
    rez_linkset.sequence = ++sequence;
    expect_rez_denial(rez_linkset, "restricted parcel rejects inventory rez", "You cannot rez objects here.");
    check(inventory.item(circuit->agent_id, ground_item->id).has_value(), "parcel denial preserves inventory");
    drop_parcel_mode = 3;
    add_shape["RayStart"] = std::vector<double>{245, 170, 40};
    add_shape["RayEnd"] = std::vector<double>{245, 170, 30};
    rez_linkset.block("RezData")["RayStart"] = std::vector<double>{245, 170, 40};
    rez_linkset.block("RezData")["RayEnd"] = std::vector<double>{245, 170, 30};
    const auto before_public_rez = runtime.world().objects();
    add.sequence = ++sequence;
    send(add);
    pump([&] { return runtime.world().size() == before_public_rez.size() + 1; });
    const auto after_public_rez = runtime.world().objects();
    const auto public_prim = std::ranges::find_if(after_public_rez, [&](const auto& part) {
        return std::ranges::find(before_public_rez, part.id, &SceneObject::id) == before_public_rez.end();
    });
    check(public_prim != after_public_rez.end() && public_prim->owner == circuit->agent_id,
          "public parcel admits a prim at the per-user limit");
    add.sequence = ++sequence;
    expect_rez_denial(add, "public parcel rejects new prim above the per-user limit",
                      "Unable to rez object because you have reached your limit");
    rez_linkset.sequence = ++sequence;
    expect_rez_denial(rez_linkset, "public parcel rejects inventory linkset above the per-user limit",
                      "Unable to rez object because you have reached your limit");
    check(inventory.item(circuit->agent_id, ground_item->id).has_value(),
          "per-user limit denial preserves inventory");
    check(runtime.world().remove(public_prim->id), "remove the prim added at the per-user limit");
    add_shape["RayStart"] = std::vector<double>{170, 170, 40};
    add_shape["RayEnd"] = std::vector<double>{170, 170, 30};
    rez_linkset.block("RezData")["RayStart"] = std::vector<double>{170, 170, 40};
    rez_linkset.block("RezData")["RayEnd"] = std::vector<double>{170, 170, 30};
    drop_parcel_mode = 0;
    drop_parcel_capacity = occupied_prims;
    split_parcel = true;
    auto move_to_parcel = make("ObjectPosition");
    auto move_part = network::lludp::make_block(move_to_parcel.spec->blocks.at(1));
    move_part["ObjectLocalID"] = std::uint64_t{duplicate_source->local_id};
    move_part["Position"] = std::vector<double>{181, 170, 30};
    move_to_parcel.blocks["ObjectData"].push_back(std::move(move_part));
    expect_rez_denial(move_to_parcel, "parcel entry preserves the linkset");
    check(runtime.world().object(duplicate_source->id)->position == duplicate_source->position,
          "full neighboring parcel rejects movement");
    move_to_parcel.sequence = ++sequence;
    move_to_parcel.block("ObjectData")["Position"] = std::vector<double>{171, 170, 30};
    send(move_to_parcel);
    pump([&] {
        return runtime.world().object(duplicate_source->id)->position == framework::Vector3{171, 170, 30};
    });
    const std::string entry_script_source = R"LSL(
default
{
    state_entry()
    {
        llSetPos(<181, 170, 30>);
        llSetObjectDesc("Script tried parcel entry");
    }
}
)LSL";
    auto entry_script = std::make_shared<framework::AssetBase>(
        Uuid::random(), "Parcel entry script", 10, circuit->agent_id.str());
    entry_script->data = std::make_shared<std::vector<std::uint8_t>>(
        entry_script_source.begin(), entry_script_source.end());
    check(assets.store(entry_script).has_value(), "store parcel entry script");
    auto entry_task = std::make_shared<framework::TaskInventoryItem>();
    entry_task->item_id = Uuid::random();
    entry_task->asset_id = entry_script->full_id();
    entry_task->parent_id = entry_task->parent_part_id = duplicate_source->id;
    entry_task->owner_id = entry_task->creator_id = circuit->agent_id;
    entry_task->type = entry_task->inventory_type = 10;
    entry_task->name = "Parcel entry script";
    runtime.restore_task_inventory(duplicate_source->id, {entry_task});
    pump([&] { return runtime.world().object(duplicate_source->id)->description == "Script tried parcel entry"; });
    check(runtime.world().object(duplicate_source->id)->position == framework::Vector3{171, 170, 30},
          "a script cannot move a linkset into the full neighboring parcel");

    const auto arriving_root = Uuid::random();
    const auto arriving_child = Uuid::random();
    const auto crossing_part = [&](Uuid id) {
        return "<SceneObjectPart><UUID>" + id.str() + "</UUID><OwnerID>" + circuit->agent_id.str() +
               "</OwnerID><Shape><PCode>9</PCode></Shape></SceneObjectPart>";
    };
    const auto crossing_xml = "<SceneObjectGroup>" + crossing_part(arriving_root) + "<OtherParts>" +
                              crossing_part(arriving_child) + "</OtherParts></SceneObjectGroup>";
    const auto transfer_scene = runtime.get_scene(descriptor.id);
    check(bool(transfer_scene), "find destination transfer scene");
    const auto transfer = std::dynamic_pointer_cast<SimulationObject>(transfer_scene->deserialize_object(crossing_xml));
    check(bool(transfer), "deserialize incoming two-prim linkset");
    const framework::Vector3 arrival{181, 172, 30};
    drop_parcel_capacity = occupied_prims + 1;
    check(!runtime.create_object(&descriptor, arrival, transfer, true) &&
              !runtime.world().object(arriving_root) && !runtime.world().object(arriving_child),
          "one free prim slot rejects an entire two-prim transfer");
    drop_parcel_capacity = occupied_prims + 2;
    check(runtime.create_object(&descriptor, arrival, transfer, true), "two free slots admit the same transfer");
    const auto arrived_root = runtime.world().object(arriving_root);
    const auto arrived_child = runtime.world().object(arriving_child);
    check(arrived_root && arrived_child && arrived_root->position == arrival &&
              arrived_child->parent_id == arrived_root->local_id,
          "accepted transfer preserves the linkset in the destination parcel");
    split_parcel = false;
    drop_parcel_capacity = 15000;
    ObjectAssetPart crossing_plant;
    crossing_plant.object = *rezzed_tree;
    const auto crossing_plant_bytes = serialize_object_asset(crossing_plant);
    const std::string crossing_plant_xml(crossing_plant_bytes.begin(), crossing_plant_bytes.end());
    const auto plant_transfer = std::dynamic_pointer_cast<SimulationObject>(
        transfer_scene->deserialize_object(crossing_plant_xml));
    check(plant_transfer && runtime.create_object(&descriptor, arrival, plant_transfer, true),
          "destination accepts a standalone Cypress transfer");
    const auto arrived_plant = runtime.world().object(rezzed_tree->id);
    check(arrived_plant && arrived_plant->shape.pcode == 111 && arrived_plant->shape.state == 7 &&
              arrived_plant->scale == rezzed_tree->scale && arrived_plant->position == arrival &&
              (arrived_plant->flags & 0x400u),
          "incoming vegetation preserves species, scale, position and phantom status");
    runtime.world().remove(rezzed_tree->id);
    const auto store_script = [&](const std::string& name, std::string_view source) {
        auto asset = std::make_shared<framework::AssetBase>(Uuid::random(), name, 10, circuit->agent_id.str());
        asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
        check(assets.store(asset).has_value(), "store scripted rez fixture source");
        return asset->full_id();
    };
    const std::string_view spawned_source = R"LSL(
default
{
    on_rez(integer parameter)
    {
        if (parameter == llGetStartParameter() && (parameter == 17 || parameter == 19))
            llSetObjectDesc("Rez parameter verified");
        else if (parameter == 0)
            llSetObjectDesc("Unexpected viewer on_rez");
    }
    changed(integer reason)
    {
        if (reason == CHANGED_REGION_START && llGetStartParameter() == 17)
            llSetObjectDesc("Restart restored rez parameter");
        else if (reason == CHANGED_REGION_START && llGetStartParameter() == 0)
            llSetObjectDesc("Started after scripts were enabled");
    }
}
)LSL";
    const auto spawned_script = store_script("Rez parameter", spawned_source);
    const auto owner_text = circuit->agent_id.str();
    const std::string scripted_asset_xml =
        "<SceneObjectGroup><SceneObjectPart><UUID>" + Uuid::random().str() +
        "</UUID><OwnerID>" + owner_text + "</OwnerID><Shape><PCode>9</PCode></Shape>"
        "<TaskInventory><TaskInventoryItem><ItemID>" + Uuid::random().str() + "</ItemID><AssetID>" +
        spawned_script.str() + "</AssetID><OwnerID>" + owner_text +
        "</OwnerID><Type>10</Type><InvType>10</InvType><Name>Rez parameter</Name>"
        "</TaskInventoryItem></TaskInventory></SceneObjectPart><OtherParts><SceneObjectPart><UUID>" +
        Uuid::random().str() + "</UUID><OwnerID>" + owner_text +
        "</OwnerID><Shape><PCode>9</PCode></Shape><OffsetPosition><X>2</X><Y>0</Y><Z>0</Z></OffsetPosition>"
        "</SceneObjectPart></OtherParts></SceneObjectGroup>";
    auto scripted_asset = std::make_shared<framework::AssetBase>(Uuid::random(), "Scripted linkset", 6, owner_text);
    auto parsed_scripted_asset = read_object_asset_xml({
        reinterpret_cast<const std::uint8_t*>(scripted_asset_xml.data()), scripted_asset_xml.size()});
    check(parsed_scripted_asset && parsed_scripted_asset->linksets.front().parts.size() == 2,
          "read scripted rez linkset");
    auto& scripted_parts = parsed_scripted_asset->linksets.front().parts;
    auto stopped_task = scripted_parts.front().inventory.front();
    stopped_task.reset_ids(scripted_parts.back().object.id);
    stopped_task.script_running = false;
    scripted_parts.back().inventory.push_back(stopped_task);
    const auto stopped_program = script::compile_lsl(spawned_source);
    check(bool(stopped_program), "compile stopped script checkpoint fixture");
    script::LslContext stopped_context;
    stopped_context.start_parameter = 99;
    script::LslInstance stopped_instance(stopped_program.program, stopped_context, script::LslHost{}, false);
    auto stopped_checkpoint = stopped_instance.checkpoint();
    check(stopped_checkpoint && !stopped_checkpoint->running, "snapshot stopped script fixture");
    scripted_parts.back().native_script_checkpoints.emplace(stopped_task.item_id, std::move(*stopped_checkpoint));
    scripted_asset->data = std::make_shared<std::vector<std::uint8_t>>(
        serialize_object_asset(scripted_parts.front(), std::span(scripted_parts).subspan(1)));
    check(assets.store(scripted_asset).has_value(), "store scripted rez object");
    const auto rez_task = [&](const std::string& name, Uuid asset, std::int32_t type, std::uint32_t permissions) {
        auto item = std::make_shared<framework::TaskInventoryItem>();
        item->item_id = Uuid::random();
        item->asset_id = asset;
        item->parent_id = item->parent_part_id = duplicate_source->id;
        item->owner_id = item->creator_id = circuit->agent_id;
        item->type = item->inventory_type = type;
        item->name = name;
        item->current_permissions = permissions;
        return item;
    };
    const std::string permission = wear_replaces_all ? "PERMISSION_ATTACH" : "PERMISSION_TRIGGER_ANIMATION";
    const auto permission_script = store_script("Automatic permission grant",
        "default { state_entry() { llRequestPermissions(llGetOwner(), " + permission + "); }"
        " run_time_permissions(integer mask) { if (mask == " + permission +
        " && llGetPermissions() == mask && llGetPermissionsKey() == llGetOwner())"
        " llSetObjectDesc(\"Automatic permission granted\"); } }");
    const auto permission_object = runtime.world().add(
        SceneObject::primitive(circuit->agent_id, {194, 170, 30}));
    check(permission_object.has_value(), "add automatic permission test object");
    const auto permission_task = rez_task("Automatic permission grant", permission_script, 10, 0);
    permission_task->reset_ids(permission_object->id);
    runtime.restore_task_inventory(permission_object->id, {permission_task});
    pump([&] {
        const auto object = runtime.world().object(permission_object->id);
        return object && object->description == "Automatic permission granted";
    });
    runtime.world().remove(permission_object->id);
    if (!wear_replaces_all) {
        const auto revoke_script = store_script("Viewer revokes animation permission", R"LSL(
default {
    state_entry() { llRequestPermissions(llGetOwner(), PERMISSION_TRIGGER_ANIMATION); }
    run_time_permissions(integer mask) {
        llSetObjectDesc("Permission granted");
        llSetTimerEvent(0.1);
    }
    timer() {
        if (llGetPermissions() == 0 && llGetPermissionsKey() == NULL_KEY) {
            llSetObjectDesc("Permission revoked");
            llSetTimerEvent(0.0);
        } else {
            llSetObjectDesc("Permission retained");
        }
    }
}
)LSL");
        const auto object = runtime.world().add(SceneObject::primitive(circuit->agent_id, {194, 170, 30}));
        check(object.has_value(), "add viewer permission revocation object");
        const auto task = rez_task("Viewer revokes animation permission", revoke_script, 10, 0);
        task->reset_ids(object->id);
        runtime.restore_task_inventory(object->id, {task});
        const auto has_description = [&](std::string_view expected) {
            const auto prim = runtime.world().object(object->id);
            return prim && prim->description == expected;
        };
        pump([&] { return has_description("Permission granted") || has_description("Permission retained"); });
        const auto revoke = [&](Uuid target) {
            auto packet = make("RevokePermissions");
            packet.block("Data")["ObjectID"] = target;
            packet.block("Data")["ObjectPermissions"] = std::uint64_t{16};
            send(packet);
            return packet.sequence;
        };
        await_ack(revoke(object->id));
        const auto check_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        pump([&] {
            check(!has_description("Permission revoked"), "object-targeted viewer revocation preserves the grant");
            return std::chrono::steady_clock::now() >= check_at && has_description("Permission retained");
        });
        auto teleport = make("TeleportLocationRequest");
        auto& destination = teleport.block("Info");
        destination["RegionHandle"] = permission_destination.handle();
        destination["Position"] = std::vector<double>{128, 128, 30};
        destination["LookAt"] = std::vector<double>{0, 1, 0};
        send(teleport);
        pump([&] {
            std::lock_guard lock(received_mutex);
            return std::ranges::any_of(received, [&](const auto& packet) {
                return packet.spec->name == "TeleportFinish" &&
                       std::get<std::uint64_t>(packet.block("Info").at("RegionHandle")) ==
                           permission_destination.handle();
            });
        });
        await_ack(revoke(descriptor.id));
        const auto transfer_check_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        pump([&] {
            check(!has_description("Permission revoked"), "pending teleport preserves the permission");
            return std::chrono::steady_clock::now() >= transfer_check_at;
        });
        auto cancel = make("TeleportCancel");
        cancel.block("Info")["AgentID"] = circuit->agent_id;
        cancel.block("Info")["SessionID"] = circuit->session_id;
        send(cancel);
        pump([&] { return permission_teleport_cancelled.load(); });
        check(has_description("Permission retained"), "pending teleport preserves the permission");
        revoke(descriptor.id);
        pump([&] { return has_description("Permission revoked"); });
        runtime.world().remove(object->id);
    }
    if (wear_replaces_all) {
        const auto release_script = store_script("Control permission reassignment",
            "default { state_entry() { llRequestPermissions(llGetOwner(), PERMISSION_TAKE_CONTROLS); }"
            " run_time_permissions(integer mask) {"
            " if (mask & PERMISSION_TAKE_CONTROLS) {"
            " llTakeControls(CONTROL_FWD, TRUE, FALSE);"
            " llRequestPermissions(\"" + offline_owner.str() + "\", PERMISSION_TAKE_CONTROLS);"
            " } else if (llGetPermissions() == 0) { llSetObjectDesc(\"Controls released\"); } } }");
        const auto object = runtime.world().add(SceneObject::primitive(circuit->agent_id, {194, 170, 30}));
        check(object.has_value(), "add control reassignment test object");
        const auto task = rez_task("Control permission reassignment", release_script, 10, 0);
        task->reset_ids(object->id);
        runtime.restore_task_inventory(object->id, {task});
        pump([&] {
            std::lock_guard lock(received_mutex);
            return std::ranges::any_of(received, [&](const auto& packet) {
                return packet.spec->name == "ScriptQuestion" &&
                       std::get<Uuid>(packet.block("Data").at("ItemID")) == task->item_id;
            });
        });
        auto reply = make("ScriptAnswerYes");
        reply.block("Data")["TaskID"] = object->id;
        reply.block("Data")["ItemID"] = task->item_id;
        reply.block("Data")["Questions"] = std::int64_t{4};
        send(reply);
        pump([&] {
            const auto prim = runtime.world().object(object->id);
            return prim && prim->description == "Controls released";
        });
        runtime.world().remove(object->id);

        const auto control_script = store_script("Control permission denial", R"LSL(
default {
    state_entry() { llRequestPermissions(llGetOwner(), PERMISSION_TAKE_CONTROLS); }
    run_time_permissions(integer mask) {
        if (mask & PERMISSION_TAKE_CONTROLS) {
            llSetObjectDesc((string)mask);
            llTakeControls(CONTROL_FWD, TRUE, FALSE);
            llRequestPermissions(llGetOwner(), PERMISSION_TAKE_CONTROLS);
        } else {
            llSetObjectDesc("Control permission denied");
        }
    }
}
)LSL");
        const auto companion_script = store_script("Companion permission listener", R"LSL(
default {
    state_entry() {
        llRequestPermissions(llGetOwner(), PERMISSION_TRIGGER_ANIMATION);
        llRequestPermissions(llGetOwner(), 0);
    }
    run_time_permissions(integer mask) { llSetObjectName((string)mask); }
}
)LSL");
        const auto control_object = runtime.world().add(
            SceneObject::primitive(circuit->agent_id, {194, 170, 30}));
        check(control_object.has_value(), "add control permission test object");
        const auto control_task = rez_task("Control permission denial", control_script, 10, 0);
        control_task->reset_ids(control_object->id);
        const auto companion_task = rez_task("Companion permission listener", companion_script, 10, 0);
        companion_task->reset_ids(control_object->id);
        std::size_t first_packet;
        {
            std::lock_guard lock(received_mutex);
            first_packet = received.size();
        }
        runtime.restore_task_inventory(control_object->id, {control_task, companion_task});
        const auto question_count = [&](const Uuid& item_id) {
            return std::count_if(received.begin() + first_packet, received.end(), [&](const auto& packet) {
                return packet.spec->name == "ScriptQuestion" &&
                       std::get<Uuid>(packet.block("Data").at("ItemID")) == item_id;
            });
        };
        const auto answer = [&](const Uuid& task_id, const Uuid& item_id, int permissions) {
            auto packet = make("ScriptAnswerYes");
            packet.block("Data")["TaskID"] = task_id;
            packet.block("Data")["ItemID"] = item_id;
            packet.block("Data")["Questions"] = std::int64_t{permissions};
            send(packet);
            return packet.sequence;
        };
        pump([&] {
            const auto object = runtime.world().object(control_object->id);
            if (!object || object->name != "0")
                return false;
            std::lock_guard lock(received_mutex);
            return question_count(control_task->item_id) == 1 &&
                   question_count(companion_task->item_id) == 1;
        });
        constexpr int take_controls_permission = 4;
        constexpr int trigger_animation_permission = 16;
        constexpr std::uint64_t forward_control = 1;
        const auto wrong_task_reply = answer(duplicate_source->id, control_task->item_id, 0);
        await_ack(wrong_task_reply);
        const auto submitted_permissions = take_controls_permission | trigger_animation_permission;
        answer(control_object->id, Uuid::random(), submitted_permissions);
        pump([&] {
            const auto object = runtime.world().object(control_object->id);
            if (!object || object->description != std::to_string(submitted_permissions) ||
                object->name != std::to_string(submitted_permissions))
                return false;
            std::lock_guard lock(received_mutex);
            return question_count(control_task->item_id) >= 2 &&
                   std::any_of(received.begin() + first_packet, received.end(), [&](const auto& packet) {
                       if (packet.spec->name != "ScriptControlChange")
                           return false;
                       const auto& data = packet.blocks.at("Data").front();
                       return std::get<std::uint64_t>(data.at("Controls")) == forward_control &&
                              std::get<bool>(data.at("TakeControls"));
                   });
        });
        {
            std::lock_guard lock(received_mutex);
            first_packet = received.size();
        }
        answer(control_object->id, control_task->item_id, 0);
        pump([&] {
            const auto object = runtime.world().object(control_object->id);
            if (!object || object->description != "Control permission denied")
                return false;
            std::lock_guard lock(received_mutex);
            return std::any_of(received.begin() + first_packet, received.end(), [&](const auto& packet) {
                if (packet.spec->name != "ScriptControlChange")
                    return false;
                const auto& data = packet.blocks.at("Data").front();
                return std::get<std::uint64_t>(data.at("Controls")) == forward_control &&
                       !std::get<bool>(data.at("TakeControls"));
            });
        });
        runtime.world().remove(control_object->id);

        const auto owner_script = store_script("Permission prompt owner",
            "default { state_entry() { llRequestPermissions(\"" + circuit->agent_id.str() +
            "\", PERMISSION_TRIGGER_ANIMATION); } }");
        const auto check_prompt_owner = [&](Uuid owner, Uuid group, std::u16string_view expected_name) {
            auto prim = SceneObject::primitive(owner, {194, 170, 30});
            prim.group = group;
            const auto object = runtime.world().add(std::move(prim));
            check(object.has_value(), "add permission test object");
            const auto task = rez_task("Permission prompt owner", owner_script, 10, 0);
            task->owner_id = owner;
            task->reset_ids(object->id);
            runtime.restore_task_inventory(object->id, {task});
            pump([&] {
                std::lock_guard lock(received_mutex);
                return std::ranges::any_of(received, [&](const auto& packet) {
                    if (packet.spec->name != "ScriptQuestion")
                        return false;
                    const auto& data = packet.block("Data");
                    return std::get<Uuid>(data.at("ItemID")) == task->item_id &&
                           framework::sdk_bytes_to_string(
                               std::get<std::vector<std::uint8_t>>(data.at("ObjectOwner"))) == expected_name;
                });
            });
            runtime.world().remove(object->id);
        };
        check_prompt_owner(permission_group, permission_group, u"Permission Test Group");
        check_prompt_owner(offline_owner, {}, u"Offline Owner");
    }
    const auto spawner = store_script("Scripted rez", R"LSL(
integer received;
default
{
    state_entry()
    {
        llRezObject("Centered", <174, 170, 30>, ZERO_VECTOR, ZERO_ROTATION, 17);
        llRezAtRoot("Rooted", <175, 170, 30>, ZERO_VECTOR, ZERO_ROTATION, 19);
    }
    object_rez(key object)
    {
        if (object != NULL_KEY && ++received == 2)
            llSetObjectDesc("Script received both object_rez events");
    }
}
)LSL");
    runtime.restore_task_inventory(duplicate_source->id,
        {rez_task("Centered", scripted_asset->full_id(), 6, 0),
         rez_task("Rooted", scripted_asset->full_id(), 6, 1u << 15),
         rez_task("Scripted rez", spawner, 10, 0)});
    pump([&] {
        const auto root = runtime.world().object(duplicate_source->id);
        if (root->description != "Script received both object_rez events")
            return false;
        const auto objects = runtime.world().objects();
        const auto placed = [&](framework::Vector3 position) {
            return std::ranges::any_of(objects, [&](const auto& part) {
                return part.parent_id == 0 && part.rezzer == duplicate_source->id &&
                       part.position == position && part.description == "Rez parameter verified";
            });
        };
        return placed({173, 170, 30}) && placed({175, 170, 30});
    });
    const auto source_inventory = runtime.snapshot_task_inventory(duplicate_source->id);
    check(source_inventory && std::ranges::none_of(*source_inventory, [](const auto& item) {
              return item.name == "Centered";
          }) && std::ranges::any_of(*source_inventory, [](const auto& item) {
              return item.name == "Rooted";
          }), "scripted rez consumes only the no-copy task item");
    const auto rezzed_parts = runtime.world().objects();
    const auto checkpoint_root = std::ranges::find_if(rezzed_parts, [&](const auto& part) {
        return part.rezzer == duplicate_source->id && part.position == framework::Vector3{173, 170, 30};
    });
    check(checkpoint_root != rezzed_parts.end(), "find centered object for script restore");
    const auto stopped_child = runtime.world().link_key(checkpoint_root->id, 2);
    const auto stopped_states = runtime.snapshot_task_script_states(stopped_child);
    check(stopped_states.size() == 1, "snapshot stopped script on the rezzed child");
    const auto rezzed_stopped = script::decode_lsl_checkpoint(stopped_states.begin()->second);
    check(rezzed_stopped && !rezzed_stopped->running && rezzed_stopped->start_parameter == 17,
          "a rezzed stopped script retains its running flag and receives the new rez parameter");
    const auto checkpoint_tasks = runtime.snapshot_task_inventory(checkpoint_root->id);
    const auto checkpoint_states = runtime.snapshot_task_script_states(checkpoint_root->id);
    check(checkpoint_tasks && checkpoint_tasks->size() == 1 && checkpoint_states.size() == 1,
          "snapshot rezzed object task inventory and checkpoint");
    auto restart_part = SceneObject::primitive(circuit->agent_id, {173, 172, 30});
    const auto restored_part = runtime.world().add(restart_part);
    check(restored_part.has_value(), "create part for region-start script restore");
    auto restarted_task = std::make_shared<framework::TaskInventoryItem>(checkpoint_tasks->front());
    restarted_task->reset_ids(restored_part->id);
    runtime.restore_task_inventory(restored_part->id, {restarted_task},
        {{restarted_task->item_id, checkpoint_states.begin()->second}},
        RegionRuntime::TaskInventoryRestoreSource::RegionStart);
    pump([&] { return runtime.world().object(restored_part->id)->description == "Restart restored rez parameter"; });

    auto deferred_item = *ground_item;
    deferred_item.id = Uuid::random();
    deferred_item.asset_id = scripted_asset->full_id();
    deferred_item.name = "Rez with scripts disabled";
    check(inventory.add_item(deferred_item), "store the object for viewer rez with scripts disabled");
    runtime.set_scripts_enabled(false);
    auto deferred_rez = make("RezObject");
    deferred_rez.block("InventoryData")["ItemID"] = deferred_item.id;
    deferred_rez.block("RezData")["RayStart"] = std::vector<double>{168, 166, 40};
    deferred_rez.block("RezData")["RayEnd"] = std::vector<double>{168, 166, 30};
    deferred_rez.block("RezData")["RayEndIsIntersection"] = true;
    send(deferred_rez);
    Uuid deferred_root;
    pump([&] {
        for (const auto& part : runtime.world().objects()) {
            if (part.name == deferred_item.name) {
                deferred_root = part.id;
                return true;
            }
        }
        return false;
    });
    check(runtime.snapshot_task_script_states(deferred_root).empty(),
          "viewer rez does not compile scripts while region scripts are disabled");
    runtime.set_scripts_enabled(true);
    pump([&] {
        const auto part = runtime.world().object(deferred_root);
        return part && part->description == "Started after scripts were enabled";
    });
    runtime.poll();
    check(runtime.world().object(deferred_root)->description == "Started after scripts were enabled",
          "enabling scripts after viewer rez does not replay on_rez");
    runtime.world().remove(deferred_root);

    check(checkpoint_root->die_at_edge, "script-rezzed root defaults to die at edge");
    const auto override_root = std::ranges::find_if(rezzed_parts, [&](const auto& part) {
        return part.rezzer == duplicate_source->id && part.position == framework::Vector3{175, 170, 30};
    });
    check(override_root != rezzed_parts.end(), "find second script-rezzed root for status override");
    const auto override_child = runtime.world().link_key(override_root->id, 2);
    check(!override_child.is_zero(), "find child for status override");
    const auto status_script = store_script("Edge status", R"LSL(
default
{
    state_entry()
    {
        if (llGetStatus(STATUS_DIE_AT_EDGE) != 1)
            return;
        llSetStatus(STATUS_DIE_AT_EDGE, 0);
        if (llGetStatus(STATUS_DIE_AT_EDGE) != 0)
            return;
        llSetStatus(STATUS_DIE_AT_EDGE, 2);
        if (llGetStatus(STATUS_DIE_AT_EDGE) != 1)
            return;
        llSetStatus(STATUS_DIE_AT_EDGE, 0);
        llSetStatus(STATUS_PHYSICS | STATUS_PHANTOM, 1);
        if (!llGetStatus(STATUS_PHYSICS) || !llGetStatus(STATUS_PHANTOM))
            return;
        llSetStatus(STATUS_PHYSICS, 0);
        if (llGetStatus(STATUS_PHYSICS) || !llGetStatus(STATUS_PHANTOM))
            return;
        llSetScale(<11, 1, 1>);
        llSetStatus(STATUS_PHYSICS, 1);
        if (llGetStatus(STATUS_PHYSICS))
            return;
        if (llGetStatus(STATUS_CAST_SHADOWS))
            return;
        llSetStatus(STATUS_CAST_SHADOWS, 0);
        if (!llGetStatus(STATUS_CAST_SHADOWS))
            return;
        llSetObjectDesc("Child changed root edge status");
    }
}
)LSL");
    auto status_task = rez_task("Edge status", status_script, 10, 0);
    status_task->reset_ids(override_child);
    runtime.restore_task_inventory(override_child, {status_task});
    pump([&] { return runtime.world().object(override_child)->description == "Child changed root edge status"; });
    check(!runtime.world().object(override_root->id)->die_at_edge,
          "child script changes the root edge flag");
    check(!(runtime.world().object(override_root->id)->flags & World::cast_shadows_flag),
          "cast shadows affects the script's child prim");
    pump([&] {
        std::lock_guard lock(received_mutex);
        for (const auto& packet : received) {
            if (packet.spec->name != "ObjectUpdate")
                continue;
            for (const auto& part : packet.blocks.at("ObjectData")) {
                if (std::get<Uuid>(part.at("FullID")) == override_child &&
                    (std::get<std::uint64_t>(part.at("UpdateFlags")) & World::cast_shadows_flag))
                    return true;
            }
        }
        return false;
    });
    check((runtime.world().object(override_root->id)->flags & 0x401u) == 0x400u &&
              (runtime.world().object(override_child)->flags & 0x401u) == 0x400u,
          "child script changes linked physics and phantom flags and cannot enable oversized physics");
    check(runtime.world().patch_transform(override_child, {}, {}, framework::Vector3{1, 1, 1}),
          "restore eligible size before testing the linkset count");
    auto extra_shape = SceneObject::primitive(circuit->agent_id, {1, 1, 0});
    extra_shape.parent_id = override_root->local_id;
    const auto extra_shape_part = runtime.world().add(extra_shape);
    check(extra_shape_part.has_value(), "add third shape to the two-shape physics limit fixture");
    const auto count_script = store_script("Physics shape count", R"LSL(
default
{
    state_entry()
    {
        llSetStatus(STATUS_PHYSICS, 1);
        if (!llGetStatus(STATUS_PHYSICS))
            llSetObjectDesc("Physics shape count rejected");
    }
}
)LSL");
    auto count_task = rez_task("Physics shape count", count_script, 10, 0);
    count_task->reset_ids(extra_shape_part->id);
    runtime.restore_task_inventory(extra_shape_part->id, {count_task});
    pump([&] {
        return runtime.world().object(extra_shape_part->id)->description == "Physics shape count rejected";
    });
    auto physics_request = make("ObjectFlagUpdate");
    auto& physics_flags = physics_request.block("AgentData");
    physics_flags["ObjectLocalID"] = std::uint64_t{extra_shape_part->local_id};
    physics_flags["UsePhysics"] = true;
    send(physics_request);
    pump([&] {
        return packet_seen("AlertMessage",
            "Object physics cancelled because it exceeds limits for physical prims, either size or number of primswith shape type not set to None");
    });
    check((runtime.world().object(override_root->id)->flags & 1u) == 0,
          "viewer cannot enable physics with three collision shapes and a limit of two");
    auto excluded_shape = extra_shape_part->physics;
    excluded_shape.shape = 1;
    check(runtime.world().update_extra_physics(extra_shape_part->id, excluded_shape).has_value(),
          "set the third part's physics shape to None");
    physics_request.sequence = ++sequence;
    send(physics_request);
    pump([&] { return (runtime.world().object(override_root->id)->flags & 1u) != 0; });
    const auto additional_root = runtime.world().add(SceneObject::primitive(circuit->agent_id, {177, 170, 30}));
    check(additional_root.has_value(), "add collidable root to link onto physical object");
    auto over_limit_link = make("ObjectLink");
    for (const auto local : {override_root->local_id, additional_root->local_id}) {
        auto part = network::lludp::make_block(over_limit_link.spec->blocks.at(1));
        part["ObjectLocalID"] = std::uint64_t{local};
        over_limit_link.blocks["ObjectData"].push_back(std::move(part));
    }
    send(over_limit_link);
    pump([&] { return runtime.world().object(additional_root->id)->parent_id == override_root->local_id; });
    check((runtime.world().object(override_root->id)->flags & 1u) == 0 &&
              (runtime.world().object(override_child)->flags & 1u) == 0,
          "linking past the collision-shape limit succeeds and disables physics");
    const auto edge_child = runtime.world().link_key(checkpoint_root->id, 2);
    check(!edge_child.is_zero(), "find the script-rezzed child");
    auto edge_move = make("ObjectPosition");
    auto edge_part = network::lludp::make_block(edge_move.spec->blocks.at(1));
    edge_part["ObjectLocalID"] = std::uint64_t{checkpoint_root->local_id};
    edge_part["Position"] = std::vector<double>{257, 170, 30};
    edge_move.blocks["ObjectData"].push_back(std::move(edge_part));
    send(edge_move);
    pump([&] {
        return !runtime.world().object(checkpoint_root->id) && !runtime.world().object(edge_child);
    });
    const auto return_script = store_script("Return at edge", R"LSL(
default
{
    state_entry()
    {
        llSetStatus(STATUS_RETURN_AT_EDGE, 1);
        if (llGetStatus(STATUS_RETURN_AT_EDGE))
            llSetObjectDesc("Return at edge enabled");
    }
}
)LSL");
    auto return_task = rez_task("Return at edge", return_script, 10, 0);
    return_task->reset_ids(override_child);
    runtime.restore_task_inventory(override_child, {return_task});
    pump([&] {
        return runtime.world().object(override_child)->description == "Return at edge enabled";
    });
    const auto lost_and_found = inventory.for_type(circuit->agent_id, 16);
    check(lost_and_found.has_value(), "find Lost and Found for the edge return");
    const auto previously_returned = inventory.folder_items(circuit->agent_id, lost_and_found->id);
    edge_move.sequence = ++sequence;
    edge_move.block("ObjectData")["ObjectLocalID"] = std::uint64_t{override_root->local_id};
    send(edge_move);
    pump([&] { return !runtime.world().object(override_root->id); });
    const auto returned = inventory.folder_items(circuit->agent_id, lost_and_found->id);
    const auto edge_item = std::ranges::find_if(returned, [&](const auto& item) {
        return std::ranges::find(previously_returned, item.id, &InventoryItem::id) == previously_returned.end();
    });
    check(returned.size() == previously_returned.size() + 1 && edge_item != returned.end(),
          "edge return deposits a single item in Lost and Found");
    const auto edge_asset = assets.get_asset(edge_item->asset_id.str());
    const auto returned_linkset = edge_asset && edge_asset->data ? read_object_asset_xml(*edge_asset->data)
                                                               : std::nullopt;
    check(returned_linkset && returned_linkset->linksets.size() == 1 &&
              returned_linkset->linksets.front().parts.size() == 4 &&
              !runtime.world().object(override_child) && !runtime.world().object(additional_root->id),
          "edge return saves and removes the complete linked object");
    check(std::ranges::any_of(returned_linkset->linksets.front().parts, [&](const auto& part) {
              return part.object.id == override_child && (part.object.flags & World::cast_shadows_flag);
          }), "returned object asset preserves the child shadow flag");
    const auto sandbox_script = store_script("Sandbox status", R"LSL(
default
{
    state_entry()
    {
        llSetStatus(STATUS_SANDBOX | STATUS_PHYSICS, 1);
        if (llGetStatus(STATUS_SANDBOX) && llGetStatus(STATUS_PHYSICS))
            llSetObjectDesc("Sandbox enabled");
    }
}
)LSL");
    runtime.restore_task_inventory(duplicate_source->id, {rez_task("Sandbox status", sandbox_script, 10, 0)});
    pump([&] { return runtime.world().object(duplicate_source->id)->description == "Sandbox enabled"; });
    move_to_parcel.sequence = ++sequence;
    move_to_parcel.block("ObjectData")["Position"] = std::vector<double>{190, 170, 30};
    send(move_to_parcel);
    pump([&] {
        const auto root = runtime.world().object(duplicate_source->id);
        return root && root->position == framework::Vector3{181, 170, 30} && !(root->flags & 1u);
    });
    const auto sandbox_move_script = store_script("Sandbox move", R"LSL(
default
{
    state_entry()
    {
        llSetPos(<182, 170, 30>);
        llSetObjectDesc("Sandbox script move attempted");
    }
}
)LSL");
    runtime.restore_task_inventory(duplicate_source->id,
        {rez_task("Sandbox move", sandbox_move_script, 10, 0)});
    pump([&] {
        return runtime.world().object(duplicate_source->id)->description == "Sandbox script move attempted";
    });
    check(runtime.world().object(duplicate_source->id)->position == framework::Vector3{181, 170, 30},
          "sandbox rejects a script move past the viewer-clamped boundary");
    const auto grab_root = runtime.world().add(SceneObject::primitive(circuit->agent_id, {190, 170, 30}));
    check(grab_root.has_value(), "add grab test root");
    auto grab_child_part = SceneObject::primitive(circuit->agent_id, {1, 0, 0});
    grab_child_part.parent_id = grab_root->local_id;
    const auto grab_child = runtime.world().add(grab_child_part);
    check(grab_child.has_value(), "add grab test child");
    const auto set_grab_status = [&](std::string_view code, const std::string& marker) {
        const auto source = "default {\n    state_entry() {\n" + std::string(code) +
                            "        llSetObjectDesc(\"" + marker + "\");\n    }\n}\n";
        auto task = rez_task(marker, store_script(marker, source), 10, 0);
        task->reset_ids(grab_child->id);
        runtime.restore_task_inventory(grab_child->id, {task});
        pump([&] { return runtime.world().object(grab_child->id)->description == marker; });
    };
    const auto drag = [&](Uuid part, float x) {
        auto packet = make("ObjectGrabUpdate");
        packet.block("ObjectData")["ObjectID"] = part;
        packet.block("ObjectData")["GrabPosition"] = std::vector<double>{x, 170, 30};
        send(packet);
    };
    const auto expect_grab_position = [&](float x) {
        pump([&] { return runtime.world().object(grab_root->id)->position == framework::Vector3{x, 170, 30}; });
    };
    const auto expect_blocked_grab = [&](Uuid part, float from, float to) {
        drag(part, to);
        const auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        pump([&] { return std::chrono::steady_clock::now() >= settle; });
        check(runtime.world().object(grab_root->id)->position == framework::Vector3{from, 170, 30},
              "blocked grab leaves the linkset in place");
    };
    set_grab_status(R"LSL(        llSetStatus(STATUS_BLOCK_GRAB, 1);
        if (!llGetStatus(STATUS_BLOCK_GRAB))
            return;
)LSL", "Child blocks grab");
    expect_blocked_grab(grab_child->id, 190, 191);
    drag(grab_root->id, 192);
    expect_grab_position(192);
    set_grab_status(R"LSL(        llSetStatus(STATUS_BLOCK_GRAB, 0);
        llSetStatus(STATUS_BLOCK_GRAB_OBJECT, 1);
        if (llGetStatus(STATUS_BLOCK_GRAB) || !llGetStatus(STATUS_BLOCK_GRAB_OBJECT))
            return;
)LSL", "Linkset blocks grab");
    expect_blocked_grab(grab_root->id, 192, 193);
    expect_blocked_grab(grab_child->id, 192, 194);
    set_grab_status(R"LSL(        llSetStatus(STATUS_BLOCK_GRAB_OBJECT, 0);
        if (llGetStatus(STATUS_BLOCK_GRAB_OBJECT))
            return;
)LSL", "Linkset allows grab");
    drag(grab_child->id, 195);
    expect_grab_position(195);
    const auto debug_host = runtime.world().add(SceneObject::primitive(circuit->agent_id, {170, 168, 30}));
    check(debug_host.has_value(), "add estate scripting toggle fixture");
    const auto debug_source = store_script("Estate scripting toggle", R"LSL(
default
{
    state_entry() { llSetObjectDesc("Estate script started"); }
    changed(integer flags)
    {
        if (flags == CHANGED_REGION_START && llGetStartParameter() == 0)
            llSetObjectDesc("Estate script restarted");
    }
}
)LSL");
    auto debug_task = rez_task("Estate scripting toggle", debug_source, 10, 0);
    debug_task->reset_ids(debug_host->id);
    runtime.restore_task_inventory(debug_host->id, {debug_task});
    pump([&] { return runtime.world().object(debug_host->id)->description == "Estate script started"; });

    const auto set_estate_scripting = [&](bool enabled) {
        auto request = make("EstateOwnerMessage");
        request.block("MethodData")["Method"] = wire("setregiondebug");
        for (const auto* value : {enabled ? "false" : "true", "false", "false"}) {
            auto parameter = network::lludp::make_block(request.spec->blocks.at(2));
            parameter["Parameter"] = wire(value);
            request.blocks["ParamList"].push_back(std::move(parameter));
        }
        send(request);
    };
    estate_access = true;
    set_estate_scripting(false);
    pump([&] { return runtime.snapshot_task_script_states(debug_host->id).empty(); });
    const auto debug_inventory = runtime.snapshot_task_inventory(debug_host->id);
    check(debug_inventory && debug_inventory->size() == 1,
          "disabling estate scripting removes the instance and keeps the inventory item");
    set_estate_scripting(true);
    pump([&] { return runtime.world().object(debug_host->id)->description == "Estate script restarted"; });
    estate_access = false;
    runtime.world().remove(debug_host->id);

    auto final_edit = make("ObjectDescription");
    auto final_edit_part = network::lludp::make_block(final_edit.spec->blocks.at(1));
    final_edit_part["LocalID"] = std::uint64_t(worn_again->local_id);
    final_edit_part["Description"] = wire("Edit with a failed checkpoint");
    final_edit.blocks["ObjectData"].push_back(std::move(final_edit_part));
    send(final_edit);
    pump([&] { return runtime.world().object(worn_again->id)->description == "Edit with a failed checkpoint"; });
    const auto previous_checkpoint = attachment_scripts.load(circuit->agent_id, wear_item.id, detached_item->asset_id);
    attachment_table.execute("CREATE TRIGGER fail_attachment_checkpoint BEFORE INSERT ON native_attachment_scripts "
                             "BEGIN SELECT RAISE(ABORT, 'injected checkpoint write failure'); END", {});
    check(runtime.close_agent_session(circuit->agent_id, circuit->session_id.str()), "close session with failed save");
    pump([&] { return !runtime.world().object(worn_again->id); });
    check(inventory.item(circuit->agent_id, wear_item.id)->asset_id == detached_item->asset_id &&
              attachment_scripts.load(circuit->agent_id, wear_item.id, detached_item->asset_id) == previous_checkpoint,
          "checkpoint write failure preserves the published asset and its checkpoint");
    check(runtime.world().object(target.id).has_value(), "logout preserves unattached prims");
    client.stop();
    runtime.stop();
}
}
int main() {
    try {
        check_attachment_asset_round_trip();
        run(true);
        run(false);
        run(true, false);
        std::cout << "PASS: attachment asset round trip, UDP admission, scene-owner asset persistence, "
                     "native VM retention and reset, and shutdown cancellations\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
