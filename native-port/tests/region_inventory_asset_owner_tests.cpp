#include "opensim/framework/agent_data.hpp"
#include "opensim/framework/texture_entry.hpp"
// Actual region admission/UDP/scene-owner asset update/VM retention and shutdown.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/services/asset_store.hpp"
#include "opensim/services/avatar_service.hpp"
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
void run() {
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
    const std::string source =
        R"LSL(
integer saw_camera;

default
{
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
            llSay(0, "Script running");
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
            && llGetObjectPrimCount(llGetKey()) == 2
            && llGetObjectPrimCount(NULL_KEY) == 0
            && llGetLocalPos() == <1, 0, 0>
            && llGetPos() == llGetRootPosition() + <-1, 0, 0>
            && llGetLocalRot() == <1, 0, 0, 0>
            && llGetRootRotation() == <0, 0, 1, 0>
            && llGetRot() == <0, 1, 0, 0>)
            llSay(0, "Linked child position");

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
    std::atomic<bool> god_mode = false;
    RegionPolicies policies;
    policies.log = [](const auto &text) { std::cerr << text << '\n'; };
    policies.access = [](auto, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.can_edit = [&](auto agent, const auto &object) { return allow_edit && agent == object.owner; };
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
    const auto avatar_database = path(directory / "avatars.db");
    SqliteAvatarStore avatar_store(avatar_database, true);
    AvatarService avatars(avatar_store);
    std::atomic<int> appearance_saves = 0;
    policies.save_appearance = [&](Uuid agent, const framework::AvatarAppearance &appearance) {
        check(agent == circuit->agent_id && appearance.attachments().empty(),
              "saved appearance removes inventory attachment");
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
    int stage = 0;
    auto pump = [&](auto predicate) {
        ++stage;
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (!predicate() && std::chrono::steady_clock::now() < until) {
            runtime.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!predicate())
            throw std::runtime_error("region owner/UDP deadline stage " + std::to_string(stage));
    };
    auto transferred = std::make_shared<framework::AgentData>();
    transferred->agent_id = circuit->agent_id;
    transferred->session_id = circuit->session_id;
    transferred->circuit_code = circuit->circuit_code;
    transferred->position = circuit->start_position;
    transferred->active_group_id = Uuid::random();
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
    auto scripted_prim =
        SceneObject::primitive(circuit->agent_id, runtime.viewers().agent(circuit->agent_id)->position);
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
        bool dimensions = false, absent = false, same_group = false, wearer_group = false;
        for (const auto &packet : received)
            if (packet.spec->name == "ChatFromSimulator") {
                const auto message = field(packet, "ChatData", "Message");
                dimensions |= message == "<0.500000, 0.750000, 1.875000>";
                absent |= message == "Absent avatar size";
                same_group |= message == "Same object group";
                wearer_group |= message == "Attachment uses wearer group";
            }
        return dimensions && absent && same_group && wearer_group;
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
    runtime.viewers().disconnect(circuit->agent_id);
    pump([&] { return !runtime.world().object(attachment.id); });
    check(runtime.world().object(target.id).has_value(), "logout preserves unattached prims");
    client.stop();
    runtime.stop();
}
}
int main() {
    try {
        run();
        std::cout
            << "PASS: actual UDP admission/root/alerts, scene-owner asset persistence, native VM retention/reset and16 shutdown cancellations\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
