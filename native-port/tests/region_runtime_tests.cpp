#include "opensim/scene/region_runtime.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
using namespace opensim;
using namespace framework;
using namespace services;
using namespace scene;
struct FailingPresence : IPresenceService {
    NullPresenceStore store;
    PresenceService service{store};
    int region_logouts = 0;
    bool login(const std::string& u, const Uuid& s, const Uuid& secure) override {
        return service.login(u, s, secure);
    }
    bool logout(const Uuid& s) override {
        return service.logout(s);
    }
    bool logout_region(const Uuid& r) override {
        ++region_logouts;
        service.logout_region(r);
        throw std::runtime_error("controlled presence cleanup failure");
    }
    bool report(const Uuid& s, const Uuid& r) override {
        return service.report(s, r);
    }
    std::optional<PresenceInfo> agent(const Uuid& s) override {
        return service.agent(s);
    }
    std::vector<PresenceInfo> agents(const std::vector<std::string>& u) override {
        return service.agents(u);
    }
};
namespace {
void verify_viewer_packets(RegionRuntime& runtime, const server::RegistryCapabilityListener& listener,
                           const network::UdpEndpoint& endpoint,
                           const AgentCircuitData& circuit, const AssetBase& covenant, Uuid library_item,
                           const std::function<std::shared_ptr<AssetBase>(Uuid)>& uploaded_asset,
                           InventoryService& inventory, const SceneObject& hub, float expected_arrival_x,
                           const std::atomic<unsigned>& parcel_refreshes, Uuid unsit_asset, Uuid force_sit_asset,
                           Uuid parcel_owner, Uuid parcel_group, Uuid administrator) {
    namespace udp = network::lludp;
    std::mutex mutex;
    std::vector<udp::Packet> received;
    std::atomic<std::uint32_t> sequence = 0;
    network::UdpSocket viewer({"127.0.0.1", 0}, [&](const auto& from, auto bytes) {
        auto packet = udp::decode_datagram(bytes);
        if (packet.reliable) {
            auto ack = udp::make_packet("PacketAck");
            ack.sequence = ++sequence;
            auto block = udp::make_block(ack.spec->blocks.front());
            block["ID"] = std::uint64_t(packet.sequence);
            ack.blocks["Packets"].push_back(std::move(block));
            viewer.send_to(from, udp::encode_datagram(ack));
        }
        std::lock_guard lock(mutex);
        received.push_back(std::move(packet));
    });
    viewer.start();
    const auto transmit = [&](udp::Packet packet) {
        packet.sequence = ++sequence;
        if (!viewer.send_to(endpoint, udp::encode_datagram(packet)))
            throw std::runtime_error("Viewer could not send asset transfer fixture packet");
    };
    const auto wait = [&](auto predicate, const char* error) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            runtime.poll();
            {
                std::lock_guard lock(mutex);
                if (predicate(received))
                    return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error(error);
    };
    auto use = udp::make_packet("UseCircuitCode");
    auto& credentials = use.block("CircuitCode");
    credentials["Code"] = std::uint64_t{circuit.circuit_code};
    credentials["ID"] = circuit.agent_id;
    credentials["SessionID"] = circuit.session_id;
    transmit(std::move(use));
    wait(
        [](const auto& packets) {
            return std::any_of(packets.begin(), packets.end(),
                               [](const auto& packet) { return packet.spec->name == "RegionHandshake"; });
        },
        "Viewer did not connect for covenant transfer");
    auto handshake = udp::make_packet("RegionHandshakeReply");
    handshake.block("AgentData")["AgentID"] = circuit.agent_id;
    handshake.block("AgentData")["SessionID"] = circuit.session_id;
    transmit(std::move(handshake));

    for (const auto crc : {0u, 123u}) {
        auto request = udp::make_packet("MuteListRequest");
        request.block("AgentData")["AgentID"] = circuit.agent_id;
        request.block("AgentData")["SessionID"] = circuit.session_id;
        request.block("MuteData")["MuteCRC"] = std::uint64_t{crc};
        transmit(std::move(request));
        wait(
            [&](const auto& packets) {
                for (const auto& reply : packets) {
                    if (crc != 0 && reply.spec->name == "UseCachedMuteList" &&
                        std::get<Uuid>(reply.block("AgentData").at("AgentID")) == circuit.agent_id)
                        return true;
                    if (crc == 0 && reply.spec->name == "GenericMessage") {
                        const auto& bytes =
                            std::get<std::vector<std::uint8_t>>(reply.block("MethodData").at("Method"));
                        if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                                "emptymutelist" &&
                            reply.blocks.at("ParamList").size() == 1)
                            return true;
                    }
                }
                return false;
            },
            "Viewer did not receive the disabled mute-list response");
    }

    runtime.set_estate_covenant(covenant.full_id(), 123);
    auto covenant_request = udp::make_packet("EstateCovenantRequest");
    auto& identity = covenant_request.block("AgentData");
    identity["AgentID"] = circuit.agent_id;
    identity["SessionID"] = circuit.session_id;
    transmit(std::move(covenant_request));
    wait(
        [&](const auto& packets) {
            for (const auto& packet : packets) {
                if (packet.spec->name != "EstateCovenantReply")
                    continue;
                const auto& data = packet.block("Data");
                if (std::get<Uuid>(data.at("CovenantID")) == covenant.full_id() &&
                    std::get<std::uint64_t>(data.at("CovenantTimestamp")) == 123)
                    return true;
            }
            return false;
        },
        "Viewer did not receive the configured covenant");

    const auto verify = [&](std::int64_t source, const std::vector<std::uint8_t>& params,
                            const std::vector<std::uint8_t>& expected_params) {
        const auto id = Uuid::random();
        auto transfer = udp::make_packet("TransferRequest");
        auto& request = transfer.block("TransferInfo");
        request["TransferID"] = id;
        request["ChannelType"] = std::int64_t{2};
        request["SourceType"] = source;
        request["Params"] = params;
        transmit(std::move(transfer));
        wait(
            [&](const auto& packets) {
                bool header = false;
                std::map<int, std::vector<std::uint8_t>> chunks;
                for (const auto& packet : packets) {
                    if (packet.spec->name == "TransferInfo") {
                        const auto& info = packet.block("TransferInfo");
                        if (std::get<Uuid>(info.at("TransferID")) != id)
                            continue;
                        if (std::get<std::int64_t>(info.at("Status")) != 0 ||
                            std::get<std::int64_t>(info.at("Size")) !=
                                static_cast<std::int64_t>(covenant.data->size()) ||
                            std::get<std::vector<std::uint8_t>>(info.at("Params")) != expected_params)
                            throw std::runtime_error(
                                "Covenant or library transfer header differs from legacy");
                        header = true;
                    } else if (packet.spec->name == "TransferPacket") {
                        const auto& data = packet.block("TransferData");
                        if (std::get<Uuid>(data.at("TransferID")) != id)
                            continue;
                        const auto index = std::get<std::int64_t>(data.at("Packet"));
                        if (index < 0 || index > 1 || std::get<std::int64_t>(data.at("Status")) != index)
                            throw std::runtime_error("Covenant or library transfer chunk has invalid status");
                        chunks[static_cast<int>(index)] =
                            std::get<std::vector<std::uint8_t>>(data.at("Data"));
                    }
                }
                if (!header || chunks.size() != 2)
                    return false;
                auto assembled = chunks.at(0);
                assembled.insert(assembled.end(), chunks.at(1).begin(), chunks.at(1).end());
                if (chunks.at(0).size() != 600 || assembled != *covenant.data)
                    throw std::runtime_error("Covenant or library transfer changed asset bytes");
                return true;
            },
            "Covenant or library transfer did not complete");
    };
    std::vector<std::uint8_t> estate_params(20);
    estate_params[16] = static_cast<std::uint8_t>(AssetType::Notecard);
    verify(4, {}, estate_params);
    std::vector<std::uint8_t> library_params(64);
    for (const auto& id : {library_item, covenant.full_id()})
        library_params.insert(library_params.end(), id.bytes().begin(), id.bytes().end());
    library_params.insert(library_params.end(), {static_cast<std::uint8_t>(AssetType::Notecard), 0, 0, 0});
    verify(3, library_params, library_params);

