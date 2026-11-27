// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/services/inventory_store.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
#include <tuple>

using namespace opensim;
using namespace framework;
using namespace scene;

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Could not read original attachment sidecar fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> object_asset(const ObjectAssetPart &part) {
    std::u16string text;
    XmlTextWriterIo io;
    io.encoding = [] { return std::shared_ptr<NativeXmlTextEncoding>{}; };
    io.write = [&](auto, auto value) {
        if (value)
            text.append(*value);
    };
    io.write_array = [&](auto value, int offset, int count) { text.append(value->data() + offset, count); };
    io.write_whole_array = [&](auto value) { text.append(value->data(), value->size()); };
    NativeXmlTextWriter writer(std::move(io));
    write_object_asset_xml(writer, part, {});
    const auto encoded = utf8(text);
    return {encoded.begin(), encoded.end()};
}

void exercise(const std::filesystem::path &fixtures, bool login_wear) {
    const auto source = read(fixtures / "pending/source.lsl");
    const auto original = read(fixtures / "pending/snapshot.bin");
    const auto metadata = network::parse_osd_json(read(fixtures / "pending/metadata.json"));
    services::InventoryFolderStore folders(":memory:", true);
    services::InventoryService inventory(folders);
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    auto circuit = std::make_shared<AgentCircuitData>();
    circuit->agent_id = Uuid::random();
    circuit->session_id = Uuid::random();
    circuit->secure_session_id = Uuid::random();
    circuit->circuit_code = 42;
    circuit->caps_path = Uuid::random().str();
    circuit->first_name = "Sidecar";
    circuit->last_name = "Owner";
    circuit->start_position = {100, 100, 30};
    circuit->appearance = std::make_shared<AvatarAppearance>();
    check(inventory.create_user_inventory(circuit->agent_id), "Could not create attachment inventory through service");
    std::map<Uuid, std::shared_ptr<AssetBase>> assets;
    auto source_asset =
        std::make_shared<AssetBase>(Uuid::random(), "Original sidecar script", 10, circuit->agent_id.str());
    source_asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    assets.emplace(source_asset->full_id(), source_asset);
    ObjectAssetPart part;
    part.object = SceneObject::primitive(circuit->agent_id, {});
    part.object.last_attachment_point = part.last_attachment_point = 5;
    part.object.saved_attachment_position = part.attachment_position = {.25f, 0, 0};
    TaskInventoryItem script_item;
    script_item.item_id = Uuid::random();
    script_item.parent_id = script_item.parent_part_id = part.object.id;
    script_item.asset_id = source_asset->full_id();
    script_item.owner_id = script_item.creator_id = circuit->agent_id;
    script_item.type = script_item.inventory_type = 10;
    script_item.name = "Deferred original script";
    part.inventory.push_back(script_item);
    const auto compiled = script::compile_lsl(source);
    check(bool(compiled), "Original sidecar script failed native compilation");
    std::int64_t clock = 10000000;
    script::LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    script::LslInstance stale(compiled.program, {}, host);
    check(stale.run(10000).status == script::LslRunStatus::Idle, "Could not prepare stale embedded native checkpoint");
    auto checkpoint = stale.checkpoint();
    check(checkpoint && checkpoint->globals.size() == 2, "Stale attachment checkpoint globals unavailable");
    checkpoint->globals[0] = std::int32_t{777};
    checkpoint->globals[1] = std::int32_t{777};
    part.native_script_checkpoints.emplace(script_item.item_id, *checkpoint);
    auto wear_asset =
        std::make_shared<AssetBase>(Uuid::random(), "Attachment with stale checkpoint", 6, circuit->agent_id.str());
    wear_asset->data = std::make_shared<std::vector<std::uint8_t>>(object_asset(part));
    assets.emplace(wear_asset->full_id(), wear_asset);
    services::InventoryItem wear_item;
    wear_item.id = Uuid::random();
    wear_item.owner = circuit->agent_id;
    wear_item.parent = inventory.root(circuit->agent_id)->id;
    wear_item.asset_id = wear_asset->full_id();
    wear_item.asset_type = wear_item.inventory_type = 6;
    wear_item.name = "Opaque original sidecar attachment";
    wear_item.creator_id = circuit->agent_id.str();
    wear_item.base_permissions = wear_item.current_permissions = wear_item.next_permissions = 0x7fffffff;
    check(inventory.add_item(wear_item), "Could not add wearable object through inventory service");
    const auto payload = std::make_shared<std::vector<std::uint8_t>>(original.begin(), original.end());
    const auto snapshot = network::binary_base64(network::osd_from_binary(payload));
    const auto xml = "<ScriptState Engine=\"YEngine\" Asset=\"" + source_asset->full_id().str() + "\" SourceHash=\"" +
                     metadata.get("source_hash").text + "\"><Running>True</Running><Snapshot>" + snapshot +
                     "</Snapshot></ScriptState>";
    using Key = std::tuple<Uuid, Uuid, Uuid>;
    std::map<Key, AttachmentScriptStates> sidecars;
    sidecars.emplace(Key{circuit->agent_id, wear_item.id, wear_asset->full_id()},
                     AttachmentScriptStates{{script_item.item_id, {xml.begin(), xml.end()}}});
    unsigned loads = 0, saves = 0;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.inventory_service = &inventory;
    policies.load_asset = [&](Uuid id) {
        const auto found = assets.find(id);
        return found == assets.end() ? nullptr : found->second;
    };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    policies.store_inventory_asset = [&](auto asset) -> std::optional<std::string> {
        const auto id = asset->full_id();
        assets[id] = std::move(asset);
        return id.str();
    };
    policies.save_appearance = [](Uuid, const auto &) { return true; };
    policies.load_attachment_scripts = [&](Uuid owner, Uuid item, Uuid asset) {
        ++loads;
        const auto found = sidecars.find({owner, item, asset});
        return found == sidecars.end() ? AttachmentScriptStates{} : found->second;
    };
    policies.save_attachment_scripts = [&](Uuid owner, Uuid item, Uuid asset, const AttachmentScriptStates &states) {
        ++saves;
        const Key key{owner, item, asset};
        if (states.empty())
            sidecars.erase(key);
        else
            sidecars.insert_or_assign(key, states);
    };
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = login_wear ? "Login sidecar restoration" : "Viewer sidecar restoration";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9025, 0, std::string("127.0.0.1")}));
    const auto wait = [&](auto predicate, const char *message) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        do {
            runtime.poll();
            if (predicate())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error(message);
    };
    auto peer = std::make_shared<network::UdpSocket *>(nullptr);
    network::UdpSocket socket(network::UdpSocketOptions{}, [peer](const auto &sender, auto bytes) {
        const auto packet = network::lludp::decode_datagram(bytes, 65536, false);
        if (packet.reliable) {
            auto ack = network::lludp::make_packet("PacketAck");
            ack.reliable = false;
            ack.blocks["Packets"].push_back({{"ID", std::uint64_t(packet.sequence)}});
            (*peer)->send_to(sender, network::lludp::encode_datagram(ack));
        }
    });
    *peer = &socket;
    const auto endpoint = socket.start();
    std::uint32_t sequence = 0;
    const auto send = [&](network::lludp::Packet packet) {
        packet.sequence = ++sequence;
        if (packet.blocks.contains("AgentData")) {
            packet.block("AgentData")["AgentID"] = circuit->agent_id;
            packet.block("AgentData")["SessionID"] = circuit->session_id;
        }
        runtime.viewers().receive(endpoint, network::lludp::encode_datagram(packet));
    };
    if (login_wear)
        check(circuit->appearance->set_attachment(5, wear_item.id, wear_asset->full_id()),
              "Could not seed login appearance");
    presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id);
    services::LoginTransferContext transfer;
    std::string reason;
    check(runtime.create_agent(nullptr, region, circuit, 128, transfer, reason), "Could not reserve sidecar owner");
    auto use = network::lludp::make_packet("UseCircuitCode");
    use.block("CircuitCode")["ID"] = circuit->agent_id;
    use.block("CircuitCode")["SessionID"] = circuit->session_id;
    use.block("CircuitCode")["Code"] = std::uint64_t{42};
    send(std::move(use));
    wait([&] { return runtime.viewers().agent(circuit->agent_id).has_value(); }, "Sidecar owner did not authenticate");
    auto movement = network::lludp::make_packet("CompleteAgentMovement");
    movement.block("AgentData")["CircuitCode"] = std::uint64_t{42};
    send(std::move(movement));
    wait([&] { return runtime.viewers().agent(circuit->agent_id)->root; }, "Sidecar owner did not complete arrival");
    const auto wear = [&] {
        auto packet = network::lludp::make_packet("RezSingleAttachmentFromInv");
        packet.block("ObjectData")["ItemID"] = wear_item.id;
        packet.block("ObjectData")["AttachmentPt"] = std::uint64_t{5};
        send(std::move(packet));
    };
    if (!login_wear)
        wear();
    std::string retained;
    Uuid worn_id;
    wait(
        [&] {
            const auto wearer = runtime.viewers().agent(circuit->agent_id);
            for (const auto &object : runtime.world().attachments(wearer->local_id)) {
                if (object.from_inventory_item != wear_item.id)
                    continue;
                const auto states = runtime.snapshot_task_script_states(object.id);
                if (states.size() != 1 || script::decode_lsl_checkpoint(states.begin()->second))
                    continue;
                retained.assign(states.begin()->second.begin(), states.begin()->second.end());
                worn_id = object.id;
                return retained.find(snapshot) != retained.npos && runtime.active_script_count() == 0;
            }
            return false;
        },
        "Original sidecar did not override stale embedded native state on attachment admission");
    check(loads >= 1 && retained.find(metadata.get("source_hash").text) != retained.npos,
          "Attachment sidecar lost original managed hash/state");
    auto detach = network::lludp::make_packet("ObjectDetach");
    detach.blocks["ObjectData"].push_back(
        {{"ObjectLocalID", std::uint64_t(runtime.world().object(worn_id)->local_id)}});
    send(std::move(detach));
    wait([&] { return !runtime.world().object(worn_id) && saves > 0; },
         "Attachment detach did not persist its opaque sidecar");
    const auto current = inventory.item(circuit->agent_id, wear_item.id);
    check(bool(current), "Detach lost its inventory object");
    const auto saved = sidecars.find({circuit->agent_id, wear_item.id, current->asset_id});
    check(saved != sidecars.end() && saved->second.size() == 1 &&
              std::string(saved->second.begin()->second.begin(), saved->second.begin()->second.end()) == retained,
          "Detach changed or dropped the retained original YEngine XML");
    const auto previous_loads = loads;
    wear();
    wait(
        [&] {
            if (loads <= previous_loads)
                return false;
            const auto wearer = runtime.viewers().agent(circuit->agent_id);
            for (const auto &object : runtime.world().attachments(wearer->local_id)) {
                if (object.from_inventory_item != wear_item.id)
                    continue;
                const auto states = runtime.snapshot_task_script_states(object.id);
                return states.size() == 1 && !script::decode_lsl_checkpoint(states.begin()->second) &&
                       std::string(states.begin()->second.begin(), states.begin()->second.end()) == retained &&
                       runtime.active_script_count() == 0;
            }
            return false;
        },
        "Rewear did not preserve the detached opaque continuation");
    socket.stop();
    runtime.stop();
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original managed fixture directory required");
        exercise(argv[1], false);
        exercise(argv[1], true);
        std::cout << "PASS: viewer/login attachment sidecars override stale checkpoints and survive detach/rewear\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