    const auto upload = [&](Uuid transaction, std::vector<std::uint8_t> bytes, bool local = true) {
        auto request = udp::make_packet("AssetUploadRequest");
        auto& fields = request.block("AssetBlock");
        fields["TransactionID"] = transaction;
        fields["Type"] = std::int64_t{static_cast<std::int8_t>(AssetType::Notecard)};
        fields["StoreLocal"] = local;
        fields["Tempfile"] = true;
        fields["AssetData"] = std::move(bytes);
        transmit(std::move(request));
    };
    const auto check_uploaded = [&](Uuid id, const std::vector<std::uint8_t>& expected) {
        wait(
            [&](const auto& packets) {
                return std::any_of(packets.begin(), packets.end(), [&](const auto& packet) {
                    if (packet.spec->name != "AssetUploadComplete")
                        return false;
                    const auto& result = packet.block("AssetBlock");
                    return std::get<Uuid>(result.at("UUID")) == id && std::get<bool>(result.at("Success"));
                });
            },
            "Viewer did not receive successful asset upload completion");
        const auto asset = uploaded_asset(id);
        if (!asset || !asset->data || *asset->data != expected || !asset->local() || !asset->temporary() ||
            asset->creator_id() != circuit.agent_id.str())
            throw std::runtime_error("Uploaded asset storage differs from the viewer request");
    };
    upload(Uuid("28731084-aa42-4fd8-8014-2027257d21c1"), {7, 0, 9});
    check_uploaded(Uuid("83f2cb8e-8d5a-1a33-22e2-f4b5ba4bb964"), {7, 0, 9});

    const Uuid multipart_id("1104ffd3-7e88-7876-be92-03f28163512b");
    upload(Uuid("28731084-aa42-4fd8-8014-2027257d21c2"), {});
    std::uint64_t xfer_id = 0;
    wait(
        [&](const auto& packets) {
            for (const auto& packet : packets) {
                if (packet.spec->name != "RequestXfer")
                    continue;
                const auto& fields = packet.block("XferID");
                if (std::get<Uuid>(fields.at("VFileID")) == multipart_id) {
                    xfer_id = std::get<std::uint64_t>(fields.at("ID"));
                    return xfer_id != 0;
                }
            }
            return false;
        },
        "Viewer did not receive the asset upload Xfer ID");
    const auto part = [&](std::uint64_t number, std::vector<std::uint8_t> data) {
        auto chunk = udp::make_packet("SendXferPacket");
        chunk.block("XferID")["ID"] = xfer_id;
        chunk.block("XferID")["Packet"] = number;
        chunk.block("DataPacket")["Data"] = std::move(data);
        transmit(std::move(chunk));
        wait(
            [&](const auto& packets) {
                return std::any_of(packets.begin(), packets.end(), [&](const auto& packet) {
                    if (packet.spec->name != "ConfirmXferPacket")
                        return false;
                    const auto& fields = packet.block("XferID");
                    return std::get<std::uint64_t>(fields.at("ID")) == xfer_id &&
                           std::get<std::uint64_t>(fields.at("Packet")) == number;
                });
            },
            "Viewer did not receive the asset Xfer packet confirmation");
    };
    part(0, {5, 0, 0, 0, 1, 2, 3});
    part(0x80000001, {4, 5});
    check_uploaded(multipart_id, {1, 2, 3, 4, 5});

    const auto folder = inventory.root(circuit.agent_id);
    if (!folder)
        throw std::runtime_error("Viewer inventory root is missing");
    const auto create_item = [&](Uuid transaction, std::uint64_t callback) {
        auto request = udp::make_packet("CreateInventoryItem");
        auto& identity = request.block("AgentData");
        identity["AgentID"] = circuit.agent_id;
        identity["SessionID"] = circuit.session_id;
        auto& fields = request.block("InventoryBlock");
        fields["TransactionID"] = transaction;
        fields["CallbackID"] = callback;
        fields["FolderID"] = folder->id;
        fields["Type"] = std::int64_t{7};
        fields["InvType"] = std::int64_t{7};
        fields["NextOwnerMask"] = std::uint64_t{0x8000};
        fields["Name"] = std::vector<std::uint8_t>{'U', 'p', 'l', 'o', 'a', 'd', 0};
        transmit(std::move(request));
    };
    const auto check_created = [&](Uuid transaction, std::uint64_t callback,
                                   const std::vector<std::uint8_t>& expected) {
        Uuid item_id, asset_id;
        wait(
            [&](const auto& packets) {
                for (const auto& packet : packets) {
                    if (packet.spec->name != "UpdateCreateInventoryItem" ||
                        std::get<Uuid>(packet.block("AgentData").at("TransactionID")) != transaction)
                        continue;
                    const auto& fields = packet.block("InventoryData");
                    if (std::get<std::uint64_t>(fields.at("CallbackID")) != callback)
                        throw std::runtime_error("Upload inventory callback ID changed");
                    item_id = std::get<Uuid>(fields.at("ItemID"));
                    asset_id = std::get<Uuid>(fields.at("AssetID"));
                    return true;
                }
                return false;
            },
            "Upload did not create a viewer inventory item");
        const auto item = inventory.item(circuit.agent_id, item_id);
        const auto asset = uploaded_asset(asset_id);
        if (!item || item->asset_id != asset_id || item->parent != folder->id || item->name != "Upload" ||
            item->next_permissions != 0x8000 || !asset || !asset->data || *asset->data != expected)
            throw std::runtime_error("Upload inventory item or asset differs from the request");
        return item_id;
    };
    const auto create_first = Uuid::random();
    create_item(create_first, 41);
    upload(create_first, {1, 7, 0, 8}, false);
    check_created(create_first, 41, {1, 7, 0, 8});
    const auto upload_first = Uuid::random();
    upload(upload_first, {2, 7, 0, 8}, false);
    create_item(upload_first, 42);
    const auto updated_item = check_created(upload_first, 42, {2, 7, 0, 8});
    const auto update_item = [&](Uuid transaction) {
        auto request = udp::make_packet("UpdateInventoryItem");
        auto& identity = request.block("AgentData");
        identity["AgentID"] = circuit.agent_id;
        identity["SessionID"] = circuit.session_id;
        auto fields = udp::make_block(request.spec->blocks.at(1));
        fields["ItemID"] = updated_item;
        fields["TransactionID"] = transaction;
        fields["Name"] = std::vector<std::uint8_t>{'E', 'd', 'i', 't', 0};
        request.blocks["InventoryData"].push_back(std::move(fields));
        transmit(std::move(request));
    };
    const auto check_updated = [&](Uuid transaction, const std::vector<std::uint8_t>& expected) {
        Uuid asset_id;
        wait(
            [&](const auto& packets) {
                for (const auto& packet : packets) {
                    if (packet.spec->name != "UpdateCreateInventoryItem" ||
                        std::get<Uuid>(packet.block("AgentData").at("TransactionID")) != transaction)
                        continue;
                    const auto& fields = packet.block("InventoryData");
                    if (std::get<Uuid>(fields.at("ItemID")) != updated_item ||
                        std::get<std::uint64_t>(fields.at("CallbackID")) != 0)
                        throw std::runtime_error(
                            "Inventory upload update replaced the item identity or callback");
                    asset_id = std::get<Uuid>(fields.at("AssetID"));
                    return true;
                }
                return false;
            },
            "Upload did not update the existing inventory item");
        const auto item = inventory.item(circuit.agent_id, updated_item);
        const auto asset = uploaded_asset(asset_id);
        if (!item || item->asset_id != asset_id || item->name != "Edit" || !asset || !asset->data ||
            *asset->data != expected)
            throw std::runtime_error("Updated inventory row or asset differs from the request");
    };
    const auto update_first = Uuid::random();
    update_item(update_first);
    upload(update_first, {3, 7, 0, 8}, false);
    check_updated(update_first, {3, 7, 0, 8});
    const auto update_later = Uuid::random();
    upload(update_later, {4, 7, 0, 8}, false);
    update_item(update_later);
    check_updated(update_later, {4, 7, 0, 8});

    const auto prim = runtime.world().add(SceneObject::primitive(circuit.agent_id, {128, 128, 25}));
    if (!prim)
        throw std::runtime_error("Could not create upload test prim");
    auto task_item = std::make_shared<TaskInventoryItem>();
    task_item->item_id = Uuid::random();
    task_item->asset_id = covenant.full_id();
    task_item->owner_id = circuit.agent_id;
    task_item->creator_id = circuit.agent_id;
    task_item->type = task_item->inventory_type = 7;
    task_item->name = "Task upload";
    runtime.restore_task_inventory(prim->id, {task_item});
    const auto task_change = [&](Uuid transaction) {
        auto change = udp::make_packet("UpdateTaskInventory");
        auto& identity = change.block("AgentData");
        identity["AgentID"] = circuit.agent_id;
        identity["SessionID"] = circuit.session_id;
        change.block("UpdateData")["LocalID"] = std::uint64_t{prim->local_id};
        auto& fields = change.block("InventoryData");
        fields["ItemID"] = task_item->item_id;
        fields["TransactionID"] = transaction;
        fields["OwnerID"] = fields["CreatorID"] = circuit.agent_id;
        fields["Type"] = fields["InvType"] = std::int64_t{7};
        fields["OwnerMask"] = fields["NextOwnerMask"] = std::uint64_t{0x7fffffff};
        fields["Name"] = std::vector<std::uint8_t>{'T', 'a', 's', 'k', 0};
        transmit(std::move(change));
    };
    const auto check_task = [&](const std::vector<std::uint8_t>& expected) {
        wait(
            [&](const auto&) {
                const auto contents = runtime.snapshot_task_inventory(prim->id);
                if (!contents || contents->size() != 1 || contents->front().item_id != task_item->item_id)
                    return false;
                const auto asset = uploaded_asset(contents->front().asset_id);
                return contents->front().name == "Task" && asset && asset->data && *asset->data == expected;
            },
            "UDP task inventory transaction did not install the uploaded asset");
    };
    const auto task_upload_first = Uuid::random();
    upload(task_upload_first, {8, 0, 255, 1}, false);
    task_change(task_upload_first);
    check_task({8, 0, 255, 1});
    const auto task_update_first = Uuid::random();
    task_change(task_update_first);
    upload(task_update_first, {8, 0, 255, 2}, false);
    check_task({8, 0, 255, 2});

    auto restricted = udp::make_packet("AssetUploadRequest");
    auto& restricted_fields = restricted.block("AssetBlock");
    restricted_fields["TransactionID"] = Uuid::random();
    restricted_fields["Type"] = std::int64_t{static_cast<std::int8_t>(AssetType::Texture)};
    restricted_fields["StoreLocal"] = true;
    restricted_fields["AssetData"] = std::vector<std::uint8_t>{1, 2, 3};
    transmit(std::move(restricted));
    wait(
        [](const auto& packets) {
            for (const auto& packet : packets) {
                if (packet.spec->name != "AgentAlertMessage")
                    continue;
                const auto& message =
                    std::get<std::vector<std::uint8_t>>(packet.block("AlertData").at("Message"));
                if (std::string(message.begin(), std::find(message.begin(), message.end(), 0)) ==
                    "/Unable to upload asset. Insufficient permissions.")
                    return true;
            }
            return false;
        },
        "Permanent texture upload did not enforce the configured user level");

    if (!runtime.world().add(hub))
        throw std::runtime_error("Could not add the arrival fixture telehub");
    auto movement = udp::make_packet("CompleteAgentMovement");
    auto& movement_data = movement.block("AgentData");
    movement_data["AgentID"] = circuit.agent_id;
    movement_data["SessionID"] = circuit.session_id;
    movement_data["CircuitCode"] = std::uint64_t{circuit.circuit_code};
    transmit(std::move(movement));
    wait([&](const auto& packets) {
        return std::any_of(packets.begin(), packets.end(), [&](const auto& packet) {
            if (packet.spec->name != "AgentMovementComplete")
                return false;
            const auto& position = std::get<std::vector<double>>(packet.block("Data").at("Position"));
            return position.at(0) == expected_arrival_x && position.at(1) == 64;
        });
    }, "Viewer did not enter the region at the expected telehub spawn point");
    if (!runtime.world().remove(hub.id))
        throw std::runtime_error("Could not remove the arrival fixture telehub");

    std::vector<std::uint32_t> home_alert_sequences;
    const auto set_home = [&](double x, std::string_view expected) {
        auto request = udp::make_packet("SetStartLocationRequest");
        auto& agent = request.block("AgentData");
        agent["AgentID"] = circuit.agent_id;
        agent["SessionID"] = circuit.session_id;
        auto& location = request.block("StartLocationData");
        location["LocationID"] = std::uint64_t{1};
        location["LocationPos"] = std::vector<double>{x, 64, 60};
        location["LocationLookAt"] = std::vector<double>{0, 1, 0};
        transmit(std::move(request));
        wait([&](const auto& packets) {
            for (const auto& reply : packets) {
                if (reply.spec->name != "AgentAlertMessage" ||
                    std::find(home_alert_sequences.begin(), home_alert_sequences.end(), reply.sequence) !=
                        home_alert_sequences.end())
                    continue;
                const auto& alert = reply.block("AlertData");
                const auto& bytes = std::get<std::vector<std::uint8_t>>(alert.at("Message"));
                if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) == expected) {
                    home_alert_sequences.push_back(reply.sequence);
                    return !std::get<bool>(alert.at("Modal"));
                }
            }
            return false;
        }, "Viewer did not receive the expected Set Home alert");
    };
    constexpr auto denied = "/You are not allowed to set your home location in this parcel.";
    set_home(96, denied); // A saved telehub UUID alone does not grant permission.
    if (!runtime.world().add(hub))
        throw std::runtime_error("Could not add the Set Home fixture telehub");
    set_home(32, "Home position set."); // Group role grants permission in this parcel.
    set_home(96, "Home position set."); // The actual telehub grants permission in its parcel.
    set_home(160, denied); // Neither a neighboring telehub nor another group's role grants permission.

    const auto placed_hub = runtime.world().object(hub.id);
    if (!placed_hub)
        throw std::runtime_error("Set Home fixture telehub disappeared");
    std::vector<std::uint32_t> telehub_replies;
    const auto manage_telehub = [&](std::string_view command, std::uint32_t parameter,
                                   Uuid expected_hub, std::size_t expected_points) {
        auto request = udp::make_packet("EstateOwnerMessage");
        auto& agent = request.block("AgentData");
        agent["AgentID"] = circuit.agent_id;
        agent["SessionID"] = circuit.session_id;
        request.block("MethodData")["Method"] = std::vector<std::uint8_t>{'t','e','l','e','h','u','b',0};
        for (const auto& argument : {std::string(command), std::to_string(parameter)}) {
            auto block = udp::make_block(request.spec->blocks.at(2));
            std::vector<std::uint8_t> bytes(argument.begin(), argument.end());
            bytes.push_back(0);
            block["Parameter"] = std::move(bytes);
            request.blocks["ParamList"].push_back(std::move(block));
        }
        transmit(std::move(request));
        wait([&](const auto& packets) {
            for (const auto& reply : packets) {
                if (reply.spec->name != "TelehubInfo" ||
                    std::find(telehub_replies.begin(), telehub_replies.end(), reply.sequence) != telehub_replies.end())
                    continue;
                telehub_replies.push_back(reply.sequence);
                const auto& points = reply.blocks.at("SpawnPointBlock");
                if (std::get<Uuid>(reply.block("TelehubBlock").at("ObjectID")) != expected_hub ||
                    points.size() != expected_points)
                    throw std::runtime_error("Wrong telehub object or spawn point count");
                if (command == "spawnpoint add") {
                    const auto& offset = std::get<std::vector<double>>(points.front().at("SpawnPointPos"));
                    if (std::abs(offset[0] - 32) > .001 || std::abs(offset[1] - 64) > .001 ||
                        std::abs(offset[2]) > .001)
                        throw std::runtime_error("Telehub did not return the relative spawn offset");
                }
                return true;
            }
            return false;
        }, "Viewer did not receive TelehubInfo");
    };
    manage_telehub("info ui", 0, hub.id, 3);
    manage_telehub("connect", placed_hub->local_id, hub.id, 0);
    manage_telehub("spawnpoint add", prim->local_id, hub.id, 1);
    manage_telehub("spawnpoint remove", 0, hub.id, 0);
    manage_telehub("delete", 0, {}, 0);
    set_home(96, denied);

    runtime.world().set_sit_target(prim->id, {.5f, 0, 1}, {0, 0, 0, 1});
    auto sit = udp::make_packet("AgentRequestSit");
    sit.block("AgentData")["AgentID"] = circuit.agent_id;
    sit.block("AgentData")["SessionID"] = circuit.session_id;
    sit.block("TargetObject")["TargetID"] = prim->id;
    transmit(std::move(sit));
    wait([&](const auto& packets) {
        return std::any_of(packets.begin(), packets.end(), [&](const auto& packet) {
            return packet.spec->name == "AvatarSitResponse" &&
                   std::get<Uuid>(packet.block("SitObject").at("ID")) == prim->id;
        });
    }, "Viewer did not receive its sit response");
    const auto avatar_parent = [&](const udp::Packet& packet, std::uint32_t parent) {
        if (packet.spec->name != "ObjectUpdate")
            return false;
        return std::ranges::any_of(packet.blocks.at("ObjectData"), [&](const auto& row) {
            return std::get<Uuid>(row.at("FullID")) == circuit.agent_id &&
                   std::get<std::uint64_t>(row.at("ParentID")) == parent;
        });
    };
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [&](const auto& packet) { return avatar_parent(packet, prim->local_id); });
    }, "Viewer did not receive its parented avatar update");
    const Uuid sit_animation("1a5fe8ac-a804-8a5d-7cbd-56bd83184568");
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [&](const auto& packet) {
            if (packet.spec->name != "AvatarAnimation" ||
                std::get<Uuid>(packet.block("Sender").at("ID")) != circuit.agent_id)
                return false;
            const auto animations = packet.blocks.find("AnimationList");
            return animations != packet.blocks.end() &&
                   std::ranges::any_of(animations->second, [&](const auto& animation) {
                       return std::get<Uuid>(animation.at("AnimID")) == sit_animation;
                   });
        });
    }, "Viewer did not receive the default object-sit animation");
    const auto seated = runtime.viewers().agent(circuit.agent_id);
    if (!seated || !seated->seat)
        throw std::runtime_error("Viewer did not enter the object seat");
    const auto seat_order = seated->seat->order;
    const float turn = std::sqrt(.5f);
    if (!runtime.world().patch_transform(prim->id, Vector3{132, 126, 26},
                                         Quaternion{0, 0, turn, turn}, std::nullopt))
        throw std::runtime_error("Could not move the occupied seat");
    wait([&](const auto&) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->seat && avatar->seat->order == seat_order &&
               std::abs(avatar->position.x - 132.f) < .001f &&
               std::abs(avatar->position.y - 126.5f) < .001f &&
               std::abs(avatar->position.z - 27.35f) < .001f;
    }, "Seated avatar did not follow the translated and rotated linkset");
    const auto new_root = runtime.world().add(SceneObject::primitive(circuit.agent_id, {136, 126, 26}));
    if (!new_root || !runtime.world().link_prim(prim->id, new_root->id))
        throw std::runtime_error("Could not link the occupied prim to a new root");
    wait([&](const auto& packets) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->seat && avatar->seat->order == seat_order &&
               avatar->seat->parent_local == new_root->local_id &&
               std::ranges::any_of(packets, [&](const auto& packet) {
                   return avatar_parent(packet, new_root->local_id);
               });
    }, "Linking the occupied prim did not reparent its sitter");
    std::size_t before_delink;
    {
        std::lock_guard lock(mutex);
        before_delink = received.size();
    }
    if (!runtime.world().delink_child(prim->id, new_root->id))
        throw std::runtime_error("Could not delink the occupied prim");
    wait([&](const auto& packets) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->seat && avatar->seat->order == seat_order &&
               avatar->seat->parent_local == prim->local_id &&
               std::ranges::any_of(std::span(packets).subspan(before_delink), [&](const auto& packet) {
                   return avatar_parent(packet, prim->local_id);
               });
    }, "Delinking the occupied prim did not reparent its sitter");
    std::size_t before_teleport;
    {
        std::lock_guard lock(mutex);
        before_teleport = received.size();
    }
    auto teleport = udp::make_packet("TeleportLocationRequest");
    teleport.block("AgentData")["AgentID"] = circuit.agent_id;
    teleport.block("AgentData")["SessionID"] = circuit.session_id;
    auto& destination = teleport.block("Info");
    destination["RegionHandle"] = runtime.descriptor().handle();
    destination["Position"] = std::vector<double>{134, 128, 40};
    destination["LookAt"] = std::vector<double>{1, 0, 0};
    transmit(std::move(teleport));
    wait([&](const auto& packets) {
        bool cleared_camera = false, arrived = false, unparented = false;
        for (const auto& packet : std::span(packets).subspan(before_teleport)) {
            if (packet.spec->name == "ClearFollowCamProperties")
                cleared_camera |= std::get<Uuid>(packet.block("ObjectData").at("ObjectID")) == prim->id;
            if (packet.spec->name == "TeleportLocal")
                arrived |= std::get<std::vector<double>>(packet.block("Info").at("Position")) ==
                           std::vector<double>{134, 128, 40};
            unparented |= avatar_parent(packet, 0);
        }
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return cleared_camera && arrived && unparented && avatar && !avatar->seat;
    }, "Local teleport did not clear the seat and camera and deliver the destination");
    auto reseat = udp::make_packet("AgentRequestSit");
    reseat.block("AgentData")["AgentID"] = circuit.agent_id;
    reseat.block("AgentData")["SessionID"] = circuit.session_id;
    reseat.block("TargetObject")["TargetID"] = prim->id;
    transmit(std::move(reseat));
    wait([&](const auto&) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->seat && avatar->seat->order != seat_order;
    }, "Viewer did not sit again after local teleport");
    std::size_t before_removal;
    {
        std::lock_guard lock(mutex);
        before_removal = received.size();
    }
    if (!runtime.world().remove(prim->id))
        throw std::runtime_error("Could not remove the occupied seat");
    wait([&](const auto& packets) {
        bool cleared_camera = false, unparented = false;
        for (auto i = before_removal; i < packets.size(); ++i) {
            const auto& packet = packets[i];
            if (packet.spec->name == "ClearFollowCamProperties")
                cleared_camera |= std::get<Uuid>(packet.block("ObjectData").at("ObjectID")) == prim->id;
            unparented |= avatar_parent(packet, 0);
        }
        return cleared_camera && unparented;
    }, "Removing the occupied seat did not clear its camera and unparent the avatar");
    const auto standing = runtime.viewers().agent(circuit.agent_id);
    if (!standing || standing->seat)
        throw std::runtime_error("Removing the occupied seat did not clear the avatar's seat state");

    const auto ground_control = [&](std::uint32_t control) {
        auto update = udp::make_packet("AgentUpdate");
        auto& data = update.block("AgentData");
        data["AgentID"] = circuit.agent_id;
        data["SessionID"] = circuit.session_id;
        data["ControlFlags"] = std::uint64_t(control);
        transmit(std::move(update));
    };
    ground_control(0x20000u);
    const Uuid ground_animation("1a2bd58e-87ff-0df8-0b4c-53e047b0bb6e");
    wait([&](const auto& packets) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        if (!avatar || !avatar->ground_sitting || avatar->seat || avatar->velocity != Vector3{})
            return false;
        return std::ranges::any_of(packets, [&](const auto& packet) {
            if (packet.spec->name != "AvatarAnimation" ||
                std::get<Uuid>(packet.block("Sender").at("ID")) != circuit.agent_id)
                return false;
            const auto animations = packet.blocks.find("AnimationList");
            return animations != packet.blocks.end() &&
                   std::ranges::any_of(animations->second, [&](const auto& animation) {
                       return std::get<Uuid>(animation.at("AnimID")) == ground_animation;
                   });
        });
    }, "Ground sit did not stop movement and send the constrained animation");
    ground_control(0x10000u);
    wait([&](const auto&) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && !avatar->ground_sitting && !avatar->seat;
    }, "Stand control did not clear ground seating");
    const auto check_external_unsit = [&](Uuid owner, bool permitted, Uuid group = {}) {
        ground_control(0x20000u);
        wait([&](const auto&) {
            const auto avatar = runtime.viewers().agent(circuit.agent_id);
            return avatar && avatar->ground_sitting;
        }, "Viewer did not sit on the ground before the permission check");
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        auto fixture = SceneObject::primitive(owner, avatar->position);
        fixture.group = group;
        const auto object = runtime.world().add(std::move(fixture));
        if (!object)
            throw std::runtime_error("Could not create the external unsit fixture prim");
        auto script = std::make_shared<TaskInventoryItem>();
        script->item_id = Uuid::random();
        script->asset_id = unsit_asset;
        script->owner_id = script->creator_id = owner;
        script->parent_id = script->parent_part_id = object->id;
        script->inventory_type = script->type = 10;
        script->name = "External unsit permission fixture";
        script->script_running = true;
        auto settings = std::make_shared<TaskInventoryItem>();
        settings->item_id = Uuid::random();
        settings->asset_id = Uuid("9a848f0e-f7df-49c0-9d79-99341ce1d676");
        settings->owner_id = settings->creator_id = owner;
        settings->parent_id = settings->parent_part_id = object->id;
        settings->inventory_type = 25;
        settings->type = 56;
        settings->name = "Parcel day cycle";
        runtime.restore_task_inventory(object->id, {script, settings});
        const std::string expected_message = owner == administrator
            ? "External unsit complete 127.0.0.1" : "External unsit complete ";
        wait([&](const auto& packets) {
            return std::ranges::any_of(packets, [&](const auto& packet) {
                if (packet.spec->name != "ChatFromSimulator")
                    return false;
                const auto& chat = packet.block("ChatData");
                const auto& message = std::get<std::vector<std::uint8_t>>(chat.at("Message"));
                const std::string line(message.begin(), std::find(message.begin(), message.end(), 0));
                if (std::get<Uuid>(chat.at("SourceID")) != object->id)
                    return false;
                if (line.starts_with("Environment fixture failed"))
                    throw std::runtime_error(line);
                return line == expected_message;
            });
        }, "External unsit script did not complete");
        const auto result = runtime.viewers().agent(circuit.agent_id);
        if (!result || result->ground_sitting == permitted)
            throw std::runtime_error("External unsit did not honor the parcel authority");
        runtime.world().remove(object->id);
    };
    check_external_unsit(Uuid::random(), false);
    check_external_unsit(parcel_owner, true);
    check_external_unsit(parcel_group, true, parcel_group);
    check_external_unsit(administrator, true);
    const auto caps = runtime.capabilities()->capabilities().get_caps_for_user(circuit.circuit_code);
    const auto paths = caps->local_paths();
    network::HttpRequest environment_request;
    environment_request.method = "GET";
    environment_request.path = paths.at("ExtEnvironment").value();
    environment_request.raw_target = environment_request.path + "?parcelid=99";
    const auto environment_reply = listener.dispatch(environment_request);
    const auto overridden = network::parse_llsd_xml(environment_reply.body).get("environment");
    if (environment_reply.status != 200 || overridden.get("day_length").integer != 14400 ||
        overridden.get("day_cycle").get("name").as_string() != "Script fixture")
        throw std::runtime_error("Agent override did not take precedence over the requested parcel and newer region timing");
    runtime.world().set_sit_target(new_root->id, {0, 0, 1}, {0, 0, 0, 1});
    auto seat_script = std::make_shared<TaskInventoryItem>();
    seat_script->item_id = Uuid::random();
    seat_script->asset_id = force_sit_asset;
    seat_script->owner_id = seat_script->creator_id = circuit.agent_id;
    seat_script->parent_id = seat_script->parent_part_id = new_root->id;
    seat_script->inventory_type = seat_script->type = 10;
    seat_script->name = "Forced sit fixture";
    seat_script->script_running = true;
    const Uuid animation_id("37aec7d2-5518-4217-9e0c-b6dacb7bc7e6");
    auto animation_item = std::make_shared<TaskInventoryItem>();
    animation_item->item_id = Uuid::random();
    animation_item->asset_id = animation_id;
    animation_item->owner_id = animation_item->creator_id = circuit.agent_id;
    animation_item->parent_id = animation_item->parent_part_id = new_root->id;
    animation_item->inventory_type = 19;
    animation_item->type = 20;
    animation_item->name = "Custom gesture";
    runtime.restore_task_inventory(new_root->id, {seat_script, animation_item});
    wait([&](const auto& packets) {
        return std::ranges::any_of(packets, [](const auto& packet) {
            if (packet.spec->name != "RegionInfo")
                return false;
            const auto& info = packet.block("RegionInfo");
            return std::get<double>(info.at("WaterHeight")) == 27.125 &&
                   std::get<double>(info.at("TerrainRaiseLimit")) == 56 &&
                   std::get<double>(info.at("TerrainLowerLimit")) == -34;
        });
    }, "Script water update did not broadcast the new water height and existing terrain limits");
    wait([&](const auto& packets) {
        const auto avatar = runtime.viewers().agent(circuit.agent_id);
        return avatar && avatar->seat && avatar->seat->part == new_root->id &&
               std::ranges::any_of(packets, [&](const auto& packet) {
                   return avatar_parent(packet, new_root->local_id);
               });
    }, "Authorized osForceOtherSit did not seat the viewer before logout");
    wait([&](const auto& packets) {
        bool started = false;
        for (const auto& packet : packets) {
            if (packet.spec->name != "AvatarAnimation" ||
                std::get<Uuid>(packet.block("Sender").at("ID")) != circuit.agent_id)
                continue;
            const bool playing = std::ranges::any_of(packet.blocks.at("AnimationList"), [&](const auto& row) {
                return std::get<Uuid>(row.at("AnimID")) == animation_id;
            });
            if (playing) {
                started = std::ranges::any_of(packet.blocks.at("AnimationSourceList"), [&](const auto& row) {
                    return std::get<Uuid>(row.at("ObjectID")) == new_root->id;
                });
            } else if (started) {
                return true;
            }
        }
        return false;
    }, "OSSL animation did not play from prim inventory and stop by asset UUID");
    const auto before_logout = parcel_refreshes.load();
    auto logout = udp::make_packet("LogoutRequest");
    logout.block("AgentData")["AgentID"] = circuit.agent_id;
    logout.block("AgentData")["SessionID"] = circuit.session_id;
    transmit(std::move(logout));
    wait([&](const auto&) {
        return !runtime.viewers().agent(circuit.agent_id) && parcel_refreshes.load() > before_logout;
    }, "Seated logout did not release the viewer and refresh the parcel count");
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 4)
            throw std::runtime_error("Expected original ubODE library and animation catalog paths");
        FailingPresence presence;
        auto listener = std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9000, 0, std::string("127.0.0.1")});
        GridRegion region;
        region.id = Uuid::random();
        region.name = "Cleanup";
        region.estate_owner = Uuid::random();
        region.external_host = "127.0.0.1";
        region.internal_endpoint = RegionEndpoint{"127.0.0.1", 0};
        RegionPolicies policies;
        policies.environment_enabled = true;
        policies.estate_menu_flags |= 0x200u;
        std::vector<std::pair<std::optional<Uuid>, std::string>> environment_writes;
        policies.persist_environment = [&](auto parcel, auto settings) {
            if (!settings)
                throw std::runtime_error("Unexpected environment write");
            environment_writes.emplace_back(parcel, std::move(*settings));
        };
        std::vector<std::array<float, 3>> terrain_settings_writes;
        policies.persist_region_terrain_settings = [&](float water, float raise, float lower) {
            terrain_settings_writes.push_back({water, raise, lower});
        };
        std::atomic<unsigned> parcel_refreshes = 0;
        policies.parcel_prim_count_tainted = [&] { ++parcel_refreshes; };
        policies.physics_library = argv[1];
        policies.animations = std::make_shared<DefaultAnimations>(argv[2]);
        if (argc == 4)
            policies.spawn_point_routing = argv[3];
        policies.access = [routing = policies.spawn_point_routing](auto, const auto& position, auto& reason) {
            if (position.x == 128 || (position.x == 96 && routing == "random")) {
                reason = "Fixture spawn position denied";
                return false;
            }
            return true;
        };
        policies.authorize = [](const auto&, auto&) { return true; };
        const auto covenant = std::make_shared<AssetBase>(Uuid::random(), "Fixture covenant",
                                                          static_cast<std::int8_t>(AssetType::Notecard), "");
        covenant->data = std::make_shared<std::vector<std::uint8_t>>(785, 0);
        for (std::size_t i = 0; i < covenant->data->size(); ++i)
            (*covenant->data)[i] = static_cast<std::uint8_t>(i % 251);
        const auto library_item = Uuid::random();
        InventoryFolderStore inventory_store(":memory:", true);
        InventoryService inventory(inventory_store);
        policies.inventory_service = &inventory;
        policies.can_edit = [](Uuid agent, const SceneObject& object) { return agent == object.owner; };
        policies.minimum_asset_upload_level = 50;
        policies.asset_upload_user_level = [](Uuid) { return 10; };
        const auto group = Uuid::random();
        const auto other_group = Uuid::random();
        auto group_parcel = std::make_shared<ScriptParcel>();
        group_parcel->id = Uuid::random();
        group_parcel->group = group;
        auto hub_parcel = std::make_shared<ScriptParcel>();
        hub_parcel->id = Uuid::random();
        auto other_parcel = std::make_shared<ScriptParcel>();
        other_parcel->id = Uuid::random();
        other_parcel->group = other_group;
        other_parcel->owner = Uuid::random();
        other_parcel->group_owned = true;
        const auto blocked_visitor = Uuid::random();
        constexpr std::uint32_t use_ban_list = 1u << 10;
        for (const auto& parcel : {hub_parcel, other_parcel}) {
            parcel->flags |= use_ban_list;
            parcel->access_entries.push_back({blocked_visitor, 2, 0});
        }
        policies.god_rights_level = [blocked_visitor](Uuid agent) {
            return agent == blocked_visitor ? 200 : 0;
        };
        const auto hub = SceneObject::primitive(Uuid::random(), {96, 64, 25});
        policies.script_parcel_at = [=](Vector3 position) -> std::shared_ptr<const ScriptParcel> {
            if (position.x < 64)
                return group_parcel;
            if (position.x < 128)
                return hub_parcel;
            return other_parcel;
        };
        policies.inventory_group_powers = [group](Uuid, Uuid parcel_group) {
            return parcel_group == group ? 1ULL << 28 : 0ULL;
        };
        std::mutex home_mutex;
        std::vector<Vector3> home_writes;
        std::vector<std::pair<Uuid, std::size_t>> telehub_writes;
        policies.persist_telehub = [&](Uuid id, const auto& points) {
            std::lock_guard lock(home_mutex);
            telehub_writes.emplace_back(id, points.size());
        };
        policies.set_home = [&](Uuid, Uuid region_id, Vector3 position, Vector3 look) {
            if (region_id != region.id || look != Vector3{0, 1, 0})
                return false;
            std::lock_guard lock(home_mutex);
            home_writes.push_back(position);
            return true;
        };
        std::mutex uploaded_mutex;
        std::map<Uuid, std::shared_ptr<AssetBase>> uploads;
        std::vector<std::pair<InventoryItem, int>> upload_events;
        policies.inventory_upload_complete = [&](const InventoryItem& item, int level) {
            upload_events.emplace_back(item, level);
        };
        policies.store_inventory_asset = [&](auto asset) -> std::optional<std::string> {
            std::lock_guard lock(uploaded_mutex);
            const auto id = asset->full_id();
            uploads.emplace(id, std::move(asset));
            return id.str();
        };
        const auto uploaded_asset = [&](Uuid id) -> std::shared_ptr<AssetBase> {
            std::lock_guard lock(uploaded_mutex);
            const auto entry = uploads.find(id);
            return entry == uploads.end() ? nullptr : entry->second;
        };
        const auto unsit_script = std::make_shared<AssetBase>(Uuid::random(), "External unsit",
                                                              static_cast<std::int8_t>(AssetType::LSLText), "");
        const auto force_sit_script = std::make_shared<AssetBase>(
            Uuid::random(), "Forced sit", static_cast<std::int8_t>(AssetType::LSLText), "");
        const auto script_day_cycle = std::make_shared<AssetBase>(
            Uuid("9a848f0e-f7df-49c0-9d79-99341ce1d676"), "Script day cycle",
            static_cast<std::int8_t>(AssetType::Settings), "");
        const std::string_view day_cycle_data = "<? llsd/notation ?>\n"
            "{'type':'daycycle','name':'Script fixture','frames':{'Sky':{'type':'sky'}},"
            "'tracks':[[],[{'key_keyframe':r0,'key_name':'Sky'}],[],[],[]]}";
        script_day_cycle->data = std::make_shared<std::vector<std::uint8_t>>(
            day_cycle_data.begin(), day_cycle_data.end());
        policies.script_ossl_threat_level = "None";
        policies.script_ossl_rules["osForceOtherSit"].owners = "true";
        policies.script_ossl_rules["osGetAgentIP"].owners = "true";
        policies.script_ossl_rules["osAvatarPlayAnimation"].owners = "true";
        policies.script_ossl_rules["osAvatarStopAnimation"].owners = "true";
        policies.script_ossl_rules["osSetRegionWaterHeight"].owners = "true";
        policies.script_ossl_rules["osSetRegionSunSettings"].owners = "true";
        policies.script_ossl_rules["osReplaceAgentEnvironment"].owners = blocked_visitor.str();
        policies.load_asset = [covenant, unsit_script, force_sit_script, script_day_cycle](Uuid id)
                -> std::shared_ptr<AssetBase> {
            if (id == covenant->full_id())
                return covenant;
            if (id == unsit_script->full_id())
                return unsit_script;
            if (id == script_day_cycle->full_id())
                return script_day_cycle;
            return id == force_sit_script->full_id() ? force_sit_script : nullptr;
        };
        policies.library_inventory_item = [library_item, covenant](Uuid id) -> std::optional<InventoryItem> {
            if (id != library_item)
                return {};
            InventoryItem item;
            item.id = id;
            item.asset_id = covenant->full_id();
            item.asset_type = static_cast<int>(AssetType::Notecard);
            return item;
        };
        {
            policies.estate_handshake_only_flags &= ~0x100000u;
            RegionRuntime runtime(region, presence, policies);
            runtime.set_terrain_limits(-34, 56);
            runtime.load_region_sun_settings(false);
            runtime.set_region_telehub(hub.id, {{0, 0, 32}, {0, 0, 0}, {0, 0, 16}});
            auto bound = runtime.start(listener);
            auto circuit = std::make_shared<AgentCircuitData>();
            circuit->agent_id = region.estate_owner;
            circuit->first_name = "Native";
            circuit->last_name = "Viewer";
            circuit->session_id = Uuid::random();
            circuit->secure_session_id = Uuid("d36e9ccb-4bbb-46c9-8e5e-b8cb159d490d");
            circuit->circuit_code = 42;
            circuit->caps_path = "shutdown";
            circuit->start_position = {126, 64, 25};
            const auto unsit_source = "key avatar = \"" + circuit->agent_id.str() + "\";\n" +
                "key administrator = \"" + blocked_visitor.str() + "\";\n" + R"LSL(
                default {
                    state_entry() {
                        llUnSit(avatar);
                        if (llGetOwner() == administrator) {
                            if (osReplaceAgentEnvironment("invalid", 0, NULL_KEY) != -4
                                || osReplaceAgentEnvironment(avatar, 0, "Missing setting") != -3
                                || osReplaceAgentEnvironment(avatar, 0, "Parcel day cycle") != 1
                                || osReplaceAgentEnvironment(avatar, 0, NULL_KEY) != 1
                                || osReplaceAgentEnvironment(avatar, 1, "Parcel day cycle") != 1) {
                                llSay(0, "Environment fixture failed agent override");
                                return;
                            }
                            integer missing = osReplaceRegionEnvironment(0, "Missing setting", 6, -1, 300, 100, 200);
                            if (missing != -4) {
                                llSay(0, "Environment fixture failed missing asset " + (string)missing);
                                return;
                            }
                            if (osReplaceRegionEnvironment(0, NULL_KEY, 0, 99, 0, 0, 0) != 1)
                                return;
                            integer result = osReplaceRegionEnvironment(1, "9a848f0e-f7df-49c0-9d79-99341ce1d676",
                                    6, -1, 300, 100, 200);
                            if (result != 1 || llGetRegionDayLength() != 21600 || llGetRegionDayOffset() != 82800) {
                                llSay(0, "Environment fixture failed edit " + (string)result);
                                return;
                            }
                            integer parcel_result = osReplaceParcelEnvironment(1, "Parcel day cycle");
                            if (parcel_result != 1 || llGetDayLength() != 21600 || llGetDayOffset() != 82800
                                || osReplaceParcelEnvironment(0, NULL_KEY) != 1) {
                                llSay(0, "Environment fixture failed parcel edit " + (string)parcel_result);
                                return;
                            }
                        }
                        else if (osReplaceRegionEnvironment(0, "", 6, -1, 300, 100, 200) != -3) {
                            llSay(0, "Environment fixture failed permission check");
                            return;
                        }
                        else if (osReplaceAgentEnvironment(avatar, 0, NULL_KEY) != -2) {
                            llSay(0, "Environment fixture failed agent permission check");
                            return;
                        }
                        if (osGetAgentIP("invalid") == "" && osGetAgentIP(NULL_KEY) == "")
                            llSay(0, "External unsit complete " + osGetAgentIP(avatar));
                    }
                }
            )LSL";
            if (const auto compiled = script::compile_lsl(unsit_source); !compiled)
                throw std::runtime_error("External unsit fixture did not compile: " + compiled.diagnostics.front().message);
            unsit_script->data = std::make_shared<std::vector<std::uint8_t>>(unsit_source.begin(), unsit_source.end());
            const std::string_view force_sit_source = R"LSL(
                default {
                    state_entry() {
                        osSetRegionWaterHeight(27.125);
                        osSetRegionWaterHeight(-1);
                        osSetRegionSunSettings(TRUE, TRUE, 11.25);
                        list agents = osGetAgents();
                        if (llWater(ZERO_VECTOR) == 27.125
                            && llGetListLength(agents) == 1
                            && llGetListEntryType(agents, 0) == TYPE_STRING
                            && llList2String(agents, 0) == llKey2Name(llGetOwner())) {
                            osForceOtherSit(llGetOwner());
                            osAvatarPlayAnimation(llGetOwner(), "Custom gesture");
                            key animation = "37aec7d2-5518-4217-9e0c-b6dacb7bc7e6";
                            osAvatarStopAnimation(llGetOwner(), animation);
                        }
                    }
                }
            )LSL";
            force_sit_script->data = std::make_shared<std::vector<std::uint8_t>>(
                force_sit_source.begin(), force_sit_source.end());
            presence.login(circuit->agent_id.str(), circuit->session_id, circuit->secure_session_id);
            LoginTransferContext context;
            std::string reason;
            if (!runtime.world().add(hub))
                throw std::runtime_error("Could not place the foreign admission fixture telehub");
            auto foreign = std::make_shared<AgentCircuitData>(*circuit);
            foreign->agent_id = blocked_visitor;
            foreign->session_id = Uuid::random();
            foreign->secure_session_id = Uuid::random();
            foreign->circuit_code = 44;
            foreign->verified_foreign_login = true;
            if (runtime.create_agent(nullptr, region, foreign, 1u << 30, context, reason) ||
                reason != "No suitable landing point found")
                throw std::runtime_error("Foreign visitor bypassed telehub restrictions: " + reason);
            if (!runtime.world().remove(hub.id))
                throw std::runtime_error("Could not remove the foreign admission fixture telehub");
            if (!runtime.create_agent(nullptr, region, circuit, 128, context, reason) ||
                listener->routes.simple_keys().empty() || listener->routes.poll_keys().empty())
                throw std::runtime_error("Region admission did not own live capability routes");
            auto abandoned = std::make_shared<AgentCircuitData>(*circuit);
            abandoned->agent_id = Uuid::random();
            abandoned->session_id = Uuid::random();
            abandoned->secure_session_id = Uuid::random();
            abandoned->circuit_code = 43;
            abandoned->caps_path = "second-unconnected";
            presence.login(abandoned->agent_id.str(), abandoned->session_id, abandoned->secure_session_id);
            if (!runtime.create_agent(nullptr, region, abandoned, 128, context, reason))
                throw std::runtime_error("Unconnected login reservation blocked subsequent admission");
            if (!inventory.create_user_inventory(circuit->agent_id))
                throw std::runtime_error("Could not create upload test inventory");
            verify_viewer_packets(runtime, *listener, bound, *circuit, *covenant, library_item, uploaded_asset,
                                  inventory, hub, policies.spawn_point_routing == "sequence" ? 96.f : 112.f,
                                  parcel_refreshes, unsit_script->full_id(), force_sit_script->full_id(), other_parcel->owner,
                                  other_parcel->group, blocked_visitor);
            if (terrain_settings_writes != std::vector<std::array<float, 3>>{{27.125f, 56.f, -34.f}})
                throw std::runtime_error("Script water height must persist once and preserve terrain limits");
            if (!runtime.use_estate_sun())
                throw std::runtime_error("Script did not enable the estate-sun setting");
            if (environment_writes.size() != 2 || environment_writes[0].first ||
                environment_writes[1].first != other_parcel->id)
                throw std::runtime_error("Environment edits should persist once in their region and parcel scopes");
            const auto environment = network::parse_llsd_xml(environment_writes[0].second);
            const auto parcel_environment = network::parse_llsd_xml(environment_writes[1].second);
            if (parcel_environment.get("day_length").integer != 21600 ||
                parcel_environment.get("day_offset").integer != 82800 ||
                parcel_environment.get("day_cycle").get("name").as_string() != "Script fixture")
                throw std::runtime_error("Parcel environment did not inherit the region timing and save the inventory cycle");
            const auto& altitudes = environment.get("track_altitudes").array;
            if (environment.get("day_length").integer != 21600 || environment.get("day_offset").integer != 82800 ||
                environment.get("day_cycle").get("name").as_string() != "Script fixture" ||
                altitudes.size() != 3 || altitudes[0].real != 100 || altitudes[1].real != 200 ||
                altitudes[2].real != 300)
                throw std::runtime_error("Script did not persist the requested timing and sorted altitude settings");
            {
                std::lock_guard lock(home_mutex);
                if (home_writes != std::vector<Vector3>{{32, 64, 60}, {96, 64, 60}})
                    throw std::runtime_error("Set Home persisted a location outside its authorized parcel");
                if (telehub_writes != std::vector<std::pair<Uuid, std::size_t>>{
                        {hub.id, 0}, {hub.id, 1}, {hub.id, 0}, {Uuid{}, 0}})
                    throw std::runtime_error("Telehub management persisted the wrong state");
            }
            std::vector<Uuid> updated_assets;
            for (const auto& [item, level] : upload_events) {
                if (item.name != "Edit")
                    continue;
                const auto asset = uploaded_asset(item.asset_id);
                const auto expected =
                    std::vector<std::uint8_t>{static_cast<std::uint8_t>(3 + updated_assets.size()), 7, 0, 8};
                if (level != 0 || item.owner != circuit->agent_id || !asset || !asset->data ||
                    *asset->data != expected)
                    throw std::runtime_error(
                        "UDP inventory upload completion reported the wrong item or asset");
                updated_assets.push_back(item.asset_id);
            }
            if (updated_assets.size() != 2 || updated_assets.front() == updated_assets.back())
                throw std::runtime_error("Each successful UDP inventory asset update must publish one event");
            bool failed = false;
            try {
                runtime.stop();
            } catch (const std::runtime_error& e) {
                failed = std::string(e.what()) == "controlled presence cleanup failure";
            }
            if (!failed || runtime.capabilities() || !listener->routes.simple_keys().empty() ||
                !listener->routes.poll_keys().empty())
                throw std::runtime_error("Cleanup failure skipped capability disposal");
            network::UdpSocket replacement({bound.address, bound.port}, [](const auto&, auto) {});
            replacement.start();
            replacement.stop();
            if (runtime.create_agent(nullptr, region, circuit, 128, context, reason))
                throw std::runtime_error("Stopped region accepted new login");
        } // destructor repeats throwing provider cleanup without terminating the process.
        if (presence.region_logouts != 2)
            throw std::runtime_error("Destructor cleanup was not exercised");
        std::cout << "PASS: region cleanup continues after provider failure; destructor does not terminate\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
