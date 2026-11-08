// Connected viewer-protocol probe; uses production codecs over actual UDP.
#include "opensim/network/udp_socket.hpp"
#include "opensim/network/lludp.hpp"
#include "opensim/scene/terrain_layer.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/texture_entry.hpp"
#include "opensim/framework/avatar_wearable.hpp"
#include "opensim/framework/crc32.hpp"
#include "legacy_inventory_probe.hpp"
#include "task_inventory_probe.hpp"
#include "inventory_script_rez_probe.hpp"
#include <condition_variable>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <fstream>
#include <optional>
#include <iostream>
#include <set>
#include <source_location>
using namespace opensim;
using namespace framework;
using namespace network;
int main(int argc, char** argv) {
    try {
        const bool restart_inventory = argc >= 12 && std::string_view(argv[6]) == "--terrain-only" &&
                                       std::string_view(argv[8]) == "--bake";
        if (argc < 6 || (restart_inventory && argc % 2 != 0))
            throw std::invalid_argument(
                "agent session circuit host port [state message] [touch message] [--walk] [legacy metadata] required");
        bool has_touch = argc > 7 && std::string(argv[7]).size() > 0,
             block_flight = argc > 8 && std::string_view(argv[8]) == "--walk-block-flight",
             walk = block_flight || (argc > 8 && std::string_view(argv[8]) == "--walk");
        Uuid agent(argv[1]), session(argv[2]);
        auto code = static_cast<std::uint32_t>(std::stoul(argv[3]));
        UdpEndpoint destination{argv[4], static_cast<std::uint16_t>(std::stoul(argv[5]))};
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<lludp::Packet> packets;
        std::atomic<std::uint32_t> sequence = 1;
        UdpSocket* sending = nullptr;
        UdpSocket socket({"127.0.0.1", 0}, [&](const auto& from, auto bytes) {
            auto p = lludp::decode_datagram(bytes, 65536, false);
            if (p.reliable) {
                auto ack = lludp::make_packet("PacketAck");
                ack.sequence = sequence++;
                auto b = lludp::make_block(ack.spec->blocks[0]);
                b["ID"] = std::uint64_t(p.sequence);
                ack.blocks["Packets"].push_back(std::move(b));
                sending->send_to(from, lludp::encode_datagram(ack));
            }
            {
                std::lock_guard lock(mutex);
                packets.push_back(std::move(p));
            }
            changed.notify_all();
        });
        sending = &socket;
        socket.start();
        auto send = [&](const char* name) {
            auto p = lludp::make_packet(name);
            p.sequence = sequence++;
            if (p.blocks.contains("AgentData")) {
                p.block("AgentData")["AgentID"] = agent;
                p.block("AgentData")["SessionID"] = session;
            }
            return p;
        };
        auto transmit = [&](const auto& p) { socket.send_to(destination, lludp::encode_datagram(p)); };
        int wait_index = 0;
        auto wait = [&](auto predicate, std::source_location location = std::source_location::current()) {
            ++wait_index;
            std::unique_lock lock(mutex);
            if (!changed.wait_for(lock, std::chrono::seconds(12), [&] { return predicate(packets); })) {
                std::set<std::string> names;
                for (auto& p : packets) {
                    names.insert(p.spec->name);
                    if (p.spec->name == "ChatFromSimulator" || p.spec->name == "AlertMessage") {
                        auto& b = p.block(p.spec->name == "AlertMessage" ? "AlertData" : "ChatData");
                        auto& bytes = std::get<std::vector<std::uint8_t>>(b.at("Message"));
                        std::cerr << "Message: " << std::string(bytes.begin(), bytes.end()) << '\n';
                    }
                }
                std::string seen;
                for (auto& name : names)
                    seen += name + " ";
                throw std::runtime_error("Viewer session timeout at wait " + std::to_string(wait_index) +
                                         " (probe line " + std::to_string(location.line()) + "): " + seen);
            }
        };
        auto has = [](const auto& list, const char* name) {
            return std::any_of(list.begin(), list.end(), [&](const auto& p) { return p.spec->name == name; });
        };
        auto use = send("UseCircuitCode");
        use.block("CircuitCode")["ID"] = agent;
        use.block("CircuitCode")["SessionID"] = session;
        use.block("CircuitCode")["Code"] = std::uint64_t(code);
        transmit(use);
        wait([&](const auto& p) { return has(p, "RegionHandshake"); });
        if (argc > 6 && std::string_view(argv[6]) == "--check-scripting-enabled") {
            std::lock_guard lock(mutex);
            const auto packet = std::find_if(packets.begin(), packets.end(),
                                             [](const auto& p) { return p.spec->name == "RegionHandshake"; });
            const auto flags = std::get<std::uint64_t>(packet->block("RegionInfo").at("RegionFlags"));
            const auto extended =
                std::get<std::uint64_t>(packet->block("RegionInfo4").at("RegionFlagsExtended"));
            if ((flags & 0x2000u) || flags != extended)
                throw std::runtime_error("Reconnect received stale scripting flags");
            return 0;
        }
        auto reply = send("RegionHandshakeReply");
        transmit(reply);
        if (argc > 6 && std::string_view(argv[6]) == "Rotating fixture") {
            std::lock_guard lock(mutex);
            const auto handshake = std::find_if(packets.begin(), packets.end(), [](const auto& p) {
                return p.spec->name == "RegionHandshake";
            });
            constexpr std::uint64_t expected_flags = 0x1u | 0x40u | 0x4000u | 0x80000u;
            const auto flags = std::get<std::uint64_t>(handshake->block("RegionInfo").at("RegionFlags"));
            const auto extended =
                std::get<std::uint64_t>(handshake->block("RegionInfo4").at("RegionFlagsExtended"));
            if ((flags & expected_flags) != expected_flags || extended != flags)
                throw std::runtime_error("Persisted region flags missing or inconsistent in handshake");
            if (std::get<double>(handshake->block("RegionInfo").at("WaterHeight")) != 27.5)
                throw std::runtime_error("Missing stored region water height");
        }
        if (block_flight) {
            std::lock_guard lock(mutex);
            const auto handshake = std::find_if(packets.begin(), packets.end(), [](const auto& p) {
                return p.spec->name == "RegionHandshake";
            });
            if (handshake == packets.end() ||
                !(std::get<std::uint64_t>(handshake->block("RegionInfo").at("RegionFlags")) & 0x80000u))
                throw std::runtime_error("Blocked-flight physics fixture did not advertise BlockFly");
        }
        const bool advanced_profiles = argc > 11 && std::string_view(argv[11]) == "--advanced-profiles";
        const bool local_offline = argc > 11 && std::string_view(argv[11]) == "--local-offline";
        if (!advanced_profiles && !local_offline) {
            auto wrong_session = send("UserInfoRequest");
            wrong_session.block("AgentData")["SessionID"] = Uuid{};
            transmit(wrong_session);
            {
                std::unique_lock lock(mutex);
                if (changed.wait_for(lock, std::chrono::milliseconds(150),
                                     [&] { return has(packets, "UserInfoReply"); }))
                    throw std::runtime_error("User info reply accepted a mismatched session");
            }
            auto update_info = send("UpdateUserInfo");
            update_info.block("UserData")["IMViaEMail"] = true;
            update_info.block("UserData")["DirectoryVisibility"] =
                std::vector<std::uint8_t>({'h', 'i', 'd', 'd', 'e', 'n', 0});
            transmit(update_info);
            transmit(send("UserInfoRequest"));
            wait([&](const auto& list) { return has(list, "UserInfoReply"); });
            {
                std::lock_guard lock(mutex);
                const auto found = std::find_if(packets.begin(), packets.end(), [](const auto& packet) {
                    return packet.spec->name == "UserInfoReply";
                });
                const auto& data = found->block("UserData");
                if (std::get<Uuid>(found->block("AgentData").at("AgentID")) != agent ||
                    std::get<bool>(data.at("IMViaEMail")) ||
                    std::get<std::vector<std::uint8_t>>(data.at("DirectoryVisibility")) !=
                        std::vector<std::uint8_t>({'d', 'e', 'f', 'a', 'u', 'l', 't', 0}) ||
                    !std::get<std::vector<std::uint8_t>>(data.at("EMail")).empty())
                    throw std::runtime_error("Default user info reply differs from LLClientView");
            }
            std::cout << "PASS: default user info reply before root" << std::endl;
        } else if (advanced_profiles) {
            const Uuid system_agent("11111111-1111-0000-0000-000100bba000");
            auto system_request = send("AvatarPropertiesRequest");
            system_request.block("AgentData")["AvatarID"] = system_agent;
            transmit(system_request);
            wait([&](const auto& list) {
                for (const auto& reply : list) {
                    if (reply.spec->name != "AvatarPropertiesReply")
                        continue;
                    const auto& ids = reply.block("AgentData");
                    const auto& fields = reply.block("PropertiesData");
                    const auto& born = std::get<std::vector<std::uint8_t>>(fields.at("BornOn"));
                    const std::string expected("01/01/2007 00:00:00\0", 20);
                    if (std::get<Uuid>(ids.at("AgentID")) != agent ||
                        std::get<Uuid>(ids.at("AvatarID")) != system_agent ||
                        std::get<std::uint64_t>(fields.at("Flags")) != 0x10 ||
                        !std::equal(born.begin(), born.end(), expected.begin(), expected.end()))
                        throw std::runtime_error("System agent profile differs from the original");
                    return true;
                }
                return false;
            });
            auto request = send("GenericMessage");
            request.block("MethodData")["Method"] =
                std::vector<std::uint8_t>{' ', 'A', 'v', 'a', 't', 'a', 'r', 'P', 'i', 'c', 'k',
                                          's', 'R', 'e', 'q', 'u', 'e', 's', 't', ' ', 0};
            auto malformed = send("GenericMessage");
            malformed.block("MethodData")["Method"] = std::vector<std::uint8_t>{
                'a', 'v', 'a', 't', 'a', 'r', 'n', 'o', 't', 'e', 's', 'r', 'e', 'q', 'u', 'e', 's', 't', 0};
            transmit(malformed);
            auto parameter = lludp::make_block(request.spec->blocks.at(2));
            auto target = agent.str();
            std::vector<std::uint8_t> bytes(target.begin(), target.end());
            bytes.push_back(0);
            parameter["Parameter"] = std::move(bytes);
            request.blocks["ParamList"].push_back(std::move(parameter));
            auto wrong_session = request;
            wrong_session.block("AgentData")["SessionID"] = Uuid::random();
            transmit(wrong_session);
            {
                std::unique_lock lock(mutex);
                if (changed.wait_for(lock, std::chrono::milliseconds(150),
                                     [&] { return has(packets, "AvatarPicksReply"); }))
                    throw std::runtime_error("Mismatched session received advanced profile data");
            }
            transmit(request);
            wait([&](const auto& list) {
                for (const auto& reply : list) {
                    if (reply.spec->name != "AvatarPicksReply")
                        continue;
                    const auto& ids = reply.block("AgentData");
                    const auto& pick = reply.block("Data");
                    const std::string expected = "Native profile pick";
                    auto expected_bytes = std::vector<std::uint8_t>(expected.begin(), expected.end());
                    expected_bytes.push_back(0);
                    if (std::get<Uuid>(ids.at("AgentID")) != agent ||
                        std::get<Uuid>(ids.at("TargetID")) != agent ||
                        std::get<Uuid>(pick.at("PickID")) != Uuid("91919191-9191-4191-8191-919191919191") ||
                        std::get<std::vector<std::uint8_t>>(pick.at("PickName")) != expected_bytes)
                        throw std::runtime_error("Advanced profile picks reply differs from SQLite data");
                    return true;
                }
                return false;
            });
            std::cout << "PASS: advanced profile picks before root" << std::endl;
            const std::vector<std::uint8_t> note{'N', 'o', 't', 'e', '\n', 0xc3, 0xa9, 0};
            auto update = send("AvatarNotesUpdate");
            update.block("Data")["TargetID"] = agent;
            update.block("Data")["Notes"] = note;
            transmit(update);
            auto notes_request = send("GenericMessage");
            notes_request.blocks["ParamList"] = request.blocks.at("ParamList");
            notes_request.block("MethodData")["Method"] = std::vector<std::uint8_t>{
                'a', 'v', 'a', 't', 'a', 'r', 'n', 'o', 't', 'e', 's', 'r', 'e', 'q', 'u', 'e', 's', 't', 0};
            transmit(notes_request);
            wait([&](const auto& list) {
                for (const auto& reply : list) {
                    if (reply.spec->name != "AvatarNotesReply")
                        continue;
                    const auto& data = reply.block("Data");
                    if (std::get<Uuid>(reply.block("AgentData").at("AgentID")) != agent ||
                        std::get<Uuid>(data.at("TargetID")) != agent ||
                        std::get<std::vector<std::uint8_t>>(data.at("Notes")) != note)
                        throw std::runtime_error("Advanced profile note did not round-trip");
                    return true;
                }
                return false;
            });
            std::cout << "PASS: advanced profile note round trip" << std::endl;
            auto preferences = send("UpdateUserInfo");
            preferences.block("UserData")["IMViaEMail"] = true;
            preferences.block("UserData")["DirectoryVisibility"] =
                std::vector<std::uint8_t>{'h', 'i', 'd', 'd', 'e', 'n', 0};
            transmit(preferences);
            transmit(send("UserInfoRequest"));
            {
                std::unique_lock lock(mutex);
                if (changed.wait_for(lock, std::chrono::milliseconds(200),
                                     [&] { return has(packets, "UserInfoReply"); }))
                    throw std::runtime_error("SQLite preference read unexpectedly returned a viewer reply");
            }
        }
        if (local_offline) {
            auto movement = send("CompleteAgentMovement");
            movement.block("AgentData")["CircuitCode"] = std::uint64_t(code);
            transmit(movement);
            wait([&](const auto& list) { return has(list, "AgentMovementComplete"); });
            transmit(send("RetrieveInstantMessages"));
            const Uuid sender("93939393-9393-4393-8393-939393939393");
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "ImprovedInstantMessage" ||
                        std::get<Uuid>(packet.block("AgentData").at("AgentID")) != sender)
                        continue;
                    const auto& data = packet.block("MessageBlock");
                    if (std::get<Uuid>(data.at("ToAgentID")) == agent &&
                        std::get<std::uint64_t>(data.at("Dialog")) == 4 &&
                        std::get<std::uint64_t>(data.at("Offline")) == 0 &&
                        std::get<std::vector<std::uint8_t>>(data.at("BinaryBucket")) ==
                            std::vector<std::uint8_t>{0, 1, 255})
                        return true;
                }
                return false;
            });
            return 0;
        }
        if (!restart_inventory && argc > 9 && std::string_view(argv[9]).size()) {
            tests::LegacyInventoryProbeIo io;
            io.make = send;
            io.send = transmit;
            io.wait = [&](auto predicate) { wait(predicate); };
            io.snapshot = [&] {
                std::lock_guard lock(mutex);
                return packets;
            };
            tests::run_legacy_inventory_probe(agent, std::filesystem::path(argv[9]), io);
        }
        bool profiles_disabled =
            advanced_profiles || (argc > 11 && std::string_view(argv[11]) == "--no-profiles");
        Uuid profile_unknown("ffffffff-ffff-ffff-ffff-ffffffffffff");
        auto profile_request = send("AvatarPropertiesRequest");
        profile_request.block("AgentData")["AvatarID"] = profile_unknown;
        if (!advanced_profiles)
            transmit(profile_request);
        auto profile_matches = [&](const auto& list, Uuid target) {
            bool props = false, interests = false;
            for (auto& p : list) {
                if (p.spec->name != "AvatarPropertiesReply" && p.spec->name != "AvatarInterestsReply")
                    continue;
                auto& data = p.block("AgentData");
                if (std::get<Uuid>(data.at("AvatarID")) != target)
                    continue;
                if (std::get<Uuid>(data.at("AgentID")) != agent)
                    throw std::runtime_error("Profile reply recipient mismatch");
                auto& fields = p.block("PropertiesData");
                auto bytes = [&](const char* name) {
                    return std::get<std::vector<std::uint8_t>>(fields.at(name));
                };
                if (p.spec->name == "AvatarPropertiesReply") {
                    std::string member = target == profile_unknown ? "Avatar" : "Native Viewer";
                    std::vector<std::uint8_t> membership(member.begin(), member.end());
                    membership.push_back(0);
                    if (bytes("CharterMember") != membership ||
                        bytes("ProfileURL") !=
                            std::vector<std::uint8_t>({'N', 'o', ' ', 'p', 'r', 'o', 'f', 'i', 'l', 'e', ' ',
                                                       'd', 'a', 't', 'a', 0}) ||
                        !bytes("AboutText").empty() || !bytes("FLAboutText").empty() ||
                        std::get<std::uint64_t>(fields.at("Flags")) != 0 ||
                        !std::get<Uuid>(fields.at("ImageID")).is_zero() ||
                        !std::get<Uuid>(fields.at("FLImageID")).is_zero() ||
                        !std::get<Uuid>(fields.at("PartnerID")).is_zero())
                        throw std::runtime_error("Profile properties source fields mismatch");
                    if (target == profile_unknown &&
                        bytes("BornOn") !=
                            std::vector<std::uint8_t>({'1', '/', '1', '/', '1', '9', '7', '0', 0}))
                        throw std::runtime_error("Unknown profile birth date mismatch");
                    props = true;
                } else {
                    if (std::get<std::uint64_t>(fields.at("WantToMask")) != 0 ||
                        std::get<std::uint64_t>(fields.at("SkillsMask")) != 0 ||
                        !bytes("WantToText").empty() || !bytes("SkillsText").empty() ||
                        !bytes("LanguagesText").empty())
                        throw std::runtime_error("Profile interests source fields mismatch");
                    interests = true;
                }
            }
            return props && interests;
        };
        if (!profiles_disabled) {
            wait([&](const auto& list) { return profile_matches(list, profile_unknown); });
            std::cout << "PASS: unknown avatar properties/interests before root" << std::endl;
        }
        auto complete = send("CompleteAgentMovement");
        complete.block("AgentData")["CircuitCode"] = std::uint64_t(code);
        transmit(complete);
        if (argc > 6 && (std::string_view(argv[6]) == "--rez-persistent" ||
                         std::string_view(argv[6]) == "--restore-persistent")) {
            const bool rez = std::string_view(argv[6]) == "--rez-persistent";
            wait([&](const auto& list) { return has(list, "AgentMovementComplete"); });
            std::uint64_t root_local = 0, child_local = 0;
            if (rez) {
                auto add = send("ObjectAdd");
                auto& shape = add.block("ObjectData");
                shape["PCode"] = std::uint64_t{9};
                shape["PathCurve"] = std::uint64_t{16};
                shape["ProfileCurve"] = std::uint64_t{1};
                shape["PathScaleX"] = shape["PathScaleY"] = std::uint64_t{100};
                shape["Scale"] = std::vector<double>{.75, 1.25, .5};
                shape["Rotation"] = std::vector<double>{0, 0, 0, 1};
                shape["RayStart"] = std::vector<double>{128, 128, 70};
                shape["RayEnd"] = std::vector<double>{128, 128, 21};
                shape["RayEndIsIntersection"] = std::uint64_t{1};
                transmit(add);
                add.sequence = sequence++;
                shape["Scale"] = std::vector<double>{.5, .75, 1.25};
                shape["RayEnd"] = std::vector<double>{130, 128, 21};
                transmit(add);
            }
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ObjectUpdate")
                        for (const auto& part : packet.blocks.at("ObjectData")) {
                            if (std::get<std::uint64_t>(part.at("PCode")) != 9 ||
                                std::get<std::vector<double>>(part.at("Scale")) !=
                                    std::vector<double>{.75, 1.25, .5})
                                continue;
                            const auto id = std::get<Uuid>(part.at("FullID")).str();
                            if (!rez && (argc < 8 || id != argv[7]))
                                continue;
                            root_local = std::get<std::uint64_t>(part.at("ID"));
                            std::cout << id << std::endl;
                            return true;
                        }
                return false;
            });
            if (rez) {
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "ObjectUpdate")
                            for (const auto& part : packet.blocks.at("ObjectData"))
                                if (std::get<std::uint64_t>(part.at("PCode")) == 9 &&
                                    std::get<std::vector<double>>(part.at("Scale")) ==
                                        std::vector<double>{.5, .75, 1.25}) {
                                    child_local = std::get<std::uint64_t>(part.at("ID"));
                                    return true;
                                }
                    return false;
                });
                auto link = send("ObjectLink");
                for (const auto local : {root_local, child_local}) {
                    auto block = lludp::make_block(link.spec->blocks.at(1));
                    block["ObjectLocalID"] = local;
                    link.blocks["ObjectData"].push_back(std::move(block));
                }
                transmit(link);
            }
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ObjectUpdate")
                        for (const auto& part : packet.blocks.at("ObjectData"))
                            if (std::get<std::uint64_t>(part.at("ParentID")) == root_local &&
                                std::get<std::vector<double>>(part.at("Scale")) ==
                                    std::vector<double>{.5, .75, 1.25} &&
                                (rez ? std::get<std::uint64_t>(part.at("ID")) == child_local
                                     : argc > 8 && std::get<Uuid>(part.at("FullID")).str() == argv[8]))
                                return true;
                return false;
            });
            return 0;
        }
        scene::Terrain terrain;
        terrain.clear_land();
        const bool terrain_only = argc > 7 && std::string_view(argv[6]) == "--terrain-only";
        if (terrain_only) {
            std::ifstream input(argv[7], std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot open expected terrain heightfield");
            const std::string blob((std::istreambuf_iterator<char>(input)), {});
            terrain.load_database_blob(scene::Terrain::database_format, blob);
        }
        if (argc > 6 && (std::string_view(argv[6]) == "Rotating fixture" ||
                         std::string_view(argv[6]) == "Disabled script fixture"))
            terrain.set(107, 107, 24.f);
        std::vector<scene::TerrainPatchCoordinate> patches;
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 16; ++x)
                patches.push_back({x, y});
        auto expected = scene::terrain_layer_packets(terrain, patches);
        wait([&](const auto& list) {
            std::set<std::uint32_t> ids;
            for (auto& p : list)
                if (p.spec->name == "LayerData")
                    ids.insert(p.sequence);
            return has(list, "AgentMovementComplete") && ids.size() >= expected.size();
        });
        if (std::any_of(argv + 1, argv + argc,
                        [](const char* arg) { return std::string_view(arg) == "--stored-linkset"; })) {
            auto trigger = send("SoundTrigger");
            auto& trigger_data = trigger.block("SoundData");
            const Uuid sound("42345678-1234-4234-8234-123456789abc");
            trigger_data["SoundID"] = sound;
            trigger_data["ObjectID"] = trigger_data["OwnerID"] = trigger_data["ParentID"] = sound;
            trigger_data["Handle"] = std::uint64_t{123};
            trigger_data["Position"] = std::vector<double>{128, 128, 25};
            trigger_data["Gain"] = .375;
            const Uuid distant_sound("52345678-1234-4234-8234-123456789abc");
            auto distant = trigger;
            distant.block("SoundData")["SoundID"] = distant_sound;
            distant.block("SoundData")["Position"] = std::vector<double>{255, 255, 25};
            transmit(distant);
            trigger.sequence = sequence++;
            transmit(trigger);
            const bool parcel_blocks_sound = std::string_view(argv[6]) == "Disabled script fixture";
            if (!parcel_blocks_sound)
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "SoundTrigger") {
                            const auto& block = packet.block("SoundData");
                            if (std::get<Uuid>(block.at("SoundID")) != sound)
                                continue;
                            if (std::get<Uuid>(block.at("ObjectID")) != agent ||
                                std::get<Uuid>(block.at("OwnerID")) != agent ||
                                std::get<Uuid>(block.at("ParentID")) != agent ||
                                std::get<std::uint64_t>(block.at("Handle")) != 123 ||
                                std::get<double>(block.at("Gain")) != .375 ||
                                std::get<std::vector<double>>(block.at("Position")) !=
                                    std::vector<double>{128, 128, 25})
                                throw std::runtime_error("Viewer sound trigger identity or payload mismatch");
                            return true;
                        }
                    return false;
                });
            {
                std::lock_guard lock(mutex);
                for (const auto& packet : packets)
                    if (packet.spec->name == "SoundTrigger" &&
                        std::get<Uuid>(packet.block("SoundData").at("SoundID")) == distant_sound)
                        throw std::runtime_error(
                            "Viewer sound trigger was relayed outside its configured radius");
            }
            const Uuid root("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee");
            const Uuid child("dddddddd-dddd-4ddd-8ddd-dddddddddddd");
            if (std::string_view(argv[6]) != "Disabled script fixture") {
                const Uuid temporary("77777777-7777-4777-8777-777777777777");
                std::uint64_t temporary_local = 0;
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "ObjectUpdate")
                            for (const auto& part : packet.blocks.at("ObjectData"))
                                if (std::get<Uuid>(part.at("FullID")) == temporary) {
                                    temporary_local = std::get<std::uint64_t>(part.at("ID"));
                                    return true;
                                }
                    return false;
                });
                auto flags = send("ObjectFlagUpdate");
                flags.block("AgentData")["ObjectLocalID"] = temporary_local;
                flags.block("AgentData")["IsTemporary"] = true;
                transmit(flags);
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "ObjectUpdate")
                            for (const auto& part : packet.blocks.at("ObjectData"))
                                if (std::get<Uuid>(part.at("FullID")) == temporary &&
                                    (std::get<std::uint64_t>(part.at("UpdateFlags")) & 0x20000000u))
                                    return true;
                    return false;
                });
                const auto verify_backup = [&](const char* state) {
                    std::cout << "Temporary backup " << state << std::endl;
                    std::string result;
                    if (!std::getline(std::cin, result) || result != "OK")
                        throw std::runtime_error("Temporary prim backup transition failed");
                };
                verify_backup("removed");
                flags.sequence = sequence++;
                flags.block("AgentData")["IsTemporary"] = false;
                transmit(flags);
                verify_backup("restored");
                flags.sequence = sequence++;
                flags.block("AgentData")["IsTemporary"] = true;
                transmit(flags);
            }
            std::uint64_t child_local = 0, root_local = 0;
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ObjectUpdate")
                        for (const auto& part : packet.blocks.at("ObjectData")) {
                            const auto id = std::get<Uuid>(part.at("FullID"));
                            if (id == root)
                                root_local = std::get<std::uint64_t>(part.at("ID"));
                            if (id == child && root_local &&
                                std::get<std::uint64_t>(part.at("ParentID")) == root_local) {
                                const auto& motion =
                                    std::get<std::vector<std::uint8_t>>(part.at("ObjectData"));
                                if (motion.size() != 60 || motion[12] != 0 || motion[13] != 0 ||
                                    motion[14] != 0x80 || motion[15] != 0x3f)
                                    throw std::runtime_error(
                                        "Stored child velocity differs from the SQLite row");
                                if (std::get<std::vector<double>>(part.at("Scale")) !=
                                        std::vector<double>{1, 2, 3} ||
                                    std::get<std::int64_t>(part.at("PathTwist")) != -12 ||
                                    std::get<std::uint64_t>(part.at("ProfileHollow")) != 1500)
                                    throw std::runtime_error(
                                        "Stored child shape differs from the SQLite row");
                                child_local = std::get<std::uint64_t>(part.at("ID"));
                                const std::vector<std::uint8_t> light{1,   32, 0,   16, 0, 0,   0,  255,
                                                                      128, 64, 255, 0,  0, 160, 64, 0,
                                                                      0,   64, 63,  0,  0, 128, 63};
                                if (std::get<std::vector<std::uint8_t>>(part.at("ExtraParams")) != light)
                                    throw std::runtime_error(
                                        "Stored child light parameters differ from SQLite bytes");
                                const auto& label = std::get<std::vector<std::uint8_t>>(part.at("Text"));
                                if (label.empty() || label.back() != 0 ||
                                    std::string(label.begin(), label.end() - 1) != "Stored label" ||
                                    std::get<std::vector<std::uint8_t>>(part.at("TextColor")) !=
                                        std::vector<std::uint8_t>{16, 32, 64, 127})
                                    throw std::runtime_error(
                                        "Stored floating text or color differs from SQLite");
                                const auto& media = std::get<std::vector<std::uint8_t>>(part.at("MediaURL"));
                                if (media.empty() || media.back() != 0 ||
                                    std::string(media.begin(), media.end() - 1) !=
                                        "https://example.invalid/prim-media")
                                    throw std::runtime_error("Stored media URL differs from SQLite");
                                if (std::get<Uuid>(part.at("Sound")) !=
                                        Uuid("12345678-1234-4234-8234-123456789abc") ||
                                    std::get<Uuid>(part.at("OwnerID")) != agent ||
                                    std::get<double>(part.at("Gain")) != .5 ||
                                    std::get<std::uint64_t>(part.at("Flags")) != 1)
                                    throw std::runtime_error("Stored looped sound differs from SQLite");
                                return true;
                            }
                        }
                return false;
            });
            auto select = send("ObjectSelect");
            auto selected = lludp::make_block(select.spec->blocks.at(1));
            selected["ObjectLocalID"] = child_local;
            select.blocks["ObjectData"].push_back(std::move(selected));
            transmit(select);
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ObjectProperties")
                        for (const auto& part : packet.blocks.at("ObjectData"))
                            if (std::get<Uuid>(part.at("ObjectID")) == child &&
                                std::get<std::int64_t>(part.at("InventorySerial")) == 1) {
                                const auto& bytes = std::get<std::vector<std::uint8_t>>(part.at("Name"));
                                const std::string name(bytes.begin(),
                                                       std::find(bytes.begin(), bytes.end(), 0));
                                const bool disabled =
                                    argc > 6 && std::string_view(argv[6]) == "Disabled script fixture";
                                if (name != (disabled ? "Stored part" : "Restored script executed"))
                                    throw std::runtime_error(
                                        "Stored script startup did not match the region scripting setting");
                                return true;
                            }
                return false;
            });
            tests::LegacyInventoryProbeIo stored_io;
            stored_io.make = send;
            stored_io.send = transmit;
            stored_io.wait = [&](auto predicate) { wait(predicate); };
            stored_io.snapshot = [&] {
                std::lock_guard lock(mutex);
                return packets;
            };
            const auto inventory =
                tests::read_task_inventory_file(child, static_cast<std::uint32_t>(child_local), 1, stored_io);
            if (inventory.text.find("name\tStored note|") == std::string::npos ||
                inventory.text.find("item_id\tcccccccc-cccc-4ccc-8ccc-cccccccccccc") == std::string::npos ||
                inventory.text.find("asset_id\tbbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb") == std::string::npos)
                throw std::runtime_error("Stored task inventory differs from SQLite row");
            auto move = send("ObjectPosition");
            auto moved = lludp::make_block(move.spec->blocks.at(1));
            moved["ObjectLocalID"] = child_local;
            moved["Position"] = std::vector<double>{-12, 128, 25};
            move.blocks["ObjectData"].push_back(std::move(moved));
            transmit(move);
            auto delink = send("ObjectDelink");
            auto detached = lludp::make_block(delink.spec->blocks.at(1));
            detached["ObjectLocalID"] = child_local;
            delink.blocks["ObjectData"].push_back(std::move(detached));
            transmit(delink);
            auto stored_material = send("ObjectMaterial");
            auto stored_material_block = lludp::make_block(stored_material.spec->blocks.at(1));
            stored_material_block["ObjectLocalID"] = child_local;
            stored_material_block["Material"] = std::uint64_t{2};
            stored_material.blocks["ObjectData"].push_back(std::move(stored_material_block));
            auto stored_action = send("ObjectClickAction");
            auto stored_action_block = lludp::make_block(stored_action.spec->blocks.at(1));
            stored_action_block["ObjectLocalID"] = child_local;
            stored_action_block["ClickAction"] = std::uint64_t{2};
            stored_action.blocks["ObjectData"].push_back(std::move(stored_action_block));
            transmit(stored_action);
            transmit(stored_material);
            auto stored_flags = send("ObjectFlagUpdate");
            stored_flags.block("AgentData")["ObjectLocalID"] = child_local;
            stored_flags.block("AgentData")["IsPhantom"] = true;
            transmit(stored_flags);
            auto extra = send("ObjectExtraParams");
            auto extra_block = lludp::make_block(extra.spec->blocks.at(1));
            extra_block["ObjectLocalID"] = child_local;
            extra_block["ParamType"] = std::uint64_t{32};
            extra.blocks["ObjectData"].push_back(std::move(extra_block));
            transmit(extra);
            auto stored_shape = send("ObjectShape");
            auto stored_shape_block = lludp::make_block(stored_shape.spec->blocks.at(1));
            stored_shape_block["ObjectLocalID"] = child_local;
            stored_shape_block["PathCurve"] = std::uint64_t{16};
            stored_shape_block["ProfileCurve"] = std::uint64_t{1};
            stored_shape_block["PathScaleX"] = stored_shape_block["PathScaleY"] = std::uint64_t{100};
            stored_shape_block["PathTwist"] = std::int64_t{-8};
            stored_shape_block["ProfileHollow"] = std::uint64_t{1200};
            stored_shape.blocks["ObjectData"].push_back(std::move(stored_shape_block));
            transmit(stored_shape);
            stored_shape.sequence = sequence++;
            stored_shape.block("ObjectData")["ObjectLocalID"] = root_local;
            transmit(stored_shape);
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ObjectUpdate")
                        for (const auto& part : packet.blocks.at("ObjectData"))
                            if (std::get<Uuid>(part.at("FullID")) == child &&
                                std::get<std::uint64_t>(part.at("ParentID")) == 0 &&
                                std::get<std::uint64_t>(part.at("Material")) == 2 &&
                                std::get<std::uint64_t>(part.at("ClickAction")) == 2 &&
                                std::get<std::int64_t>(part.at("PathTwist")) == -8 &&
                                (std::get<std::uint64_t>(part.at("UpdateFlags")) & 0x400u) &&
                                std::get<std::vector<std::uint8_t>>(part.at("ExtraParams")).empty())
                                return true;
                return false;
            });
            const std::vector<std::uint8_t> light{0, 255, 255, 255, 0, 0, 0, 65, 0, 0, 0, 63, 0, 0, 128, 63};
            extra.sequence = sequence++;
            auto& light_block = extra.block("ObjectData");
            light_block["ParamInUse"] = true;
            light_block["ParamSize"] = std::uint64_t(light.size());
            light_block["ParamData"] = light;
            transmit(extra);
            std::vector<std::uint8_t> expected_light{1, 32, 0, 16, 0, 0, 0};
            expected_light.insert(expected_light.end(), light.begin(), light.end());
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ObjectUpdate")
                        for (const auto& part : packet.blocks.at("ObjectData"))
                            if (std::get<Uuid>(part.at("FullID")) == child &&
                                std::get<std::vector<std::uint8_t>>(part.at("ExtraParams")) == expected_light)
                                return true;
                return false;
            });
            if (parcel_blocks_sound) {
                std::lock_guard lock(mutex);
                for (const auto& packet : packets)
                    if (packet.spec->name == "SoundTrigger" &&
                        std::get<Uuid>(packet.block("SoundData").at("SoundID")) == sound)
                        throw std::runtime_error(
                            "Viewer sound trigger escaped a parcel that disables avatar sounds");
            }
        }
        {
            std::lock_guard lock(mutex);
            std::set<std::uint32_t> seen;
            std::size_t i = 0;
            for (auto& p : packets)
                if (p.spec->name == "LayerData" && seen.insert(p.sequence).second) {
                    if (i >= expected.size() ||
                        std::get<std::vector<std::uint8_t>>(p.block("LayerData").at("Data")) !=
                            expected[i].data ||
                        std::get<std::uint64_t>(p.block("LayerID").at("Type")) != expected[i].type)
                        throw std::runtime_error("Terrain stream content mismatch");
                    ++i;
                }
        }
        if (terrain_only) {
            if (restart_inventory) {
                const Uuid object(argv[9]);
                if (argc >= 14 && std::string_view(argv[argc - 2]) == "--linkset-data") {
                    const Uuid persisted(argv[argc - 1]);
                    std::uint64_t local = 0;
                    wait([&](const auto& list) {
                        for (const auto& packet : list)
                            if (packet.spec->name == "ObjectUpdate")
                                for (const auto& part : packet.blocks.at("ObjectData"))
                                    if (std::get<Uuid>(part.at("FullID")) == persisted &&
                                        (std::get<std::uint64_t>(part.at("UpdateFlags")) & 0x80u)) {
                                        local = std::get<std::uint64_t>(part.at("ID"));
                                        return true;
                                    }
                        return false;
                    });
                    std::uint64_t child_local = 0;
                    wait([&](const auto& list) {
                        for (const auto& packet : list)
                            if (packet.spec->name == "ObjectUpdate")
                                for (const auto& part : packet.blocks.at("ObjectData"))
                                    if (std::get<std::uint64_t>(part.at("ParentID")) == local) {
                                        child_local = std::get<std::uint64_t>(part.at("ID"));
                                        return true;
                                    }
                        return false;
                    });
                    auto child_grab = send("ObjectGrab");
                    child_grab.block("ObjectData")["LocalID"] = child_local;
                    transmit(child_grab);
                    wait([&](const auto& list) {
                        for (const auto& packet : list) {
                            if (packet.spec->name != "ChatFromSimulator")
                                continue;
                            const auto& chat = packet.block("ChatData");
                            if (std::get<Uuid>(chat.at("SourceID")) != persisted)
                                continue;
                            const auto& bytes = std::get<std::vector<std::uint8_t>>(chat.at("Message"));
                            if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                                "Touched link 2")
                                return true;
                        }
                        return false;
                    });
                    auto delink = send("ObjectDelink");
                    auto selection = lludp::make_block(delink.spec->blocks.at(1));
                    selection["ObjectLocalID"] = local;
                    delink.blocks["ObjectData"].push_back(std::move(selection));
                    transmit(delink);
                    wait([&](const auto& list) {
                        for (const auto& packet : list)
                            if (packet.spec->name == "ObjectUpdate")
                                for (const auto& part : packet.blocks.at("ObjectData"))
                                    if (std::get<std::uint64_t>(part.at("ID")) == child_local &&
                                        std::get<std::uint64_t>(part.at("ParentID")) == 0)
                                        return true;
                        return false;
                    });
                    auto grab = send("ObjectGrab");
                    grab.block("ObjectData")["LocalID"] = local;
                    transmit(grab);
                    wait([&](const auto& list) {
                        for (const auto& packet : list)
                            if (packet.spec->name == "ChatFromSimulator") {
                                const auto& chat = packet.block("ChatData");
                                if (std::get<Uuid>(chat.at("SourceID")) != persisted)
                                    continue;
                                const auto& bytes = std::get<std::vector<std::uint8_t>>(chat.at("Message"));
                                if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                                    "Restart linkset stored-start:value/yes/x")
                                    return true;
                            }
                        return false;
                    });
                    const Uuid destination_root(argv[argc - 3]);
                    std::uint64_t destination_local = 0;
                    wait([&](const auto& list) {
                        for (const auto& packet : list)
                            if (packet.spec->name == "ObjectUpdate")
                                for (const auto& part : packet.blocks.at("ObjectData"))
                                    if (std::get<Uuid>(part.at("FullID")) == destination_root) {
                                        destination_local = std::get<std::uint64_t>(part.at("ID"));
                                        return true;
                                    }
                        return false;
                    });
                    auto link = send("ObjectLink");
                    for (auto id : {destination_local, local}) {
                        auto part = lludp::make_block(link.spec->blocks.at(1));
                        part["ObjectLocalID"] = id;
                        link.blocks["ObjectData"].push_back(std::move(part));
                    }
                    transmit(link);
                    wait([&](const auto& list) {
                        for (const auto& packet : list)
                            if (packet.spec->name == "ObjectUpdate")
                                for (const auto& part : packet.blocks.at("ObjectData"))
                                    if (std::get<Uuid>(part.at("FullID")) == persisted &&
                                        std::get<std::uint64_t>(part.at("ParentID")) == destination_local)
                                        return true;
                        return false;
                    });
                    grab.sequence = sequence++;
                    transmit(grab);
                    wait([&](const auto& list) {
                        for (const auto& packet : list)
                            if (packet.spec->name == "ChatFromSimulator") {
                                const auto& chat = packet.block("ChatData");
                                if (std::get<Uuid>(chat.at("SourceID")) != persisted)
                                    continue;
                                const auto& bytes = std::get<std::vector<std::uint8_t>>(chat.at("Message"));
                                if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                                    "Restart linkset :target/yes/")
                                    return true;
                            }
                        return false;
                    });
                }
                const Uuid phantom(argv[10]);
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "ObjectUpdate")
                            for (const auto& part : packet.blocks.at("ObjectData"))
                                if (std::get<Uuid>(part.at("FullID")) == phantom) {
                                    if (!(std::get<std::uint64_t>(part.at("UpdateFlags")) & 0x400u))
                                        throw std::runtime_error("Restored prim lost its saved phantom flag");
                                    return true;
                                }
                    return false;
                });
                std::uint32_t local = 0;
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "ObjectUpdate")
                            for (const auto& part : packet.blocks.at("ObjectData"))
                                if (std::get<Uuid>(part.at("FullID")) == object) {
                                    const auto& bytes =
                                        std::get<std::vector<std::uint8_t>>(part.at("TextureEntry"));
                                    std::string hex;
                                    constexpr std::string_view digits = "0123456789abcdef";
                                    for (const auto byte : bytes) {
                                        hex += digits[byte >> 4];
                                        hex += digits[byte & 15];
                                    }
                                    if (hex != argv[11])
                                        throw std::runtime_error(
                                            "Restored texture entry differs from SQLite bytes");
                                    if (!std::get<Uuid>(part.at("Sound")).is_zero() ||
                                        std::get<double>(part.at("Gain")) != 0. ||
                                        std::get<std::uint64_t>(part.at("Flags")) != 0)
                                        throw std::runtime_error("Restored prim replayed its stopped sound");
                                    const std::vector<std::uint8_t> label{'S', 'c', 'r', 'i', 'p', 't', ' ',
                                                                          'l', 'a', 'b', 'e', 'l', 0};
                                    if (std::get<std::vector<std::uint8_t>>(part.at("Text")) != label ||
                                        std::get<std::vector<std::uint8_t>>(part.at("TextColor")) !=
                                            std::vector<std::uint8_t>{255, 127, 0, 128})
                                        throw std::runtime_error(
                                            "Restored prim lost its scripted floating text or color");
                                    local =
                                        static_cast<std::uint32_t>(std::get<std::uint64_t>(part.at("ID")));
                                    return true;
                                }
                    return false;
                });
                tests::LegacyInventoryProbeIo io;
                io.make = send;
                io.send = transmit;
                io.wait = [&](auto predicate) { wait(predicate); };
                io.snapshot = [&] {
                    std::lock_guard lock(mutex);
                    return packets;
                };
                auto select = send("ObjectSelect");
                auto selected = lludp::make_block(select.spec->blocks.at(1));
                selected["ObjectLocalID"] = std::uint64_t(local);
                select.blocks["ObjectData"].push_back(std::move(selected));
                transmit(select);
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "ObjectProperties")
                            for (const auto& part : packet.blocks.at("ObjectData"))
                                if (std::get<Uuid>(part.at("ObjectID")) == object) {
                                    const auto label = [&](const char* field) {
                                        const auto& bytes =
                                            std::get<std::vector<std::uint8_t>>(part.at(field));
                                        return std::string(bytes.begin(),
                                                           std::find(bytes.begin(), bytes.end(), 0));
                                    };
                                    if (label("TouchName") != "Activate " || label("SitName") != "Take seat")
                                        throw std::runtime_error(
                                            "Restored prim lost its scripted interaction labels");
                                    return true;
                                }
                    return false;
                });
                if (argc == 12)
                    tests::check_empty_task_inventory(object, local, io);
                else {
                    const auto inventory = tests::read_task_inventory_file(object, local, 1, io);
                    for (int i = 12; i < argc; i += 2) {
                        if (std::string_view(argv[i]) == "--linkset-data" ||
                            std::string_view(argv[i]) == "--linkset-target")
                            continue;
                        const auto item = inventory.text.find(std::string("item_id\t") + argv[i]);
                        const auto end =
                            item == inventory.text.npos ? item : inventory.text.find("inv_item\t", item);
                        const auto asset =
                            item == inventory.text.npos
                                ? item
                                : inventory.text.find(std::string("asset_id\t") + argv[i + 1], item);
                        if (item == inventory.text.npos || asset == inventory.text.npos || asset >= end)
                            throw std::runtime_error(
                                "Restored task inventory differs from saved item and asset IDs");
                    }
                }
            }
            if (argc > 8 && std::string_view(argv[8]) == "--bake") {
                auto request = send("EstateOwnerMessage");
                request.block("MethodData")["Method"] =
                    std::vector<std::uint8_t>{'t', 'e', 'r', 'r', 'a', 'i', 'n', 0};
                auto parameter = lludp::make_block(request.spec->blocks[2]);
                parameter["Parameter"] = std::vector<std::uint8_t>{'b', 'a', 'k', 'e', 0};
                request.blocks["ParamList"].push_back(std::move(parameter));
                transmit(request);
                request.sequence = sequence++;
                auto& parameters = request.blocks.at("ParamList");
                const std::string download = "download filename", destination = "terrain.raw";
                parameters.front()["Parameter"] = std::vector<std::uint8_t>(download.begin(), download.end());
                auto name = lludp::make_block(request.spec->blocks[2]);
                name["Parameter"] = std::vector<std::uint8_t>(destination.begin(), destination.end());
                parameters.push_back(std::move(name));
                transmit(request);
                wait([&](const auto& list) { return has(list, "InitiateDownload"); });
                auto transfer = send("RequestXfer");
                auto& fields = transfer.block("XferID");
                const std::uint64_t id = 987654321;
                fields["ID"] = id;
                {
                    std::lock_guard lock(mutex);
                    const auto reply = std::find_if(packets.begin(), packets.end(), [](const auto& p) {
                        return p.spec->name == "InitiateDownload";
                    });
                    const auto& files = reply->block("FileData");
                    auto expected_name = std::vector<std::uint8_t>(destination.begin(), destination.end());
                    expected_name.push_back(0);
                    if (std::get<std::vector<std::uint8_t>>(files.at("ViewerFilename")) != expected_name)
                        throw std::runtime_error("Terrain download filename mismatch");
                    fields["Filename"] = files.at("SimFilename");
                }
                auto throttle = send("AgentThrottle");
                throttle.block("AgentData")["CircuitCode"] = std::uint64_t(code);
                std::vector<std::uint8_t> rates;
                for (float rate : {50000.f, 80000.f, 10000.f, 10000.f, 4000000.f, 100000.f, 80000.f}) {
                    const auto bits = std::bit_cast<std::uint32_t>(rate);
                    for (int shift = 0; shift < 32; shift += 8)
                        rates.push_back(static_cast<std::uint8_t>(bits >> shift));
                }
                throttle.block("Throttle")["Throttles"] = std::move(rates);
                transmit(throttle);
                transmit(transfer);
                constexpr std::size_t raw_size = 256 * 256 * 13, chunk_size = 1024;
                std::vector<std::uint8_t> raw(raw_size);
                std::size_t cursor = 0, next_chunk = 0;
                wait([&](const auto& list) {
                    while (cursor < list.size()) {
                        const auto& packet = list[cursor++];
                        if (packet.spec->name != "SendXferPacket")
                            continue;
                        const auto& header = packet.block("XferID");
                        if (std::get<std::uint64_t>(header.at("ID")) != id)
                            continue;
                        const auto number = std::get<std::uint64_t>(header.at("Packet"));
                        const auto index = number & 0x7fffffffu;
                        if (index < next_chunk)
                            continue;
                        if (index != next_chunk)
                            throw std::runtime_error("Unexpected terrain download packet sequence");
                        const bool last = (index + 1) * chunk_size == raw_size;
                        if (bool(number & 0x80000000u) != last)
                            throw std::runtime_error("Terrain download end marker mismatch");
                        const auto& data =
                            std::get<std::vector<std::uint8_t>>(packet.block("DataPacket").at("Data"));
                        const auto prefix = index == 0 ? 4u : 0u;
                        if (data.size() != chunk_size + prefix ||
                            (prefix && (data[0] != 0 || data[1] != 0 || data[2] != 13 || data[3] != 0)))
                            throw std::runtime_error("Terrain download size mismatch");
                        std::copy(data.begin() + prefix, data.end(), raw.begin() + index * chunk_size);
                        auto ack = send("ConfirmXferPacket");
                        ack.block("XferID")["ID"] = id;
                        ack.block("XferID")["Packet"] = number;
                        transmit(ack);
                        ++next_chunk;
                        if (last) {
                            ack.sequence = sequence++;
                            transmit(ack);
                            return true;
                        }
                    }
                    return false;
                });
                const auto heights = terrain.serialized_floats();
                std::array<int, 65026> representable{};
                for (int a = 0; a < 256; ++a)
                    for (int b = 0; b < 256; ++b)
                        representable[a * b] = a * b;
                for (std::size_t i = 1; i < representable.size(); ++i)
                    representable[i] = std::max(representable[i], representable[i - 1]);
                for (std::size_t row = 0; row < 256; ++row)
                    for (std::size_t x = 0; x < 256; ++x) {
                        const auto* sample = raw.data() + (row * 256 + x) * 13;
                        const auto scaled = std::clamp(heights[(255 - row) * 256 + x] * 128.f, 0.f, 65025.f);
                        if (sample[0] * sample[1] != representable[static_cast<std::size_t>(scaled)] ||
                            sample[2] != 20 || sample[3] || sample[4] || sample[5] || sample[6] ||
                            sample[7] != 255 || sample[8] != 255 || sample[9] != 255 || sample[10] != 255 ||
                            sample[11] != sample[0] || sample[12] != sample[1])
                            throw std::runtime_error("Terrain download LLRAW sample mismatch");
                    }
                std::uint64_t upload_id = 0;
                for (const std::string extension : {".abort", ".f32", ".png", ".r32"}) {
                    auto upload = send("EstateOwnerMessage");
                    upload.block("MethodData")["Method"] =
                        std::vector<std::uint8_t>{'t', 'e', 'r', 'r', 'a', 'i', 'n', 0};
                    for (const auto& value : {std::string("upload filename"), "terrain" + extension}) {
                        auto parameter = lludp::make_block(upload.spec->blocks[2]);
                        parameter["Parameter"] = std::vector<std::uint8_t>(value.begin(), value.end());
                        upload.blocks["ParamList"].push_back(std::move(parameter));
                    }
                    transmit(upload);
                    wait([&](const auto& list) {
                        return std::any_of(list.rbegin(), list.rend(), [&](const auto& p) {
                            return p.spec->name == "RequestXfer" &&
                                   std::get<std::uint64_t>(p.block("XferID").at("ID")) > upload_id;
                        });
                    });
                    {
                        std::lock_guard lock(mutex);
                        const auto offer = std::find_if(packets.rbegin(), packets.rend(), [](const auto& p) {
                            return p.spec->name == "RequestXfer";
                        });
                        upload_id = std::get<std::uint64_t>(offer->block("XferID").at("ID"));
                    }
                    if (extension == ".abort") {
                        auto abort = send("AbortXfer");
                        abort.block("XferID")["ID"] = upload_id;
                        transmit(abort);
                        wait([&](const auto& list) {
                            return std::any_of(list.begin(), list.end(), [](const auto& p) {
                                if (p.spec->name != "AlertMessage")
                                    return false;
                                const auto& bytes =
                                    std::get<std::vector<std::uint8_t>>(p.block("AlertData").at("Message"));
                                return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                                        bytes.size())
                                    .starts_with("Terrain Upload aborted by the client");
                            });
                        });
                        continue;
                    }
                    // 16x16 RGB16 PNG with nonzero low bytes in the bottom row.
                    std::vector<std::uint8_t> encoded{
                        92,  0,   0,   0,   137, 80, 78,  71,  13,  10,  26, 10, 0,   0,   0,   13,
                        73,  72,  68,  82,  0,   0,  0,   16,  0,   0,   0,  16, 16,  2,   0,   0,
                        0,   192, 1,   180, 117, 0,  0,   0,   35,  73,  68, 65, 84,  120, 156, 99,
                        96,  24,  5,   163, 96,  20, 140, 130, 81,  192, 48, 4,  128, 144, 73,  88,
                        197, 172, 61,  13,  13,  13, 255, 27,  104, 98,  1,  0,  236, 172, 5,   106,
                        120, 136, 101, 14,  0,   0,  0,   0,   73,  69,  78, 68, 174, 66,  96,  130};
                    if (extension == ".f32")
                        encoded = {3, 0, 0, 0, 0, 0, 0};
                    if (extension == ".r32") {
                        encoded.clear();
                        const auto append = [&](std::uint32_t value) {
                            for (unsigned shift = 0; shift < 32; shift += 8)
                                encoded.push_back(static_cast<std::uint8_t>(value >> shift));
                        };
                        append(static_cast<std::uint32_t>(heights.size() * 4));
                        for (std::uint32_t value : {0x7fc00000u, 0x7f800000u, 0xff800000u, 0x42280000u})
                            append(value);
                        append(std::bit_cast<std::uint32_t>(4095.f));
                        for (std::size_t i = 5; i < heights.size(); ++i)
                            append(std::bit_cast<std::uint32_t>(heights[i]));
                    }
                    for (std::size_t offset = 0, index = 0; offset < encoded.size(); ++index) {
                        const auto end = std::min(encoded.size(), offset + 1024 + (index == 0 ? 4 : 0));
                        const std::uint64_t number = index | (end == encoded.size() ? 0x80000000u : 0);
                        auto chunk = send("SendXferPacket");
                        chunk.block("XferID")["ID"] = upload_id;
                        chunk.block("XferID")["Packet"] = number;
                        chunk.block("DataPacket")["Data"] =
                            std::vector<std::uint8_t>(encoded.begin() + offset, encoded.begin() + end);
                        const auto confirmed = [&](const auto& list) {
                            return std::count_if(list.begin(), list.end(), [&](const auto& p) {
                                if (p.spec->name != "ConfirmXferPacket")
                                    return false;
                                const auto& fields = p.block("XferID");
                                return std::get<std::uint64_t>(fields.at("ID")) == upload_id &&
                                       std::get<std::uint64_t>(fields.at("Packet")) == number;
                            });
                        };
                        transmit(chunk);
                        wait([&](const auto& list) { return confirmed(list) >= 1; });
                        if (end == encoded.size()) {
                            chunk.sequence = sequence++;
                            transmit(chunk);
                            wait([&](const auto& list) { return confirmed(list) >= 2; });
                        }
                        offset = end;
                    }
                    wait([&](const auto& list) {
                        return std::any_of(list.begin(), list.end(), [&](const auto& p) {
                            if (p.spec->name != "AlertMessage")
                                return false;
                            const auto& bytes =
                                std::get<std::vector<std::uint8_t>>(p.block("AlertData").at("Message"));
                            const std::string message(bytes.begin(), bytes.end());
                            if (extension == ".f32")
                                return message.starts_with("There was an IO Exception loading your terrain.");
                            return message.starts_with("Your terrain was loaded as a " + extension + " file");
                        });
                    });
                }
                request.sequence = sequence++;
                transmit(request);
                wait([&](const auto& list) {
                    return std::any_of(list.begin(), list.end(), [](const auto& p) {
                        if (p.spec->name != "AlertMessage")
                            return false;
                        const auto& bytes =
                            std::get<std::vector<std::uint8_t>>(p.block("AlertData").at("Message"));
                        return std::string(bytes.begin(), bytes.end()) == std::string("Terrain error\0", 14);
                    });
                });
                {
                    std::lock_guard lock(mutex);
                    if (std::count_if(packets.begin(), packets.end(),
                                      [](const auto& p) { return p.spec->name == "InitiateDownload"; }) != 1)
                        throw std::runtime_error("Over-limit terrain produced a download offer");
                }
            }
            transmit(send("LogoutRequest"));
            wait([&](const auto& list) { return has(list, "LogoutReply"); });
            socket.stop();
            return 0;
        }
        wait([&](const auto& list) {
            for (auto& p : list)
                if (p.spec->name == "ObjectUpdate")
                    for (auto& b : p.blocks.at("ObjectData"))
                        if (std::get<Uuid>(b.at("FullID")) == agent &&
                            std::get<std::uint64_t>(b.at("PCode")) == 47)
                            return true;
            return false;
        });
        std::uint32_t avatar_local = 0;
        {
            std::lock_guard lock(mutex);
            for (auto& p : packets)
                if (p.spec->name == "ObjectUpdate")
                    for (auto& b : p.blocks.at("ObjectData"))
                        if (std::get<Uuid>(b.at("FullID")) == agent &&
                            std::get<std::uint64_t>(b.at("PCode")) == 47) {
                            avatar_local = static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("ID")));
                            break;
                        }
        }
        if (avatar_local < 720001)
            throw std::runtime_error("Missing shared avatar local ID");
        if (!profiles_disabled) {
            auto request = send("AvatarPropertiesRequest");
            request.block("AgentData")["AvatarID"] = agent;
            transmit(request);
            wait([&](const auto& list) { return profile_matches(list, agent); });
            std::cout << "PASS: account avatar properties/interests" << std::endl;
        }
        if (advanced_profiles) {
            auto request = send("AvatarPropertiesRequest");
            request.block("AgentData")["AvatarID"] = agent;
            transmit(request);
            wait([&](const auto& list) {
                for (const auto& reply : list) {
                    if (reply.spec->name != "AvatarPropertiesReply")
                        continue;
                    const auto& ids = reply.block("AgentData");
                    if (std::get<Uuid>(ids.at("AvatarID")) != agent)
                        continue;
                    const auto& about =
                        std::get<std::vector<std::uint8_t>>(reply.block("PropertiesData").at("AboutText"));
                    const std::string expected = "Stored native profile";
                    if (std::get<Uuid>(ids.at("AgentID")) != agent ||
                        std::get<Uuid>(ids.at("AvatarID")) != agent || about.size() != expected.size() + 1 ||
                        about.back() != 0 || !std::equal(expected.begin(), expected.end(), about.begin()))
                        throw std::runtime_error("Background profile properties differ from SQLite data");
                    return true;
                }
                return false;
            });
            std::cout << "PASS: advanced profile properties from worker" << std::endl;
        }
        const auto wearables_requested = std::chrono::steady_clock::now();
        transmit(send("AgentWearablesRequest"));
        wait([&](const auto& list) {
            return has(list, "AgentWearablesUpdate") && has(list, "AvatarAppearance");
        });
        if (std::chrono::steady_clock::now() - wearables_requested < std::chrono::seconds(4))
            throw std::runtime_error("Wearables reply arrived before the AvatarFactory delay");
        if (argc > 6 && std::string_view(argv[6]) == "Rotating fixture") {
            auto resized = send("AgentSetAppearance");
            resized.block("AgentData")["Size"] = std::vector<double>{.5, .625, 2.125};
            resized.blocks["VisualParam"].push_back({{"ParamValue", std::uint64_t{17}}});
            transmit(resized);
        }
        auto cache_request = send("AgentCachedTexture");
        const bool verify_wearables = std::any_of(argv + 1, argv + argc, [](const char* option) {
            return std::string_view(option) == "--verify-default-object-permissions";
        });
        if (verify_wearables) {
            auto wearing = send("AgentIsNowWearing");
            wearing.blocks["WearableData"].push_back(
                {{"ItemID", AvatarWearable::default_body_item}, {"WearableType", std::uint64_t{0}}});
            wearing.blocks["WearableData"].push_back(
                {{"ItemID", Uuid::random()}, {"WearableType", std::uint64_t{4}}});
            transmit(wearing);
            cache_request.sequence = sequence++;
        }
        cache_request.block("AgentData")["SerialNum"] = std::int64_t{271};
        cache_request.blocks["WearableData"].push_back(
            {{"ID", Uuid::random()}, {"TextureIndex", std::uint64_t{255}}});
        transmit(cache_request);
        wait([&](const auto& list) {
            for (const auto& packet : list)
                if (packet.spec->name == "AgentCachedTextureResponse") {
                    const auto& identity = packet.block("AgentData");
                    const auto& row = packet.block("WearableData");
                    return packet.zerocoded && std::get<std::int64_t>(identity.at("SerialNum")) == 271 &&
                           std::get<Uuid>(identity.at("AgentID")) == agent &&
                           std::get<Uuid>(identity.at("SessionID")) == session &&
                           std::get<std::uint64_t>(row.at("TextureIndex")) == 255 &&
                           std::get<Uuid>(row.at("TextureID")).is_zero() &&
                           std::get<std::vector<std::uint8_t>>(row.at("HostName")).empty();
                }
            return false;
        });
        if (verify_wearables) {
            // The reply reads live wearables. Allow the five-second save to resolve
            // inventory before the four-second reply deadline.
            std::this_thread::sleep_for(std::chrono::milliseconds(1300));
            std::size_t start;
            {
                std::lock_guard lock(mutex);
                start = packets.size();
            }
            transmit(send("AgentWearablesRequest"));
            wait([&](const auto& list) {
                for (auto i = start; i < list.size(); ++i)
                    if (list[i].spec->name == "AgentWearablesUpdate") {
                        const auto& rows = list[i].blocks.at("WearableData");
                        return rows.size() == 1 &&
                               std::get<Uuid>(rows.front().at("ItemID")) ==
                                   AvatarWearable::default_body_item &&
                               std::get<Uuid>(rows.front().at("AssetID")) ==
                                   AvatarWearable::default_body_asset;
                    }
                return false;
            });
        }
        const Uuid bake_cache_id("890172ac-bab4-4b53-b553-7c0775014fee");
        for (int i = 1; i < argc; ++i) {
            constexpr std::string_view prefix = "--restored-bake=";
            const std::string_view argument = argv[i];
            if (!argument.starts_with(prefix))
                continue;
            const Uuid expected(std::string(argument.substr(prefix.size())));
            for (int attempt = 0; attempt < 10; ++attempt) {
                auto query = send("AgentCachedTexture");
                query.block("AgentData")["SerialNum"] = std::int64_t{274};
                query.blocks["WearableData"].push_back(
                    {{"ID", bake_cache_id}, {"TextureIndex", std::uint64_t{40}}});
                transmit(query);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            wait([&](const auto& list) {
                return std::any_of(list.begin(), list.end(), [&](const auto& packet) {
                    return packet.spec->name == "AgentCachedTextureResponse" &&
                           std::get<std::int64_t>(packet.block("AgentData").at("SerialNum")) == 274 &&
                           std::get<Uuid>(packet.block("WearableData").at("TextureID")) == expected;
                });
            });
        }
        if (std::any_of(argv + 1, argv + argc,
                        [](const char* arg) { return std::string_view(arg) == "--verify-baked-cache"; })) {
            std::cout << "Baked cache fixture" << std::endl;
            std::string id;
            if (!std::getline(std::cin >> std::ws, id))
                throw std::runtime_error("Missing baked texture fixture");
            const Uuid texture(id), cache_id = bake_cache_id;
            const auto superseded_cache_id = Uuid::random();
            framework::TextureEntry entry(framework::Uuid("c228d1cf-4b5d-4ba8-84f4-899a0796aa97"));
            entry.get_face(8)->texture_id(texture);
            entry.get_face(9)->texture_id(texture);
            entry.get_face(40)->texture_id(texture);
            auto appearance = send("AgentSetAppearance");
            appearance.block("AgentData")["Size"] = std::vector<double>{.45, .6, 1.9};
            appearance.block("ObjectData")["TextureEntry"] = entry.bytes();
            appearance.blocks["WearableData"].push_back(
                {{"CacheID", superseded_cache_id}, {"TextureIndex", std::uint64_t{9}}});
            appearance.blocks["WearableData"].push_back(
                {{"CacheID", cache_id}, {"TextureIndex", std::uint64_t{8}}});
            appearance.blocks["WearableData"].push_back(
                {{"CacheID", cache_id}, {"TextureIndex", std::uint64_t{9}}});
            appearance.blocks["WearableData"].push_back(
                {{"CacheID", cache_id}, {"TextureIndex", std::uint64_t{40}}});
            appearance.blocks["VisualParam"].push_back({{"ParamValue", std::uint64_t{17}}});
            transmit(appearance);
            auto query = send("AgentCachedTexture");
            query.block("AgentData")["SerialNum"] = std::int64_t{272};
            query.blocks["WearableData"].push_back({{"ID", cache_id}, {"TextureIndex", std::uint64_t{8}}});
            query.blocks["WearableData"].push_back({{"ID", cache_id}, {"TextureIndex", std::uint64_t{40}}});
            query.blocks["WearableData"].push_back(
                {{"ID", superseded_cache_id}, {"TextureIndex", std::uint64_t{9}}});
            query.blocks["WearableData"].push_back({{"ID", cache_id}, {"TextureIndex", std::uint64_t{9}}});
            transmit(query);
            wait([&](const auto& list) {
                return std::any_of(list.begin(), list.end(), [&](const auto& packet) {
                    return packet.spec->name == "AgentCachedTextureResponse" &&
                           std::get<std::int64_t>(packet.block("AgentData").at("SerialNum")) == 272 &&
                           std::get<Uuid>(packet.block("WearableData").at("TextureID")) == texture &&
                           std::get<Uuid>(packet.blocks.at("WearableData").at(1).at("TextureID")) ==
                               texture &&
                           std::get<Uuid>(packet.blocks.at("WearableData").at(2).at("TextureID")).is_zero() &&
                           std::get<Uuid>(packet.blocks.at("WearableData").at(3).at("TextureID")) == texture;
                });
            });
            const auto missing = Uuid::random();
            entry.get_face(8)->texture_id(missing);
            appearance.blocks["WearableData"].pop_back();
            appearance.block("ObjectData")["TextureEntry"] = entry.bytes();
            appearance.sequence = sequence++;
            transmit(appearance);
            appearance.sequence = sequence++;
            transmit(appearance);
            const auto rebake_count = [&](const auto& list) {
                return std::count_if(list.begin(), list.end(), [&](const auto& packet) {
                    return packet.spec->name == "RebakeAvatarTextures" &&
                           std::get<Uuid>(packet.block("TextureData").at("TextureID")) == missing;
                });
            };
            wait([&](const auto& list) { return rebake_count(list) > 0; });
            auto omitted = send("AgentCachedTexture");
            omitted.block("AgentData")["SerialNum"] = std::int64_t{273};
            omitted.blocks["WearableData"].push_back({{"ID", cache_id}, {"TextureIndex", std::uint64_t{40}}});
            transmit(omitted);
            wait([](const auto& list) {
                return std::any_of(list.begin(), list.end(), [](const auto& packet) {
                    return packet.spec->name == "AgentCachedTextureResponse" &&
                           std::get<std::int64_t>(packet.block("AgentData").at("SerialNum")) == 273 &&
                           std::get<Uuid>(packet.block("WearableData").at("TextureID")).is_zero();
                });
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            {
                std::lock_guard lock(mutex);
                if (rebake_count(packets) != 1)
                    throw std::runtime_error("Repeated missing bake was not throttled");
            }
        }
        for (int i = 1; i < argc; ++i) {
            constexpr std::string_view prefix = "--appearance-sender=";
            const std::string_view argument = argv[i];
            if (!argument.starts_with(prefix))
                continue;
            const Uuid sender(std::string(argument.substr(prefix.size())));
            const bool verify_hover = std::any_of(argv + 1, argv + argc, [](const char* option) {
                return std::string_view(option) == "--verify-appearance-hover";
            });
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "AvatarAppearance" ||
                        std::get<Uuid>(packet.block("Sender").at("ID")) != sender)
                        continue;
                    if (verify_hover &&
                        std::get<std::vector<double>>(packet.block("AppearanceHover").at("HoverHeight")) !=
                            std::vector<double>{0., 0., .125})
                        continue;
                    const auto params = packet.blocks.find("VisualParam");
                    if (params == packet.blocks.end() || params->second.size() != 3)
                        continue;
                    if (std::get<std::uint64_t>(params->second[0].at("ParamValue")) == 17 &&
                        std::get<std::uint64_t>(params->second[1].at("ParamValue")) == 103 &&
                        std::get<std::uint64_t>(params->second[2].at("ParamValue")) == 239)
                        return true;
                }
                return false;
            });
        }
        auto add = send("ObjectAdd");
        auto& shape = add.block("ObjectData");
        shape["PCode"] = std::uint64_t(9);
        shape["PathCurve"] = std::uint64_t(16);
        shape["ProfileCurve"] = std::uint64_t(1);
        shape["PathScaleX"] = shape["PathScaleY"] = std::uint64_t(100);
        shape["Scale"] = std::vector<double>{.5, .5, .5};
        shape["Rotation"] = std::vector<double>{0, 0, 0, 1};
        shape["RayStart"] = std::vector<double>{128, 128, 70};
        shape["RayEnd"] = std::vector<double>{128, 128, 21};
        shape["RayEndIsIntersection"] = std::uint64_t(1);
        shape["AddFlags"] = std::uint64_t(2);
        transmit(add);
        auto primitive = [&](const auto& p) {
            return p.spec->name == "ObjectUpdate" && !p.blocks.at("ObjectData").empty() &&
                   std::get<std::uint64_t>(p.block("ObjectData").at("PCode")) == 9 &&
                   std::get<std::uint64_t>(p.block("ObjectData").at("ID")) == avatar_local + 1;
        };
        wait([&](const auto& list) { return std::any_of(list.begin(), list.end(), primitive); });
        auto real = [](const auto& bytes, std::size_t offset) {
            std::uint32_t bits = 0;
            for (int i = 0; i < 4; ++i)
                bits |= std::uint32_t(bytes.at(offset + i)) << (8 * i);
            return std::bit_cast<float>(bits);
        };
        std::uint32_t local = 0;
        Uuid object_id;
        {
            std::lock_guard lock(mutex);
            const auto handshake = std::find_if(packets.begin(), packets.end(), [](const auto& packet) {
                return packet.spec->name == "RegionHandshake";
            });
            if (argc > 6 && std::string_view(argv[6]) == "Rotating fixture" &&
                std::get<std::uint64_t>(handshake->block("RegionInfo").at("SimAccess")) != 21)
                throw std::runtime_error("Region handshake did not advertise the saved maturity");
            const auto water = std::get<double>(handshake->block("RegionInfo").at("WaterHeight"));
            const auto expected_z = water > 21 && water < 70 ? std::max(21.25, water) : 21.25;
            for (auto& p : packets)
                if (primitive(p)) {
                    auto& b = p.block("ObjectData");
                    local = static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("ID")));
                    object_id = std::get<Uuid>(b.at("FullID"));
                    auto& data = std::get<std::vector<std::uint8_t>>(b.at("ObjectData"));
                    if (local != avatar_local + 1 || real(data, 8) != static_cast<float>(expected_z) ||
                        (std::get<std::uint64_t>(b.at("UpdateFlags")) & 2) == 0 ||
                        std::get<std::vector<double>>(b.at("Scale")) != std::vector<double>{.5, .5, .5})
                        throw std::runtime_error(
                            "ObjectAdd did not preserve source rez/shape/local ID/selection");
                    break;
                }
        }
        auto move = send("ObjectPosition");
        auto block = lludp::make_block(move.spec->blocks.at(1));
        block["ObjectLocalID"] = std::uint64_t(local);
        block["Position"] = std::vector<double>{130, 129, 22};
        move.blocks["ObjectData"].push_back(std::move(block));
        transmit(move);
        std::cout << "Primary viewer prim " << object_id.str() << std::endl;
        wait([&](const auto& list) {
            for (auto& p : list)
                if (primitive(p)) {
                    auto& bytes = std::get<std::vector<std::uint8_t>>(p.block("ObjectData").at("ObjectData"));
                    if (real(bytes, 0) == 130 && real(bytes, 4) == 129 && real(bytes, 8) == 22)
                        return true;
                }
            return false;
        });
        // Derive the actual spawn height from the server's movement completion.
        auto action = send("ObjectClickAction");
        auto action_block = lludp::make_block(action.spec->blocks.at(1));
        action_block["ObjectLocalID"] = std::uint64_t(local);
        action_block["ClickAction"] = std::uint64_t(2);
        action.blocks["ObjectData"] = {std::move(action_block)};
        transmit(action);
        auto shape_edit = send("ObjectShape");
        auto shape_block = lludp::make_block(shape_edit.spec->blocks.at(1));
        shape_block["ObjectLocalID"] = std::uint64_t(local);
        shape_block["PathCurve"] = std::uint64_t(16);
        shape_block["ProfileCurve"] = std::uint64_t(1);
        shape_block["PathScaleX"] = shape_block["PathScaleY"] = std::uint64_t(100);
        shape_block["PathTwist"] = std::int64_t(-12);
        shape_block["ProfileHollow"] = std::uint64_t(1500);
        shape_edit.blocks["ObjectData"] = {std::move(shape_block)};
        transmit(shape_edit);
        wait([&](const auto& list) {
            for (const auto& packet : list) {
                if (!primitive(packet))
                    continue;
                const auto& fields = packet.block("ObjectData");
                if (std::get<Uuid>(fields.at("FullID")) == object_id &&
                    std::get<std::int64_t>(fields.at("PathTwist")) == -12 &&
                    std::get<std::uint64_t>(fields.at("ProfileHollow")) == 1500)
                    return true;
            }
            return false;
        });
        auto stale_image = send("ObjectImage");
        auto image = send("ObjectImage");
        auto image_block = lludp::make_block(image.spec->blocks.at(1));
        image_block["ObjectLocalID"] = std::uint64_t(local);
        const auto texture = framework::TextureEntry(framework::TextureEntry::white_texture).bytes();
        image_block["TextureEntry"] = texture;
        image.blocks["ObjectData"] = {std::move(image_block)};
        stale_image.blocks["ObjectData"] = image.blocks["ObjectData"];
        stale_image.block("ObjectData")["TextureEntry"] =
            framework::TextureEntry(Uuid("89556747-24cb-43ed-920b-47caed15465f")).bytes();
        transmit(image);
        wait([&](const auto& list) {
            for (const auto& packet : list) {
                if (!primitive(packet))
                    continue;
                const auto& fields = packet.block("ObjectData");
                if (std::get<Uuid>(fields.at("FullID")) == object_id &&
                    std::get<std::vector<std::uint8_t>>(fields.at("TextureEntry")) == texture)
                    return true;
            }
            return false;
        });
        transmit(stale_image);
        auto physics = send("ObjectFlagUpdate");
        physics.block("AgentData")["ObjectLocalID"] = std::uint64_t(local);
        auto properties = lludp::make_block(physics.spec->blocks.at(1));
        properties["PhysicsShapeType"] = std::uint64_t(2);
        properties["Density"] = 1234.;
        properties["Friction"] = .25;
        properties["Restitution"] = .75;
        properties["GravityMultiplier"] = -.5;
        physics.blocks["ExtraPhysics"] = {std::move(properties)};
        transmit(physics);
        auto material = send("ObjectMaterial");
        auto material_block = lludp::make_block(material.spec->blocks.at(1));
        material_block["ObjectLocalID"] = std::uint64_t(local);
        material_block["Material"] = std::uint64_t(2);
        material.blocks["ObjectData"] = {std::move(material_block)};
        transmit(material);
        wait([&](const auto& list) {
            for (const auto& packet : list)
                if (primitive(packet) &&
                    std::get<std::uint64_t>(packet.block("ObjectData").at("Material")) == 2) {
                    if (std::get<std::vector<std::uint8_t>>(packet.block("ObjectData").at("TextureEntry")) !=
                        texture)
                        throw std::runtime_error("Stale texture edit overwrote a newer texture");
                    return true;
                }
            return false;
        });
        if (argc > 10) {
            std::cout << "Prim material query " << local << std::endl;
            std::string result;
            if (!std::getline(std::cin, result) || result != "OK")
                throw std::runtime_error("Material physics properties verification failed");
        }
        std::vector<double> avatar_position;
        {
            std::lock_guard lock(mutex);
            for (auto& p : packets)
                if (p.spec->name == "AgentMovementComplete") {
                    avatar_position = std::get<std::vector<double>>(p.block("Data").at("Position"));
                    break;
                }
        }
        const double script_offset_x = argc > 6 && std::string_view(argv[6]) == "Rotating fixture" ? 4 : 2;
        move.sequence = sequence++;
        move.block("ObjectData")["Position"] = std::vector<double>{
            avatar_position.at(0) + script_offset_x, avatar_position.at(1) + 1, avatar_position.at(2)};
        transmit(move);
        wait([&](const auto& list) {
            for (auto& p : list)
                if (primitive(p) &&
                    real(std::get<std::vector<std::uint8_t>>(p.block("ObjectData").at("ObjectData")), 8) ==
                        static_cast<float>(avatar_position.at(2)) &&
                    std::get<std::uint64_t>(p.block("ObjectData").at("ClickAction")) == 2)
                    return true;
            return false;
        });
        tests::LegacyInventoryProbeIo task_io;
        task_io.make = send;
        task_io.send = transmit;
        task_io.wait = [&](auto predicate) { wait(predicate); };
        task_io.snapshot = [&] {
            std::lock_guard lock(mutex);
            return packets;
        };
        tests::check_empty_task_inventory(object_id, local, task_io);
        auto edit_text = [&](const char* packet_name, const char* field, std::string value) {
            auto edit = send(packet_name);
            auto block = lludp::make_block(edit.spec->blocks.at(1));
            block["LocalID"] = std::uint64_t(local);
            value.push_back('\0');
            block[field] = std::vector<std::uint8_t>(value.begin(), value.end());
            edit.blocks["ObjectData"] = {std::move(block)};
            transmit(edit);
        };
        edit_text("ObjectName", "Name", "Fixture Prim\rignored");
        edit_text("ObjectDescription", "Description", std::string(130, 'D'));
        auto selection = send("ObjectSelect");
        auto selected = lludp::make_block(selection.spec->blocks.at(1));
        selected["ObjectLocalID"] = std::uint64_t(local);
        selection.blocks["ObjectData"] = {std::move(selected)};
        transmit(selection);
        const bool verify_default_permissions = std::any_of(argv + 1, argv + argc, [](const char* option) {
            return std::string_view(option) == "--verify-default-object-permissions";
        });
        wait([&](const auto& list) {
            for (const auto& packet : list)
                if (packet.spec->name == "ObjectProperties")
                    for (const auto& part : packet.blocks.at("ObjectData"))
                        if (std::get<Uuid>(part.at("ObjectID")) == object_id) {
                            if (verify_default_permissions &&
                                (std::get<std::uint64_t>(part.at("EveryoneMask")) != 8192 ||
                                 std::get<std::uint64_t>(part.at("GroupMask")) != 32768 ||
                                 std::get<std::uint64_t>(part.at("NextOwnerMask")) != 40960))
                                throw std::runtime_error(
                                    "New object did not inherit saved default permission masks");
                            return true;
                        }
            return false;
        });
        if (argc > 10) {
            std::cout << "Prim selection query " << local << std::endl;
            std::string result;
            if (!std::getline(std::cin, result) || result != "OK")
                throw std::runtime_error("Selection physics properties verification failed");
        }
        auto rez = send("RezScript");
        rez.block("UpdateBlock")["ObjectLocalID"] = std::uint64_t(0xdead);
        auto& inventory = rez.block("InventoryBlock");
        inventory["FolderID"] = object_id;
        inventory["OwnerID"] = inventory["CreatorID"] = agent;
        inventory["Type"] = inventory["InvType"] = std::int64_t(10);
        inventory["Name"] = std::vector<std::uint8_t>{'N', 'e', 'w', ' ', 'S', 'c', 'r', 'i', 'p', 't', 0};
        inventory["BaseMask"] = inventory["OwnerMask"] = inventory["NextOwnerMask"] =
            std::uint64_t(0x7fffffff);
        transmit(rez);
        auto chat = [&](const auto& list, const std::string& message) {
            for (auto& p : list)
                if (p.spec->name == "ChatFromSimulator") {
                    auto& b = p.block("ChatData");
                    auto& bytes = std::get<std::vector<std::uint8_t>>(b.at("Message"));
                    std::string text(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0));
                    if (text == message && std::get<Uuid>(b.at("SourceID")) == object_id &&
                        std::get<Uuid>(b.at("OwnerID")) == agent)
                        return true;
                }
            return false;
        };
        if (argc > 6 && std::string_view(argv[6]) == "Disabled script fixture") {
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ObjectProperties")
                        for (const auto& entry : packet.blocks.at("ObjectData"))
                            if (std::get<Uuid>(entry.at("ObjectID")) == object_id &&
                                std::get<std::int64_t>(entry.at("InventorySerial")) >= 1)
                                return true;
                return false;
            });
            Uuid script_item;
            tests::check_task_inventory_download(agent, object_id, local, 1, task_io, {},
                                                 [&](Uuid, Uuid item) { script_item = item; });
            auto grab = send("ObjectGrab");
            grab.block("ObjectData")["LocalID"] = std::uint64_t(local);
            transmit(grab);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            {
                std::lock_guard lock(mutex);
                const auto handshake = std::find_if(packets.begin(), packets.end(), [](const auto& packet) {
                    return packet.spec->name == "RegionHandshake";
                });
                if (handshake == packets.end() ||
                    !(std::get<std::uint64_t>(handshake->block("RegionInfo").at("RegionFlags")) & 0x2000u) ||
                    (chat(packets, "Rotating fixture") || chat(packets, "Touched.")))
                    throw std::runtime_error("Disabled script fixture executed or omitted SkipScripts");
            }
            std::cout << "Enable scene scripting" << std::endl;
            std::string enable_ack;
            if (!std::getline(std::cin, enable_ack) || enable_ack != "OK")
                throw std::runtime_error("Console did not acknowledge enabling scripting");
            wait([&](const auto& list) { return chat(list, "Rotating fixture"); });
            wait([&](const auto& list) { return chat(list, "Empty linkset capacity 4096"); });
            auto status = send("GetScriptRunning");
            status.block("Script")["ObjectID"] = object_id;
            status.block("Script")["ItemID"] = script_item;
            transmit(status);
            std::cout << "Script running query " << object_id.str() << ' ' << script_item.str() << " 1"
                      << std::endl;
            std::string status_ack;
            if (!std::getline(std::cin, status_ack) || status_ack != "OK")
                throw std::runtime_error("Script status did not resume after enabling scripting");
            grab.sequence = sequence++;
            transmit(grab);
            wait([&](const auto& list) { return chat(list, "Touched."); });
            wait([&](const auto& list) { return chat(list, "Changed 8"); });
            std::cout << "Disable scene scripting" << std::endl;
            std::string acknowledgement;
            if (!std::getline(std::cin, acknowledgement) || acknowledgement != "OK")
                throw std::runtime_error("Console did not acknowledge disabling scripting");
            std::size_t before;
            {
                std::lock_guard lock(mutex);
                before = packets.size();
            }
            grab.sequence = sequence++;
            transmit(grab);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            {
                std::lock_guard lock(mutex);
                std::vector<lludp::Packet> added(packets.begin() + before, packets.end());
                if (chat(added, "Touched."))
                    throw std::runtime_error("Touch executed after disabling scripting");
            }
            auto resize = send("ObjectScale");
            auto scale = lludp::make_block(resize.spec->blocks.at(1));
            scale["ObjectLocalID"] = std::uint64_t(local);
            scale["Scale"] = std::vector<double>{3, 4, 5};
            resize.blocks["ObjectData"] = {std::move(scale)};
            transmit(resize);
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (primitive(packet) &&
                        std::get<std::vector<double>>(packet.block("ObjectData").at("Scale")) ==
                            std::vector<double>{3, 4, 5})
                        return true;
                return false;
            });
            std::cout << "Enable scene scripting" << std::endl;
            if (!std::getline(std::cin, acknowledgement) || acknowledgement != "OK")
                throw std::runtime_error("Console did not acknowledge enabling scripting");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            {
                std::lock_guard lock(mutex);
                std::vector<lludp::Packet> added(packets.begin() + before, packets.end());
                if (chat(added, "Changed 8"))
                    throw std::runtime_error("Disabled resize replayed a changed event");
            }
            transmit(send("LogoutRequest"));
            wait([&](const auto& list) { return has(list, "LogoutReply"); });
            return 0;
        }
        wait([&](const auto& list) { return chat(list, argc > 6 ? argv[6] : "Script running"); });
        const bool warm_notecard = std::any_of(argv, argv + argc, [](const char* arg) {
            return std::string_view(arg) == "--verify-warm-notecard";
        });
        const bool cold_notecard = std::any_of(argv, argv + argc, [](const char* arg) {
            return std::string_view(arg) == "--verify-notecard";
        });
        if (cold_notecard || warm_notecard)
            wait([&](const auto& list) {
                return chat(list, warm_notecard ? "Notecard cold 0" : "Notecard cold 1") &&
                       chat(list, "Notecard count 3") &&
                       chat(list, "Notecard lengths 255 300") && chat(list, "Notecard edges 1 1 1");
            });
        if (argc > 6 && std::string_view(argv[6]) == "Rotating fixture") {
            const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
            auto report = send("LandStatRequest");
            auto& request = report.block("RequestData");
            request["ReportType"] = std::uint64_t{0};
            request["RequestFlags"] = std::uint64_t{0};
            request["Filter"] = std::vector<std::uint8_t>{};
            request["ParcelLocalID"] = std::int64_t{0};
            transmit(report);
            std::cout << "Top script query " << object_id.str() << std::endl;
            std::string acknowledgement;
            if (!std::getline(std::cin, acknowledgement) || acknowledgement != "OK")
                throw std::runtime_error("Top script verification failed");
            std::set<std::string> script_urls;
            std::string denial;
            std::string quota_denial;
            bool quota_fixture = false;
            const std::string_view url_prefix = "Script HTTP URL ";
            const std::string_view quota_url_prefix = "Script HTTP quota URL ";
            const std::string_view quota_denial_prefix = "Script HTTP quota denied ";
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "ChatFromSimulator")
                        continue;
                    const auto& fields = packet.block("ChatData");
                    if (std::get<Uuid>(fields.at("SourceID")) != object_id)
                        continue;
                    const auto& bytes = std::get<std::vector<std::uint8_t>>(fields.at("Message"));
                    std::string message(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0));
                    if (message.starts_with("Script HTTP denied ")) {
                        denial = std::move(message);
                        return true;
                    }
                    if (message.starts_with(url_prefix))
                        script_urls.insert(message.substr(url_prefix.size()));
                    if (message.starts_with("Script reset URL "))
                        std::cout << message << std::endl;
                    if (message.starts_with(quota_url_prefix)) {
                        quota_fixture = true;
                        script_urls.insert(message.substr(quota_url_prefix.size()));
                    }
                    if (message.starts_with(quota_denial_prefix))
                        quota_denial = message.substr(quota_denial_prefix.size());
                    if (script_urls.size() == 2 && (!quota_fixture || !quota_denial.empty()))
                        return true;
                }
                return false;
            });
            if (denial.empty()) {
                std::cout << "Script HTTP URLs " << *script_urls.begin() << ' ' << *script_urls.rbegin();
                if (!quota_denial.empty())
                    std::cout << ' ' << quota_denial;
                std::cout << std::endl;
            } else
                std::cout << denial << std::endl;
            if (!std::getline(std::cin, acknowledgement))
                throw std::runtime_error("Script HTTP URL verification failed");
            if (acknowledgement == "HTTP complete") {
                transmit(send("LogoutRequest"));
                wait([&](const auto& list) { return has(list, "LogoutReply"); });
                return 0;
            }
            if (acknowledgement != "OK")
                throw std::runtime_error("Script HTTP URL verification failed");
            wait([&](const auto& list) { return chat(list, "Water 27.500000 Ground 21.000000"); });
            wait([&](const auto& list) {
                return chat(list, "Root agents 1") && chat(list, "Region agents " + agent.str()) &&
                       chat(list, "Parcel agents ") && chat(list, "Parcel owner agents " + agent.str()) &&
                       chat(list, "Region terraform flag 540737") &&
                       chat(list, "Parcel flags 134217794 32") && chat(list, "Parcel center 1") &&
                       chat(list, "Land owners " + agent.str() + " " + agent.str() + " " + Uuid{}.str()) &&
                       chat(list, "Over land 1 1 0 0") &&
                       chat(list, "Identity Native Viewer|Native Viewer|native.viewer|" + agent.str()) &&
                       chat(list, "Identity edges 1|||" + Uuid{}.str()) &&
                       chat(list, "Async username native.viewer") && chat(list, "Async display Native Viewer") &&
                       chat(list, "Async prim 1") && chat(list, "Async account Privacy Observer") &&
                       chat(list, "Async unknown []") && chat(list, "Invalid requests 1 1 1") &&
                       chat(list, "Async live key 1") && chat(list, "Async local key 1") &&
                       chat(list, "Async missing key 1") &&
                       chat(list, "Async foreign key 3d80d6ea-257f-4ce8-87f7-284639eaf825") &&
                       chat(list, "Async cached key 3d80d6ea-257f-4ce8-87f7-284639eaf825") &&
                       chat(list, "Agent online 1") && chat(list, "Agent name Native Viewer") &&
                       chat(list, "Agent rating 0,0,0,0,0,0") &&
                       chat(list, "Saved maturity MATURE") &&
                       chat(list, "Script danger 0 1 1") &&
                       chat(
                           list,
                           "Parcel details " + agent.str() +
                               ", 31744, 00000000-0000-4000-8000-000000000002, 32, Parcel 2, Fixture parcel, 00000000-0000-4000-8000-000000000003, 1700000002, <132.500000, 128.250000, 25.000000>, <0.000000, 1.000000, 0.000000>, 1, 0, 42, 10898, 1, 1");
            });
            wait([&](const auto& list) {
                for (auto value = epoch - 15; value <= epoch + 15; ++value)
                    if (chat(list, "Unix " + std::to_string(value)))
                        return true;
                return false;
            });
            wait([&](const auto& list) {
                if (chat(list, "Timer fired early"))
                    throw std::runtime_error("configured script timer fired early");
                return chat(list,
                            "Configured timer for New Script type 10 missing -1 count 1 first New Script");
            });
            auto deselect = send("ObjectDeselect");
            auto part = lludp::make_block(deselect.spec->blocks.at(1));
            part["ObjectLocalID"] = std::uint64_t(local);
            deselect.blocks["ObjectData"] = {std::move(part)};
            const auto count_updates = [&](const auto& list) {
                std::set<std::uint32_t> updates;
                for (const auto& packet : list) {
                    if (packet.spec->name != "ImprovedTerseObjectUpdate")
                        continue;
                    for (const auto& block : packet.blocks.at("ObjectData")) {
                        const auto& data = std::get<std::vector<std::uint8_t>>(block.at("Data"));
                        if (data.size() == 44 && data[0] == (local & 255) &&
                            data[1] == ((local >> 8) & 255) && data[2] == ((local >> 16) & 255) &&
                            data[3] == (local >> 24) && data[42] == 0xfe && data[43] == 0x84)
                            if (packet.reliable && packet.zerocoded)
                                updates.insert(packet.sequence);
                    }
                }
                return updates.size();
            };
            wait([&](const auto& list) {
                if (count_updates(list) >= 1)
                    return true;
                // A full update can absorb the initial target-omega update in the same scene frame.
                for (const auto& packet : list) {
                    if (packet.spec->name != "ObjectUpdate")
                        continue;
                    for (const auto& block : packet.blocks.at("ObjectData")) {
                        if (std::get<std::uint64_t>(block.at("ID")) != local)
                            continue;
                        const auto& data = std::get<std::vector<std::uint8_t>>(block.at("ObjectData"));
                        if (data.size() == 60 && real(data, 48) == 0.f && real(data, 52) == 0.f &&
                            real(data, 56) == 2.5f)
                            return true;
                    }
                }
                return false;
            });
            const auto before_deselect = count_updates(task_io.snapshot());
            transmit(deselect);
            wait([&](const auto& list) { return count_updates(list) > before_deselect; });
            framework::TextureEntry edited(Uuid("89556747-24cb-43ed-920b-47caed15465f"));
            image.sequence = sequence++;
            image.block("ObjectData")["TextureEntry"] = edited.bytes();
            transmit(image);
            wait([&](const auto& list) { return chat(list, "Changed 16"); });
            edited.default_texture->color({.5f, 1.f, 1.f, 1.f});
            image.sequence = sequence++;
            image.block("ObjectData")["TextureEntry"] = edited.bytes();
            transmit(image);
            wait([&](const auto& list) { return chat(list, "Changed 2"); });
            shape_edit.sequence = sequence++;
            shape_edit.block("ObjectData")["PathTwist"] = std::int64_t(-15);
            transmit(shape_edit);
            wait([&](const auto& list) { return chat(list, "Changed 4"); });
            auto resize = send("MultipleObjectUpdate");
            auto scale_block = lludp::make_block(resize.spec->blocks.at(1));
            scale_block["ObjectLocalID"] = std::uint64_t(local);
            scale_block["Type"] = std::uint64_t(5);
            std::vector<std::uint8_t> transform;
            for (const float value : {131.f, 129.f, 22.f, .75f, .5f, .5f}) {
                const auto bits = std::bit_cast<std::uint32_t>(value);
                for (unsigned shift = 0; shift < 32; shift += 8)
                    transform.push_back(static_cast<std::uint8_t>(bits >> shift));
            }
            scale_block["Data"] = std::move(transform);
            resize.blocks["ObjectData"] = {std::move(scale_block)};
            transmit(resize);
            wait([&](const auto& list) { return chat(list, "Changed 8"); });
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (!primitive(packet))
                        continue;
                    const auto& block = packet.block("ObjectData");
                    const auto& data = std::get<std::vector<std::uint8_t>>(block.at("ObjectData"));
                    if (real(data, 0) == 131.f &&
                        std::get<std::vector<double>>(block.at("Scale")) == std::vector<double>{.75, .5, .5})
                        return true;
                }
                return false;
            });
            auto rotate = send("MultipleObjectUpdate");
            auto rotation_block = lludp::make_block(rotate.spec->blocks.at(1));
            rotation_block["ObjectLocalID"] = std::uint64_t(local);
            rotation_block["Type"] = std::uint64_t(3);
            std::vector<std::uint8_t> rotation_data;
            for (const float value : {132.f, 129.f, 22.f, 0.f, 0.f, .5f}) {
                const auto bits = std::bit_cast<std::uint32_t>(value);
                for (unsigned shift = 0; shift < 32; shift += 8)
                    rotation_data.push_back(static_cast<std::uint8_t>(bits >> shift));
            }
            rotation_block["Data"] = std::move(rotation_data);
            rotate.blocks["ObjectData"] = {std::move(rotation_block)};
            transmit(rotate);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (!primitive(packet))
                        continue;
                    const auto& data =
                        std::get<std::vector<std::uint8_t>>(packet.block("ObjectData").at("ObjectData"));
                    if (real(data, 0) == 132.f && std::abs(real(data, 44) - .5f) < 1e-6f)
                        return true;
                }
                return false;
            });
            auto truncated = rotate;
            truncated.sequence = sequence++;
            truncated.block("ObjectData")["Type"] = std::uint64_t(5);
            auto& incomplete = std::get<std::vector<std::uint8_t>>(truncated.block("ObjectData").at("Data"));
            incomplete.resize(12);
            incomplete[2] = 0x48; // Little-endian 200.f.
            transmit(truncated);
            auto reset_rotation = rotate;
            reset_rotation.sequence = sequence++;
            reset_rotation.block("ObjectData")["Type"] = std::uint64_t(2);
            reset_rotation.block("ObjectData")["Data"] = std::vector<std::uint8_t>(12);
            transmit(reset_rotation);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (!primitive(packet))
                        continue;
                    const auto& data =
                        std::get<std::vector<std::uint8_t>>(packet.block("ObjectData").at("ObjectData"));
                    if (real(data, 0) == 132.f && real(data, 44) == 0.f)
                        return true;
                }
                return false;
            });
            auto ghost = rotate;
            ghost.sequence = sequence++;
            constexpr std::uint64_t missing_local_id = 0xffffffffu;
            ghost.block("ObjectData")["ObjectLocalID"] = missing_local_id;
            transmit(ghost);
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "KillObject")
                        for (const auto& block : packet.blocks.at("ObjectData"))
                            if (std::get<std::uint64_t>(block.at("ID")) == missing_local_id)
                                return true;
                return false;
            });
            auto half_turn = rotate;
            half_turn.sequence = sequence++;
            auto& half_turn_data =
                std::get<std::vector<std::uint8_t>>(half_turn.block("ObjectData").at("Data"));
            const auto write_float = [&](std::size_t offset, float value) {
                const auto bits = std::bit_cast<std::uint32_t>(value);
                for (unsigned byte = 0; byte < 4; ++byte)
                    half_turn_data.at(offset + byte) = static_cast<std::uint8_t>(bits >> (8 * byte));
            };
            write_float(0, 133.f);
            write_float(20, 0x1.fffff8p-1f);
            transmit(half_turn);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (!primitive(packet))
                        continue;
                    const auto& data =
                        std::get<std::vector<std::uint8_t>>(packet.block("ObjectData").at("ObjectData"));
                    // The SDK clears W below its cutoff, so the outgoing unit quaternion has Z equal to one.
                    if (real(data, 0) == 133.f && real(data, 44) == 1.f)
                        return true;
                }
                return false;
            });
            auto clamp_scale = send("ObjectScale");
            auto scale_limits = lludp::make_block(clamp_scale.spec->blocks.at(1));
            scale_limits["ObjectLocalID"] = std::uint64_t(local);
            scale_limits["Scale"] = std::vector<double>{0., 300., .5};
            clamp_scale.blocks["ObjectData"] = {std::move(scale_limits)};
            transmit(clamp_scale);
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (primitive(packet) &&
                        std::get<std::vector<double>>(packet.block("ObjectData").at("Scale")) ==
                            std::vector<double>{0.001f, 256., .5})
                        return true;
                return false;
            });
            auto remote_teleport = send("TeleportLocationRequest");
            remote_teleport.block("Info")["RegionHandle"] = (std::uint64_t{512} << 32) | 768;
            transmit(remote_teleport);
            wait([&](const auto& list) { return has(list, "TeleportFailed"); });
            auto flight = send("AgentUpdate");
            flight.block("AgentData")["ControlFlags"] = std::uint64_t{0x2000};
            transmit(flight);
            remote_teleport.sequence = sequence++;
            remote_teleport.block("Info")["RegionHandle"] = (std::uint64_t{1024} << 32) | 768;
            transmit(remote_teleport);
            wait([&](const auto& list) { return has(list, "TeleportFinish"); });
            wait([&](const auto& list) {
                bool started = false, finished = false;
                for (const auto& packet : list) {
                    if (packet.spec->name != "TeleportStart" && packet.spec->name != "TeleportFinish")
                        continue;
                    // The stored region fixture advertises BlockFly, so the flight request above is denied.
                    if (std::get<std::uint64_t>(packet.block("Info").at("TeleportFlags")) != 16u)
                        throw std::runtime_error("Remote teleport did not respect the source region's BlockFly setting");
                    started |= packet.spec->name == "TeleportStart";
                    finished |= packet.spec->name == "TeleportFinish";
                }
                return started && finished;
            });
            remote_teleport.sequence = sequence++;
            transmit(remote_teleport);
            wait([&](const auto& list) {
                constexpr std::string_view reason = "Previous teleport process incomplete";
                for (const auto& packet : list) {
                    if (packet.spec->name != "TeleportFailed")
                        continue;
                    const auto& bytes =
                        std::get<std::vector<std::uint8_t>>(packet.block("Info").at("Reason"));
                    if (std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())
                            .starts_with(reason))
                        return true;
                }
                return false;
            });
            auto home = send("TeleportLandmarkRequest");
            home.block("Info")["AgentID"] = agent;
            home.block("Info")["SessionID"] = session;
            transmit(home);
            wait([&](const auto& list) {
                constexpr std::string_view reason = "Already processing a teleport";
                for (const auto& packet : list) {
                    if (packet.spec->name != "TeleportFailed")
                        continue;
                    const auto& bytes =
                        std::get<std::vector<std::uint8_t>>(packet.block("Info").at("Reason"));
                    if (std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())
                            .starts_with(reason))
                        return true;
                }
                return false;
            });
            auto cancel_teleport = send("TeleportCancel");
            cancel_teleport.block("Info")["AgentID"] = agent;
            cancel_teleport.block("Info")["SessionID"] = session;
            transmit(cancel_teleport);
            flight.sequence = sequence++;
            flight.block("AgentData")["ControlFlags"] = std::uint64_t{0};
            transmit(flight);
            home.sequence = sequence++;
            transmit(home);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "TeleportLocal")
                        continue;
                    const auto& info = packet.block("Info");
                    if (std::get<std::uint64_t>(info.at("TeleportFlags")) == 34 &&
                        std::get<std::vector<double>>(info.at("Position")) ==
                            std::vector<double>{128, 128, 25} &&
                        std::get<std::vector<double>>(info.at("LookAt")) == std::vector<double>{0, 1, 0})
                        return true;
                }
                return false;
            });
            home.sequence = sequence++;
            home.block("Info")["LandmarkID"] = Uuid("84848484-8484-4484-8484-848484848484");
            transmit(home);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "TeleportLocal")
                        continue;
                    const auto& info = packet.block("Info");
                    if (std::get<std::uint64_t>(info.at("TeleportFlags")) != 10)
                        continue;
                    const auto& position = std::get<std::vector<double>>(info.at("Position"));
                    if (position != std::vector<double>{125, 126, 30})
                        throw std::runtime_error(
                            "Unexpected landmark position: " + std::to_string(position[0]) + "," +
                            std::to_string(position[1]) + "," + std::to_string(position[2]));
                    return true;
                }
                return false;
            });
            auto subregion = send("TeleportLocationRequest");
            auto& subregion_info = subregion.block("Info");
            subregion_info["RegionHandle"] = (std::uint64_t{256004} << 32) | 256008;
            subregion_info["Position"] = std::vector<double>{121, 118, 30};
            subregion_info["LookAt"] = std::vector<double>{1, 0, 0};
            transmit(subregion);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "TeleportLocal")
                        continue;
                    const auto& info = packet.block("Info");
                    if (std::get<std::uint64_t>(info.at("TeleportFlags")) == 16 &&
                        std::get<std::vector<double>>(info.at("Position")) ==
                            std::vector<double>{125, 126, 30})
                        return true;
                }
                return false;
            });
            auto edit_land = send("ModifyLand");
            edit_land.block("ModifyBlock")["Action"] = std::uint64_t{1};
            edit_land.block("ModifyBlock")["Seconds"] = 2.;
            auto parcel = lludp::make_block(edit_land.spec->blocks.at(2));
            parcel["West"] = parcel["East"] = 64.;
            parcel["South"] = parcel["North"] = 64.;
            edit_land.blocks["ParcelData"] = {std::move(parcel)};
            auto brush = lludp::make_block(edit_land.spec->blocks.at(3));
            brush["BrushSize"] = 2.;
            edit_land.blocks["ModifyBlockExtended"] = {std::move(brush)};
            const auto old_layers = task_io.snapshot();
            transmit(edit_land);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "LayerData")
                        continue;
                    const auto& data = packet.block("LayerData").at("Data");
                    if (std::none_of(old_layers.begin(), old_layers.end(), [&](const auto& old) {
                            return old.spec->name == "LayerData" && old.block("LayerData").at("Data") == data;
                        }))
                        return true;
                }
                return false;
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            edit_land.sequence = sequence++;
            edit_land.block("ModifyBlock")["Action"] = std::uint64_t{0};
            edit_land.block("ModifyBlock")["Height"] = 30.;
            auto& flatten = edit_land.block("ParcelData");
            flatten["West"] = flatten["East"] = 100.;
            flatten["South"] = flatten["North"] = 100.;
            scene::Terrain expected_land;
            expected_land.clear_land();
            expected_land.set(107, 107, 24.f);
            for (int x = 98; x <= 102; ++x)
                for (int y = 98; y <= 102; ++y) {
                    const float blend =
                        2.f * (1.f - float((x - 100) * (x - 100) + (y - 100) * (y - 100)) / 8.f);
                    expected_land.set(x, y, 21.f + 9.f * std::min(1.f, blend));
                }
            const scene::TerrainPatchCoordinate flattened_patch{6, 6};
            const auto expected_layer =
                scene::terrain_layer_packets(expected_land, std::span(&flattened_patch, 1)).front().data;
            transmit(edit_land);
            wait([&](const auto& list) {
                return std::any_of(list.begin(), list.end(), [&](const auto& packet) {
                    return packet.spec->name == "LayerData" &&
                           std::get<std::vector<std::uint8_t>>(packet.block("LayerData").at("Data")) ==
                               expected_layer;
                });
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            edit_land.sequence = sequence++;
            edit_land.block("ModifyBlock")["Action"] = std::uint64_t{3};
            auto smoothed_land = expected_land.clone();
            for (int x = 98; x <= 102; ++x)
                for (int y = 98; y <= 102; ++y) {
                    const int distance = (x - 100) * (x - 100) + (y - 100) * (y - 100);
                    if (distance >= 4)
                        continue;
                    float sum = 0;
                    for (int sx = x - 2; sx <= x + 2; ++sx)
                        for (int sy = y - 2; sy <= y + 2; ++sy)
                            sum += expected_land.get(sx, sy);
                    const float old = expected_land.get(x, y);
                    smoothed_land.set(x, y, old - (1.f - distance / 4.f) * (old - sum / 25.f));
                }
            const auto smoothed_layer =
                scene::terrain_layer_packets(smoothed_land, std::span(&flattened_patch, 1)).front().data;
            transmit(edit_land);
            wait([&](const auto& list) {
                return std::any_of(list.begin(), list.end(), [&](const auto& packet) {
                    return packet.spec->name == "LayerData" &&
                           std::get<std::vector<std::uint8_t>>(packet.block("LayerData").at("Data")) ==
                               smoothed_layer;
                });
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            edit_land.sequence = sequence++;
            edit_land.block("ModifyBlock")["Action"] = std::uint64_t{4};
            edit_land.block("ModifyBlock")["Seconds"] = 8.;
            flatten["West"] = flatten["East"] = 104.;
            flatten["South"] = flatten["North"] = 104.;
            edit_land.block("ModifyBlockExtended")["BrushSize"] = 1.;
            smoothed_land.set(104, 104, 21.f + 8.f * .736504077911377f);
            const auto noise_layer =
                scene::terrain_layer_packets(smoothed_land, std::span(&flattened_patch, 1)).front().data;
            transmit(edit_land);
            wait([&](const auto& list) {
                return std::any_of(list.begin(), list.end(), [&](const auto& packet) {
                    return packet.spec->name == "LayerData" &&
                           std::get<std::vector<std::uint8_t>>(packet.block("LayerData").at("Data")) ==
                               noise_layer;
                });
            });
            const auto restored_patch_count = [&](const auto& list) {
                std::set<std::uint32_t> sequences;
                for (const auto& packet : list)
                    if (packet.spec->name == "LayerData" &&
                        std::get<std::vector<std::uint8_t>>(packet.block("LayerData").at("Data")) ==
                            smoothed_layer)
                        sequences.insert(packet.sequence);
                return sequences.size();
            };
            const auto before_revert = restored_patch_count(task_io.snapshot());
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            edit_land.sequence = sequence++;
            edit_land.block("ModifyBlock")["Action"] = std::uint64_t{5};
            transmit(edit_land);
            wait([&](const auto& list) { return restored_patch_count(list) > before_revert; });
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            edit_land.sequence = sequence++;
            edit_land.block("ModifyBlock")["Action"] = std::uint64_t{1};
            edit_land.block("ModifyBlock")["Seconds"] = 3.;
            flatten["LocalID"] = std::int64_t{-1};
            flatten["West"] = 106.;
            flatten["East"] = 108.;
            flatten["South"] = 106.;
            flatten["North"] = 107.;
            smoothed_land.set(104, 104, 21.f);
            for (int x = 106; x <= 108; ++x)
                for (int y = 106; y <= 107; ++y)
                    smoothed_land.set(x, y, 24.f);
            smoothed_land.set(107, 107, 27.f);
            auto second_area = flatten;
            second_area["LocalID"] = std::int64_t{1};
            second_area["West"] = 110.;
            second_area["East"] = 111.;
            second_area["South"] = 110.;
            second_area["North"] = 111.;
            auto unknown_area = second_area;
            unknown_area["LocalID"] = std::int64_t{999};
            unknown_area["West"] = 112.;
            unknown_area["East"] = 113.;
            edit_land.blocks["ParcelData"].push_back(std::move(second_area));
            edit_land.blocks["ParcelData"].push_back(std::move(unknown_area));
            const auto second_brush = edit_land.block("ModifyBlockExtended");
            edit_land.blocks["ModifyBlockExtended"].push_back(second_brush);
            edit_land.blocks["ModifyBlockExtended"].push_back(second_brush);
            for (int x = 110; x <= 111; ++x)
                for (int y = 110; y <= 111; ++y)
                    smoothed_land.set(x, y, 24.f);
            const auto raised_area =
                scene::terrain_layer_packets(smoothed_land, std::span(&flattened_patch, 1)).front().data;
            transmit(edit_land);
            wait([&](const auto& list) {
                return std::any_of(list.begin(), list.end(), [&](const auto& packet) {
                    return packet.spec->name == "LayerData" &&
                           std::get<std::vector<std::uint8_t>>(packet.block("LayerData").at("Data")) ==
                               raised_area;
                });
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            auto limit_land = send("ModifyLand");
            limit_land.block("ModifyBlock")["Action"] = std::uint64_t{1};
            limit_land.block("ModifyBlock")["Seconds"] = 250.;
            auto limit_point = lludp::make_block(limit_land.spec->blocks.at(2));
            limit_point["West"] = limit_point["East"] = 110.;
            limit_point["South"] = limit_point["North"] = 108.;
            limit_land.blocks["ParcelData"].push_back(std::move(limit_point));
            limit_land.blocks["ModifyBlockExtended"].push_back(second_brush);
            const auto original_height = smoothed_land.get(110, 108);
            smoothed_land.set(110, 108, original_height + 250.f);
            const auto administrator_raise =
                scene::terrain_layer_packets(smoothed_land, std::span(&flattened_patch, 1)).front().data;
            transmit(limit_land);
            wait([&](const auto& list) {
                return std::any_of(list.begin(), list.end(), [&](const auto& packet) {
                    return packet.spec->name == "LayerData" &&
                           std::get<std::vector<std::uint8_t>>(packet.block("LayerData").at("Data")) ==
                               administrator_raise;
                });
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            limit_land.sequence = sequence++;
            limit_land.block("ModifyBlock")["Action"] = std::uint64_t{2};
            smoothed_land.set(110, 108, original_height);
            const auto lowered_area =
                scene::terrain_layer_packets(smoothed_land, std::span(&flattened_patch, 1)).front().data;
            transmit(limit_land);
            wait([&](const auto& list) {
                return std::any_of(list.begin(), list.end(), [&](const auto& packet) {
                    return packet.spec->name == "LayerData" &&
                           std::get<std::vector<std::uint8_t>>(packet.block("LayerData").at("Data")) ==
                               lowered_area;
                });
            });

        }
        wait([&](const auto& list) {
            for (const auto& packet : list) {
                if (packet.spec->name != "ObjectProperties")
                    continue;
                for (const auto& part : packet.blocks.at("ObjectData")) {
                    if (std::get<Uuid>(part.at("ObjectID")) != object_id)
                        continue;
                    const auto text = [&](const char* key) {
                        const auto& bytes = std::get<std::vector<std::uint8_t>>(part.at(key));
                        return std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0));
                    };
                    if (text("Name") != "Fixture Prim" || text("Description") != std::string(127, 'D'))
                        throw std::runtime_error(
                            "ObjectProperties did not report the edited name and description");
                    return true;
                }
            }
            return false;
        });
        Uuid first_script_asset;
        const bool verify_contents = argc > 10 && std::string_view(argv[10]) == "--verify-script-assets";
        const auto verify_script_asset = [&](Uuid asset) {
            if (first_script_asset.is_zero())
                first_script_asset = asset;
            if (verify_contents) {
                std::string result;
                if (!std::getline(std::cin, result) || result != "OK")
                    throw std::runtime_error("Live Contents script asset verification failed");
            }
        };
        Uuid first_script;
        bool first_script_has_instance = true, first_script_running = true;
        std::function<void(Uuid, Uuid)> verify_script_running;
        if (argc > 10)
            verify_script_running = [&](Uuid object, Uuid item) {
                if (first_script.is_zero())
                    first_script = item;
                if (item == first_script && !first_script_has_instance)
                    return;
                std::cout << "Script running query " << object.str() << ' ' << item.str() << ' '
                          << ((item != first_script || first_script_running) ? 1 : 0) << std::endl;
                std::string result;
                if (!std::getline(std::cin, result) || result != "OK")
                    throw std::runtime_error("Live script running EventQueue verification failed");
            };
        auto first_task_file = tests::check_task_inventory_download(
            agent, object_id, local, 1, task_io, verify_script_asset, verify_script_running);
        for (int arg = 11; arg < argc; ++arg) {
            const std::string_view option(argv[arg]);
            constexpr std::string_view prefix = "--udp-task-script=";
            if (!option.starts_with(prefix))
                continue;
            std::ifstream input(std::filesystem::u8path(option.substr(prefix.size())), std::ios::binary);
            const std::vector<std::uint8_t> expected{std::istreambuf_iterator<char>(input), {}};
            if (expected.empty() || first_script.is_zero() || first_script_asset.is_zero())
                throw std::runtime_error("Task script transfer fixture is incomplete");
            const auto request_transfer = [&](const Uuid& transfer, std::int64_t source,
                                              const std::vector<std::uint8_t>& params) {
                auto request = send("TransferRequest");
                auto& fields = request.block("TransferInfo");
                fields["TransferID"] = transfer;
                fields["ChannelType"] = std::int64_t{2};
                fields["SourceType"] = source;
                fields["Priority"] = 1.0;
                fields["Params"] = params;
                transmit(request);
            };
            const auto transfer = Uuid::random();
            std::vector<std::uint8_t> params(48);
            for (const auto& id : {object_id, first_script, first_script_asset})
                params.insert(params.end(), id.bytes().begin(), id.bytes().end());
            params.insert(params.end(), {10, 0, 0, 0});
            request_transfer(transfer, 3, params);
            const auto chunk_count = (expected.size() + 599) / 600;
            wait([&](const auto& received) {
                bool header_seen = false;
                std::vector<const std::vector<std::uint8_t>*> chunks(chunk_count);
                for (const auto& packet : received) {
                    if (packet.spec->name == "TransferInfo") {
                        const auto& info = packet.block("TransferInfo");
                        if (std::get<Uuid>(info.at("TransferID")) != transfer)
                            continue;
                        if (std::get<std::int64_t>(info.at("Status")) != 0 ||
                            std::get<std::int64_t>(info.at("Size")) !=
                                static_cast<std::int64_t>(expected.size()) ||
                            std::get<std::vector<std::uint8_t>>(info.at("Params")) != params)
                            throw std::runtime_error(
                                "Task script transfer header differs from the inventory request");
                        header_seen = true;
                    } else if (packet.spec->name == "TransferPacket") {
                        const auto& chunk = packet.block("TransferData");
                        if (std::get<Uuid>(chunk.at("TransferID")) != transfer)
                            continue;
                        const auto index =
                            static_cast<std::size_t>(std::get<std::int64_t>(chunk.at("Packet")));
                        if (index >= chunk_count ||
                            std::get<std::int64_t>(chunk.at("Status")) != (index + 1 == chunk_count))
                            throw std::runtime_error(
                                "Task script transfer chunk has invalid numbering or status");
                        chunks[index] = &std::get<std::vector<std::uint8_t>>(chunk.at("Data"));
                    }
                }
                if (!header_seen || std::find(chunks.begin(), chunks.end(), nullptr) != chunks.end())
                    return false;
                std::vector<std::uint8_t> actual;
                for (const auto* chunk : chunks)
                    actual.insert(actual.end(), chunk->begin(), chunk->end());
                if (actual != expected)
                    throw std::runtime_error("Task inventory transfer returned different script bytes");
                return true;
            });
            const auto direct = Uuid::random();
            std::vector<std::uint8_t> direct_params(first_script_asset.bytes().begin(),
                                                    first_script_asset.bytes().end());
            direct_params.insert(direct_params.end(), {10, 0, 0, 0});
            request_transfer(direct, 2, direct_params);
            std::unique_lock lock(mutex);
            if (changed.wait_for(lock, std::chrono::milliseconds(300), [&] {
                    return std::any_of(packets.begin(), packets.end(), [&](const auto& packet) {
                        if (packet.spec->name == "TransferInfo")
                            return std::get<Uuid>(packet.block("TransferInfo").at("TransferID")) == direct;
                        if (packet.spec->name == "TransferPacket")
                            return std::get<Uuid>(packet.block("TransferData").at("TransferID")) == direct;
                        return false;
                    });
                }))
                throw std::runtime_error("Direct script transfer bypassed inventory authorization");
        }
        if (argc > 6 && std::string_view(argv[6]) == "Rotating fixture")
            wait([&](const auto& list) {
                return chat(list, "Inventory asset " + first_script_asset.str() + " missing " + Uuid{}.str() +
                                      " owner mask 2147483647");
            });
        auto second_start = task_io.snapshot().size();
        rez.sequence = sequence++;
        transmit(rez);
        wait([&](const auto& list) {
            std::vector<lludp::Packet> added(list.begin() + std::min(second_start, list.size()), list.end());
            return chat(added, argc > 6 ? argv[6] : "Script running");
        });
        auto second_task_file = tests::check_task_inventory_download(
            agent, object_id, local, 2, task_io, verify_script_asset, verify_script_running);
        if (first_task_file == second_task_file)
            throw std::runtime_error("Task mutation did not invalidate filename cache");
        if (has_touch) {
            const auto before_touch = task_io.snapshot().size();
            auto grab = send("ObjectGrab");
            grab.block("ObjectData")["LocalID"] = std::uint64_t(local);
            if (std::string_view(argv[6]) == "Rotating fixture") {
                grab.block("ObjectData")["GrabOffset"] = std::vector<double>{1, 2, 3};
                auto surface = lludp::make_block(grab.spec->blocks.at(2));
                surface["FaceIndex"] = std::int64_t{4};
                surface["Position"] = std::vector<double>{1.25, 2.5, 3.75};
                surface["Normal"] = std::vector<double>{0, 0, 1};
                surface["Binormal"] = std::vector<double>{0, 1, 0};
                surface["STCoord"] = std::vector<double>{0.125, 0.25, 0};
                surface["UVCoord"] = std::vector<double>{0.5, 0.75, 0};
                grab.blocks["SurfaceInfo"] = {std::move(surface)};
            }
            transmit(grab);
            wait([&](const auto& list) { return chat(list, argv[7]); });
            if (std::any_of(argv + 1, argv + argc, [](const char* option) {
                    return std::string_view(option) == "--verify-ground-bounds";
                })) {
                auto release = send("ObjectDeGrab");
                release.block("ObjectData")["LocalID"] = std::uint64_t(local);
                transmit(release);
                auto sit = send("AgentUpdate");
                sit.block("AgentData")["ControlFlags"] = std::uint64_t{0x20000 | 0x40000 | 0x08000000};
                sit.block("AgentData")["State"] = std::uint64_t{4};
                transmit(sit);
                const auto wait_for_animation = [&](Uuid id) {
                    wait([&](const auto& list) {
                        for (const auto& packet : list) {
                            if (packet.spec->name != "AvatarAnimation" ||
                                std::get<Uuid>(packet.block("Sender").at("ID")) != agent)
                                continue;
                            const auto animations = packet.blocks.find("AnimationList");
                            if (animations != packet.blocks.end())
                                for (const auto& animation : animations->second)
                                    if (std::get<Uuid>(animation.at("AnimID")) == id)
                                        return true;
                        }
                        return false;
                    });
                };
                wait_for_animation(Uuid("1a2bd58e-87ff-0df8-0b4c-53e047b0bb6e"));
                grab.sequence = sequence++;
                transmit(grab);
                wait([&](const auto& list) { return chat(list, "Ground bounds 1"); });
                release.sequence = sequence++;
                transmit(release);
                auto stand = send("AgentUpdate");
                stand.block("AgentData")["ControlFlags"] = std::uint64_t{0x10000};
                transmit(stand);
                auto flight = send("AgentUpdate");
                flight.block("AgentData")["ControlFlags"] = std::uint64_t{0x2000};
                transmit(flight);
                auto run = send("SetAlwaysRun");
                run.block("AgentData")["AlwaysRun"] = true;
                transmit(run);
                const Uuid busy("efcf670c-2d18-8128-973a-034ebc806b67");
                auto animation = send("AgentAnimation");
                animation.blocks["AnimationList"].push_back({{"AnimID", busy}, {"StartAnim", true}});
                transmit(animation);
                wait_for_animation(busy);
                grab.sequence = sequence++;
                transmit(grab);
                wait([&](const auto& list) { return chat(list, "Flight flags 1"); });
                release.sequence = sequence++;
                transmit(release);
                animation.sequence = sequence++;
                animation.blocks["AnimationList"].front()["StartAnim"] = false;
                transmit(animation);
                run.sequence = sequence++;
                run.block("AgentData")["AlwaysRun"] = false;
                transmit(run);
                flight.sequence = sequence++;
                flight.block("AgentData")["ControlFlags"] = std::uint64_t{0};
                transmit(flight);
            }
            if (std::string_view(argv[6]) == "Rotating fixture")
                wait([&](const auto& list) {
                    return chat(list, "Touch avatar " + agent.str() +
                                          " face 4 size <0.500000, 0.625000, 2.125000> language en-us/de-de");
                });
            if (std::string_view(argv[6]) == "Rotating fixture") {
                wait([&](const auto& list) {
                    return chat(list, "Detector Native Viewer|" + agent.str() +
                                          "|1|<1.000000, 2.000000, 3.000000>");
                });
                auto sit = send("AgentRequestSit");
                sit.block("TargetObject")["TargetID"] = object_id;
                sit.block("TargetObject")["Offset"] = std::vector<double>{200, 0, 0};
                transmit(sit);
                {
                    std::unique_lock lock(mutex);
                    if (changed.wait_for(lock, std::chrono::milliseconds(250), [&] {
                            return has(packets, "AvatarSitResponse");
                        }))
                        throw std::runtime_error("Out-of-range sit request received a response");
                }
                sit.sequence = sequence++;
                sit.block("TargetObject")["Offset"] = std::vector<double>{0, 0, 0};
                transmit(sit);
                wait([&](const auto& list) {
                    return std::ranges::any_of(list, [&](const auto& packet) {
                        if (packet.spec->name != "AvatarSitResponse")
                            return false;
                        const auto& transform = packet.block("SitTransform");
                        return std::get<Uuid>(packet.block("SitObject").at("ID")) == object_id &&
                               std::get<std::vector<double>>(transform.at("SitPosition")) ==
                                   std::vector<double>{.5, 1.25, 2} &&
                               std::get<std::vector<double>>(transform.at("CameraEyeOffset")) ==
                                   std::vector<double>{2, 3, 4} &&
                               std::get<std::vector<double>>(transform.at("CameraAtOffset")) ==
                                   std::vector<double>{5, 6, 7} &&
                               std::get<bool>(transform.at("ForceMouselook"));
                    });
                });
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "ObjectUpdate")
                            for (const auto& row : packet.blocks.at("ObjectData"))
                                if (std::get<Uuid>(row.at("FullID")) == agent &&
                                    std::get<std::uint64_t>(row.at("ParentID")) == local) {
                                    const auto& bytes = std::get<std::vector<std::uint8_t>>(row.at("ObjectData"));
                                    const auto height = std::get<std::vector<double>>(row.at("Scale")).at(2);
                                    if (std::abs(real(bytes, 24) - (2.4f + height * 0.02638f)) < 0.001)
                                        return true;
                                }
                    return false;
                });
                wait([&](const auto& list) { return chat(list, "Seated link " + agent.str()); });
                transmit(send("AgentSit"));
                const auto seated_packets = task_io.snapshot();
                const auto before_stand = seated_packets.size();
                std::optional<float> seat_object_z;
                for (const auto& packet : seated_packets)
                    if (packet.spec->name == "ObjectUpdate")
                        for (const auto& row : packet.blocks.at("ObjectData"))
                            if (std::get<Uuid>(row.at("FullID")) == object_id)
                                seat_object_z = real(std::get<std::vector<std::uint8_t>>(row.at("ObjectData")), 8);
                if (!seat_object_z)
                    throw std::runtime_error("Missing seat object position");
                auto stand = send("AgentUpdate");
                stand.block("AgentData")["ControlFlags"] = std::uint64_t{0x10000};
                grab.sequence = sequence++;
                transmit(grab);
                wait([&](const auto& list) {
                    for (auto i = before_stand; i < list.size(); ++i)
                        if (list[i].spec->name == "ObjectUpdate")
                            for (const auto& row : list[i].blocks.at("ObjectData"))
                                if (std::get<Uuid>(row.at("FullID")) == agent &&
                                    std::get<std::uint64_t>(row.at("ParentID")) == 0) {
                                    const auto height = std::get<std::vector<double>>(row.at("Scale")).at(2);
                                    const auto z = real(std::get<std::vector<std::uint8_t>>(row.at("ObjectData")), 24);
                                    if (std::abs(z - (*seat_object_z + 1.85 + height * .5)) < .001)
                                        return true;
                                }
                    return false;
                });
                wait([&](const auto& list) {
                    return chat(std::span(list).subspan(before_stand), "Seated link cleared");
                });
                wait([&](const auto& list) {
                    for (auto i = before_stand; i < list.size(); ++i)
                        if (list[i].spec->name == "ClearFollowCamProperties" &&
                            std::get<Uuid>(list[i].block("ObjectData").at("ObjectID")) == object_id)
                            return true;
                    return false;
                });
                auto surface_sit = send("AgentRequestSit");
                surface_sit.block("TargetObject")["TargetID"] = object_id;
                surface_sit.block("TargetObject")["Offset"] = std::vector<double>{.25, .5, 1};
                transmit(surface_sit);
                wait([&](const auto& list) {
                    return std::ranges::any_of(list, [&](const auto& packet) {
                        return packet.spec->name == "AvatarSitResponse" &&
                               std::get<Uuid>(packet.block("SitObject").at("ID")) == object_id &&
                               std::get<std::vector<double>>(packet.block("SitTransform").at("SitPosition")) ==
                                   std::vector<double>{.25, .5, 1};
                    });
                });
                wait([&](const auto& list) { return chat(list, "Surface sitter " + agent.str()); });
                stand.sequence = sequence++;
                transmit(stand);
                wait([&](const auto& list) { return chat(list, "Surface sitter cleared"); });
                wait([&](const auto& list) {
                    return chat(list, "Surface <1.250000, 2.500000, 3.750000><0.000000, 0.000000, 1.000000>"
                                      "<0.000000, 1.000000, 0.000000><0.125000, 0.250000, 0.000000>"
                                      "<0.500000, 0.750000, 0.000000>");
                });
                const auto properties_start = task_io.snapshot().size();
                auto refresh = send("ObjectSelect");
                refresh.blocks["ObjectData"] = selection.blocks.at("ObjectData");
                transmit(refresh);
                wait([&](const auto& list) {
                    for (auto i = properties_start; i < list.size(); ++i) {
                        if (list[i].spec->name != "ObjectProperties")
                            continue;
                        for (const auto& part : list[i].blocks.at("ObjectData")) {
                            if (std::get<Uuid>(part.at("ObjectID")) != object_id)
                                continue;
                            const auto text = [&](const char* field) {
                                const auto& bytes = std::get<std::vector<std::uint8_t>>(part.at(field));
                                return std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0));
                            };
                            if (text("Name") == "Scripted Prim" && text("Description") == "Edited by LSL" &&
                                text("TouchName") == "Activate " && text("SitName") == "Take seat")
                                return true;
                        }
                    }
                    return false;
                });
                wait([&](const auto& list) {
                    for (std::size_t i = before_touch; i < list.size(); ++i) {
                        const auto& packet = list[i];
                        if (packet.spec->name != "ObjectUpdate" || packet.resent)
                            continue;
                        for (const auto& block : packet.blocks.at("ObjectData")) {
                            if (std::get<Uuid>(block.at("FullID")) != object_id)
                                continue;
                            const auto& data = std::get<std::vector<std::uint8_t>>(block.at("ObjectData"));
                            if (data.size() == 60 && real(data, 48) == 0 && real(data, 52) == 0 &&
                                real(data, 56) == 0 &&
                                std::get<std::vector<double>>(block.at("Scale")) ==
                                    std::vector<double>{2, 256, 4} &&
                                std::get<std::uint64_t>(block.at("ClickAction")) == 2 &&
                                std::get<std::vector<std::uint8_t>>(block.at("Text")) ==
                                    std::vector<std::uint8_t>{'S', 'c', 'r', 'i', 'p', 't', ' ', 'l', 'a',
                                                              'b', 'e', 'l', 0} &&
                                std::get<std::vector<std::uint8_t>>(block.at("TextColor")) ==
                                    std::vector<std::uint8_t>{255, 127, 0, 128} &&
                                std::get<Uuid>(block.at("Sound")) ==
                                    Uuid("12345678-1234-4234-8234-123456789abc") &&
                                std::get<double>(block.at("Gain")) == .75 &&
                                std::get<double>(block.at("Radius")) == 31.5 &&
                                std::get<std::uint64_t>(block.at("Flags")) == 19)
                                return true;
                        }
                    }
                    return false;
                });
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "AttachedSoundGainChange") {
                            const auto& block = packet.block("DataBlock");
                            if (std::get<Uuid>(block.at("ObjectID")) == object_id &&
                                std::get<double>(block.at("Gain")) == 1.)
                                return true;
                        }
                    return false;
                });
                wait([&](const auto& list) {
                    for (const auto& packet : list) {
                        if (packet.spec->name != "PreloadSound")
                            continue;
                        const auto bytes = lludp::encode_packet(packet);
                        const bool compression_fits = lludp::zero_encode(bytes).size() <= bytes.size();
                        if (packet.zerocoded != compression_fits)
                            continue;
                        for (const auto& block : packet.blocks.at("DataBlock"))
                            if (std::get<Uuid>(block.at("ObjectID")) == object_id &&
                                std::get<Uuid>(block.at("OwnerID")) == agent &&
                                std::get<Uuid>(block.at("SoundID")) ==
                                    Uuid("12345678-1234-4234-8234-123456789abc"))
                                return true;
                    }
                    return false;
                });
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "ObjectUpdate")
                            for (const auto& part : packet.blocks.at("ObjectData"))
                                if (std::get<Uuid>(part.at("FullID")) == object_id &&
                                    std::get<std::uint64_t>(part.at("Flags")) == 21 &&
                                    std::get<double>(part.at("Gain")) == .5)
                                    return true;
                    return false;
                });
                wait([&](const auto& list) {
                    bool ordinary = false, synchronized = false;
                    for (const auto& packet : list)
                        if (packet.spec->name == "AttachedSound") {
                            const auto& block = packet.block("DataBlock");
                            if (std::get<Uuid>(block.at("ObjectID")) != object_id ||
                                std::get<Uuid>(block.at("OwnerID")) != agent ||
                                std::get<Uuid>(block.at("SoundID")) !=
                                    Uuid("12345678-1234-4234-8234-123456789abc"))
                                continue;
                            const auto flags = std::get<std::uint64_t>(block.at("Flags"));
                            const auto gain = std::get<double>(block.at("Gain"));
                            ordinary |= flags == 16 && gain == 1.;
                            synchronized |= flags == 20 && gain == .25;
                        }
                    return ordinary && synchronized;
                });
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "SoundTrigger") {
                            const auto& block = packet.block("SoundData");
                            if (std::get<Uuid>(block.at("ObjectID")) == object_id &&
                                std::get<Uuid>(block.at("ParentID")) == object_id &&
                                std::get<Uuid>(block.at("OwnerID")) == agent &&
                                std::get<double>(block.at("Gain")) == .625 &&
                                std::get<std::uint64_t>(block.at("Handle")) != 0 &&
                                std::get<Uuid>(block.at("SoundID")) ==
                                    Uuid("12345678-1234-4234-8234-123456789abc"))
                                return true;
                        }
                    return false;
                });
                wait([&](const auto& list) {
                    bool received = false;
                    for (const auto& packet : list)
                        if (packet.spec->name == "SoundTrigger") {
                            const auto& block = packet.block("SoundData");
                            const auto sound = std::get<Uuid>(block.at("SoundID"));
                            if (sound == Uuid("32345678-1234-4234-8234-123456789abc"))
                                throw std::runtime_error("Limited sound reached a viewer outside the box");
                            if (sound == Uuid("22345678-1234-4234-8234-123456789abc") &&
                                std::get<Uuid>(block.at("ObjectID")) == object_id) {
                                const auto gain = std::get<double>(block.at("Gain"));
                                if (!(gain > 0 && gain < 1))
                                    throw std::runtime_error("Limited sound gain was not attenuated");
                                received = true;
                            }
                        }
                    return received;
                });
                transmit(send("EconomyDataRequest"));
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "EconomyData") {
                            const auto& info = packet.block("Info");
                            if (!packet.reliable ||
                                std::get<std::int64_t>(info.at("ObjectCapacity")) != 15000 ||
                                std::get<std::int64_t>(info.at("PriceUpload")) != 9 ||
                                std::get<std::int64_t>(info.at("PricePublicObjectDecay")) != 4 ||
                                std::get<double>(info.at("EnergyEfficiency")) != .75)
                                throw std::runtime_error(
                                    "Viewer economy data differs from region configuration");
                            return true;
                        }
                    return false;
                });
                auto balance = send("MoneyBalanceRequest");
                auto buy = send("ObjectBuy");
                auto purchase = lludp::make_block(buy.spec->blocks.at(1));
                purchase["ObjectLocalID"] = std::uint64_t(local);
                purchase["SalePrice"] = std::int64_t{5};
                buy.blocks["ObjectData"].push_back(std::move(purchase));
                transmit(buy);
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "ImprovedInstantMessage") {
                            const auto& message = packet.block("MessageBlock");
                            const auto& bytes = std::get<std::vector<std::uint8_t>>(message.at("Message"));
                            if (std::get<std::uint64_t>(message.at("Dialog")) == 1 &&
                                std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                                    "Buying anything for a price other than zero is not implemented")
                                return true;
                        }
                    return false;
                });
                buy.sequence = sequence++;
                buy.block("ObjectData")["ObjectLocalID"] = std::uint64_t{0};
                buy.block("ObjectData")["SalePrice"] = std::int64_t{0};
                transmit(buy);
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "AgentAlertMessage") {
                            const auto& bytes =
                                std::get<std::vector<std::uint8_t>>(packet.block("AlertData").at("Message"));
                            if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                                "/Unable to buy now. The object was not found.")
                                return true;
                        }
                    return false;
                });
                balance.sequence = sequence++;
                auto invalid_balance = balance;
                const Uuid invalid_transaction("72345678-1234-4234-8234-123456789abc");
                invalid_balance.block("AgentData")["SessionID"] = Uuid{};
                invalid_balance.block("MoneyData")["TransactionID"] = invalid_transaction;
                transmit(invalid_balance);
                balance.sequence = sequence++;
                const Uuid transaction("62345678-1234-4234-8234-123456789abc");
                balance.block("MoneyData")["TransactionID"] = transaction;
                transmit(balance);
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "MoneyBalanceReply") {
                            const auto& money = packet.block("MoneyData");
                            if (std::get<Uuid>(money.at("TransactionID")) == invalid_transaction)
                                throw std::runtime_error("Balance reply accepted a mismatched session");
                            if (std::get<Uuid>(money.at("TransactionID")) != transaction)
                                continue;
                            if (std::get<Uuid>(money.at("AgentID")) != agent ||
                                !std::get<bool>(money.at("TransactionSuccess")) ||
                                std::get<std::int64_t>(money.at("MoneyBalance")) != 0 ||
                                std::get<std::int64_t>(
                                    packet.block("TransactionInfo").at("TransactionType")) != 0)
                                throw std::runtime_error(
                                    "Viewer balance reply differs from sample economy behavior");
                            return true;
                        }
                    return false;
                });
                auto prices = send("RequestPayPrice");
                prices.block("ObjectData")["ObjectID"] = object_id;
                transmit(prices);
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "PayPriceReply") {
                            const auto& object = packet.block("ObjectData");
                            if (std::get<Uuid>(object.at("ObjectID")) != object_id)
                                continue;
                            std::vector<std::int64_t> buttons;
                            for (const auto& button : packet.blocks.at("ButtonData"))
                                buttons.push_back(std::get<std::int64_t>(button.at("PayButton")));
                            if (std::get<std::int64_t>(object.at("DefaultPayPrice")) != -2 ||
                                buttons != std::vector<std::int64_t>{5, 10, -1, -1})
                                throw std::runtime_error(
                                    "Viewer pay price reply differs from the script settings");
                            return true;
                        }
                    return false;
                });
                auto release = send("ObjectDeGrab");
                release.block("ObjectData")["LocalID"] = std::uint64_t(local);
                transmit(release);
                wait([&](const auto& list) {
                    for (const auto& packet : list)
                        if (packet.spec->name == "ObjectUpdate")
                            for (const auto& part : packet.blocks.at("ObjectData"))
                                if (std::get<Uuid>(part.at("FullID")) == object_id &&
                                    std::get<Uuid>(part.at("Sound")).is_zero() &&
                                    std::get<double>(part.at("Gain")) == 0. &&
                                    std::get<std::uint64_t>(part.at("Flags")) == 32)
                                    return true;
                    return false;
                });
            }
        }
        wait([&](const auto& list) {
            for (auto& p : list)
                if (primitive(p)) {
                    auto flags = std::get<std::uint64_t>(p.block("ObjectData").at("UpdateFlags"));
                    if ((flags & 0x40) != 0 && (flags & 0x800) == 0 && bool(flags & 0x80) == has_touch)
                        return true;
                }
            return false;
        });
        if (argc > 10 && std::string_view(argv[10]) == "--verify-script-assets") {
            auto previous_file = second_task_file;
            for (int mode = 1; mode <= 4; ++mode) {
                auto after = task_io.snapshot().size();
                std::cout << "Script save query " << object_id.str() << ' ' << first_script.str() << ' '
                          << mode << std::endl;
                std::string result;
                if (!std::getline(std::cin, result) || result != "OK")
                    throw std::runtime_error("Live script save HTTP verification failed");
                auto serial = mode + 2;
                wait([&](const auto& list) {
                    for (auto n = after; n < list.size(); ++n)
                        if (list[n].spec->name == "ObjectProperties")
                            for (const auto& b : list[n].blocks.at("ObjectData"))
                                if (std::get<Uuid>(b.at("ObjectID")) == object_id &&
                                    std::get<std::int64_t>(b.at("InventorySerial")) == serial)
                                    return true;
                    return false;
                });
                auto properties = task_io.snapshot();
                bool checked = false;
                for (auto n = after; n < properties.size(); ++n)
                    if (properties[n].spec->name == "ObjectProperties")
                        for (const auto& b : properties[n].blocks.at("ObjectData"))
                            if (std::get<Uuid>(b.at("ObjectID")) == object_id &&
                                std::get<std::int64_t>(b.at("InventorySerial")) == serial) {
                                if (!properties[n].reliable || !properties[n].zerocoded ||
                                    std::get<Uuid>(b.at("CreatorID")) != agent ||
                                    std::get<Uuid>(b.at("OwnerID")) != agent ||
                                    std::get<Uuid>(b.at("LastOwnerID")) != agent ||
                                    std::get<std::uint64_t>(b.at("BaseMask")) != 0x9e000 ||
                                    std::get<std::uint64_t>(b.at("OwnerMask")) != 0x9e000 ||
                                    std::get<std::uint64_t>(b.at("NextOwnerMask")) != 0x82000 ||
                                    std::get<std::uint64_t>(b.at("GroupMask")) != 0 ||
                                    std::get<std::uint64_t>(b.at("EveryoneMask")) != 0 ||
                                    std::get<std::uint64_t>(b.at("CreationDate")) == 0 ||
                                    std::get<std::uint64_t>(b.at("CreationDate")) % 1000000)
                                    throw std::runtime_error(
                                        "ObjectProperties actual identity/permissions/date/serial mismatch");
                                checked = true;
                            }
                if (!checked)
                    throw std::runtime_error("Script save missed actual object properties");
                if (mode == 1 || mode == 4)
                    wait([&](const auto& list) {
                        std::vector<lludp::Packet> added(list.begin() + std::min(after, list.size()),
                                                         list.end());
                        return chat(added, mode == 1 ? "Saved native" : "Recovered native");
                    });
                if (mode == 3) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    auto list = task_io.snapshot();
                    std::vector<lludp::Packet> added(list.begin() + std::min(after, list.size()), list.end());
                    if (chat(added, "Stopped native should not execute"))
                        throw std::runtime_error("Stopped script executed state_entry");
                }
                first_script_has_instance = mode != 2;
                first_script_running = mode != 3;
                auto filename = tests::check_task_inventory_download(
                    agent, object_id, local, static_cast<std::int16_t>(serial), task_io, verify_script_asset,
                    verify_script_running, 2);
                if (filename == previous_file)
                    throw std::runtime_error("Script asset update retained stale inventory file");
                previous_file = std::move(filename);
            }
            auto count_recovered = [&]() {
                auto list = task_io.snapshot();
                std::set<std::uint32_t> ids;
                for (const auto& p : list)
                    if (chat(std::vector<lludp::Packet>{p}, "Recovered native"))
                        ids.insert(p.sequence);
                return ids.size();
            };
            auto query_running = [&] {
                auto p = send("GetScriptRunning");
                p.block("Script")["ObjectID"] = object_id;
                p.block("Script")["ItemID"] = first_script;
                transmit(p);
                verify_script_running(object_id, first_script);
            };
            auto set_running = [&](bool value) {
                auto p = send("SetScriptRunning");
                p.block("Script")["ObjectID"] = object_id;
                p.block("Script")["ItemID"] = first_script;
                p.block("Script")["Running"] = value;
                transmit(p);
                first_script_running = value;
                query_running();
            };
            auto reset_script = [&] {
                auto p = send("ScriptReset");
                p.block("Script")["ObjectID"] = object_id;
                p.block("Script")["ItemID"] = first_script;
                transmit(p);
                query_running();
            };
            auto recovered = count_recovered();
            set_running(false);
            reset_script();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            if (count_recovered() != recovered)
                throw std::runtime_error("Reset incorrectly started stopped script");
            set_running(true);
            wait([&](const auto& list) {
                std::set<std::uint32_t> ids;
                for (const auto& p : list)
                    if (chat(std::vector<lludp::Packet>{p}, "Recovered native"))
                        ids.insert(p.sequence);
                return ids.size() > recovered;
            });
            if (count_recovered() != recovered + 1)
                throw std::runtime_error("Start after stopped reset replayed state entry");
            recovered = count_recovered();
            reset_script();
            wait([&](const auto& list) {
                std::set<std::uint32_t> ids;
                for (const auto& p : list)
                    if (chat(std::vector<lludp::Packet>{p}, "Recovered native"))
                        ids.insert(p.sequence);
                return ids.size() > recovered;
            });
            auto unchanged = tests::check_task_inventory_download(
                agent, object_id, local, 6, task_io, verify_script_asset, verify_script_running, 2);
            if (unchanged != previous_file)
                throw std::runtime_error(
                    "Script start/stop/reset mutated task inventory serial or asset cache");
            std::cout
                << "Verified viewer stop/start/reset, stopped reset state_entry and unchanged inventory serial\n";
        }
        if (argc > 9 && std::string_view(argv[9]).size())
            tests::run_inventory_script_rez_probe(agent, object_id, local, std::filesystem::path(argv[9]),
                                                  task_io);
        if (has_touch && std::string_view(argv[6]) == "Rotating fixture") {
            const auto wait_sale = [&](std::size_t start, std::uint64_t type, std::int64_t price) {
                wait([&](const auto& list) {
                    for (auto i = start; i < list.size(); ++i) {
                        if (list[i].spec->name == "AgentAlertMessage") {
                            const auto& bytes =
                                std::get<std::vector<std::uint8_t>>(list[i].block("AlertData").at("Message"));
                            throw std::runtime_error(
                                "Sale alert: " +
                                std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)));
                        }
                        if (list[i].spec->name != "ObjectProperties")
                            continue;
                        for (const auto& part : list[i].blocks.at("ObjectData"))
                            if (std::get<Uuid>(part.at("ObjectID")) == object_id &&
                                std::get<Uuid>(part.at("OwnerID")) == agent &&
                                std::get<std::uint64_t>(part.at("SaleType")) == type &&
                                std::get<std::int64_t>(part.at("SalePrice")) == price)
                                return true;
                    }
                    return false;
                });
            };
            auto sale = send("ObjectSaleInfo");
            auto listing = lludp::make_block(sale.spec->blocks.at(1));
            listing["LocalID"] = std::uint64_t(local);
            listing["SaleType"] = std::uint64_t{1};
            listing["SalePrice"] = std::int64_t{0};
            sale.blocks["ObjectData"].push_back(std::move(listing));
            auto start = task_io.snapshot().size();
            sale.block("ObjectData")["SaleType"] = std::uint64_t{2};
            transmit(sale);
            wait_sale(start, 2, 0);
            std::cout << "Verified object copy listing\n";
            auto copy_buy = send("ObjectBuy");
            auto copy_purchase = lludp::make_block(copy_buy.spec->blocks.at(1));
            copy_purchase["ObjectLocalID"] = std::uint64_t(local);
            copy_purchase["SaleType"] = std::uint64_t{2};
            copy_purchase["SalePrice"] = std::int64_t{0};
            copy_buy.blocks["ObjectData"].push_back(std::move(copy_purchase));
            start = task_io.snapshot().size();
            transmit(copy_buy);
            Uuid purchased_item;
            wait([&](const auto& list) {
                for (auto i = start; i < list.size(); ++i) {
                    if (list[i].spec->name == "AgentAlertMessage") {
                        const auto& bytes =
                            std::get<std::vector<std::uint8_t>>(list[i].block("AlertData").at("Message"));
                        throw std::runtime_error(
                            "Copy purchase alert: " +
                            std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)));
                    }
                    if (list[i].spec->name != "UpdateCreateInventoryItem")
                        continue;
                    for (const auto& item : list[i].blocks.at("InventoryData"))
                        if (std::get<Uuid>(item.at("OwnerID")) == agent &&
                            std::get<std::int64_t>(item.at("Type")) == 6 &&
                            !std::get<Uuid>(item.at("AssetID")).is_zero()) {
                            purchased_item = std::get<Uuid>(item.at("ItemID"));
                            std::cout << "Purchased object asset " << std::get<Uuid>(item.at("AssetID")).str()
                                      << ' ' << purchased_item.str() << std::endl;
                            return true;
                        }
                }
                return false;
            });
            sale.sequence = sequence++;
            sale.block("ObjectData")["SaleType"] = std::uint64_t{1};
            start = task_io.snapshot().size();
            transmit(sale);
            wait_sale(start, 1, 0);
            auto buy = send("ObjectBuy");
            auto purchase = lludp::make_block(buy.spec->blocks.at(1));
            purchase["ObjectLocalID"] = std::uint64_t(local);
            purchase["SaleType"] = std::uint64_t{1};
            purchase["SalePrice"] = std::int64_t{0};
            buy.blocks["ObjectData"].push_back(std::move(purchase));
            start = task_io.snapshot().size();
            transmit(buy);
            wait_sale(start, 0, 10);
            wait([&](const auto& list) {
                bool click_reset = false, owner_event = false;
                for (auto i = start; i < list.size(); ++i) {
                    const auto& packet = list[i];
                    if (packet.spec->name == "ObjectUpdate") {
                        for (const auto& part : packet.blocks.at("ObjectData"))
                            if (std::get<Uuid>(part.at("FullID")) == object_id &&
                                std::get<std::uint64_t>(part.at("ClickAction")) == 0)
                                click_reset = true;
                    } else if (packet.spec->name == "ChatFromSimulator") {
                        const auto& data = packet.block("ChatData");
                        const auto& bytes = std::get<std::vector<std::uint8_t>>(data.at("Message"));
                        if (std::get<Uuid>(data.at("SourceID")) == object_id &&
                            std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                                "Changed 128")
                            owner_event = true;
                    }
                }
                return click_reset && owner_event;
            });
            std::string purchase_check;
            if (!std::getline(std::cin, purchase_check) || purchase_check.empty())
                throw std::runtime_error("Purchased object persistence verification failed");
            const Uuid coalesced_item(purchase_check);
            std::string linked_fixture;
            if (!std::getline(std::cin, linked_fixture) || linked_fixture.empty())
                throw std::runtime_error("Linked object fixture missing");
            auto target = add;
            target.sequence = sequence++;
            target.block("ObjectData")["RayStart"] = std::vector<double>{130, 132, 50};
            target.block("ObjectData")["RayEnd"] = std::vector<double>{130, 132, 30};
            Uuid target_id;
            start = task_io.snapshot().size();
            transmit(target);
            wait([&](const auto& list) {
                for (auto i = start; i < list.size(); ++i) {
                    if (list[i].spec->name != "ObjectUpdate")
                        continue;
                    for (const auto& part : list[i].blocks.at("ObjectData")) {
                        if (std::get<std::uint64_t>(part.at("PCode")) != 9)
                            continue;
                        const auto& bytes = std::get<std::vector<std::uint8_t>>(part.at("ObjectData"));
                        if (bytes.size() >= 12 && real(bytes, 0) == 130.f && real(bytes, 4) == 132.f &&
                            real(bytes, 8) == 30.25f) {
                            target_id = std::get<Uuid>(part.at("FullID"));
                            return true;
                        }
                    }
                }
                return false;
            });
            auto rez_copy = send("RezObject");
            rez_copy.block("InventoryData")["ItemID"] = purchased_item;
            auto& placement = rez_copy.block("RezData");
            placement["RayStart"] = std::vector<double>{130, 132, 50};
            placement["RayEnd"] = std::vector<double>{130, 132, 30};
            placement["RayTargetID"] = target_id;
            placement["RezSelected"] = true;
            Uuid rezzed_copy;
            start = task_io.snapshot().size();
            transmit(rez_copy);
            wait([&](const auto& list) {
                for (auto i = start; i < list.size(); ++i) {
                    if (list[i].spec->name != "ObjectUpdate")
                        continue;
                    for (const auto& part : list[i].blocks.at("ObjectData")) {
                        const auto id = std::get<Uuid>(part.at("FullID"));
                        if (id == object_id || id == target_id ||
                            std::get<std::uint64_t>(part.at("PCode")) != 9)
                            continue;
                        const auto& bytes = std::get<std::vector<std::uint8_t>>(part.at("ObjectData"));
                        if (bytes.size() >= 12) {
                            if (std::abs(real(bytes, 0) - 130.f) < .001f &&
                                std::abs(real(bytes, 4) - 132.f) < .001f &&
                                std::abs(real(bytes, 8) - 32.5f) < .001f) {
                                if (!(std::get<std::uint64_t>(part.at("UpdateFlags")) & 2u))
                                    throw std::runtime_error(
                                        "Inventory rez omitted the create-selected flag");
                                if (std::get<std::vector<std::uint8_t>>(part.at("TextColor")) !=
                                    std::vector<std::uint8_t>{255, 127, 0, 128})
                                    throw std::runtime_error(
                                        "Inventory rez did not restore floating-text color");
                                rezzed_copy = id;
                                return true;
                            }
                            throw std::runtime_error(
                                "Targeted rez position " + std::to_string(real(bytes, 0)) + " " +
                                std::to_string(real(bytes, 4)) + " " + std::to_string(real(bytes, 8)));
                        }
                    }
                }
                return false;
            });
            auto restored_prices = send("RequestPayPrice");
            restored_prices.block("ObjectData")["ObjectID"] = rezzed_copy;
            transmit(restored_prices);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "PayPriceReply" ||
                        std::get<Uuid>(packet.block("ObjectData").at("ObjectID")) != rezzed_copy)
                        continue;
                    const auto& buttons = packet.blocks.at("ButtonData");
                    return buttons.size() == 4 && std::get<std::int64_t>(buttons[0].at("PayButton")) == 5 &&
                           std::get<std::int64_t>(buttons[1].at("PayButton")) == 10;
                }
                return false;
            });
            auto rez_bundle = send("RezObject");
            rez_bundle.block("InventoryData")["ItemID"] = coalesced_item;
            auto& bundle_placement = rez_bundle.block("RezData");
            bundle_placement["RayStart"] = std::vector<double>{140, 142, 50};
            bundle_placement["RayEnd"] = std::vector<double>{140, 142, 30};
            bundle_placement["RayEndIsIntersection"] = true;
            start = task_io.snapshot().size();
            transmit(rez_bundle);
            wait([&](const auto& list) {
                bool left = false, right = false;
                for (auto i = start; i < list.size(); ++i) {
                    if (list[i].spec->name != "ObjectUpdate")
                        continue;
                    for (const auto& part : list[i].blocks.at("ObjectData")) {
                        if (std::get<std::uint64_t>(part.at("PCode")) != 9)
                            continue;
                        const auto& bytes = std::get<std::vector<std::uint8_t>>(part.at("ObjectData"));
                        if (bytes.size() < 12 || real(bytes, 4) != 142.f || real(bytes, 8) != 32.f)
                            continue;
                        left |= real(bytes, 0) == 138.f;
                        right |= real(bytes, 0) == 142.f;
                    }
                }
                return left && right;
            });
            auto rez_linked = rez_bundle;
            rez_linked.sequence = sequence++;
            rez_linked.block("InventoryData")["ItemID"] = Uuid(linked_fixture);
            rez_linked.block("RezData")["RayStart"] = std::vector<double>{134, 134, 50};
            rez_linked.block("RezData")["RayEnd"] = std::vector<double>{134, 134, 30};
            start = task_io.snapshot().size();
            transmit(rez_linked);
            Uuid linked_root;
            std::uint64_t linked_local = 0;
            wait([&](const auto& list) {
                std::uint64_t root_local = 0;
                std::vector<std::uint64_t> child_parents;
                for (auto i = start; i < list.size(); ++i) {
                    if (list[i].spec->name != "ObjectUpdate")
                        continue;
                    for (const auto& part : list[i].blocks.at("ObjectData")) {
                        if (std::get<std::uint64_t>(part.at("PCode")) != 9)
                            continue;
                        const auto& bytes = std::get<std::vector<std::uint8_t>>(part.at("ObjectData"));
                        if (bytes.size() < 12)
                            continue;
                        const auto parent = std::get<std::uint64_t>(part.at("ParentID"));
                        if (!parent && real(bytes, 0) == 134.f && real(bytes, 4) == 134.f &&
                            real(bytes, 8) == 38.f) {
                            root_local = std::get<std::uint64_t>(part.at("ID"));
                            linked_root = std::get<Uuid>(part.at("FullID"));
                            linked_local = root_local;
                        }
                        if (parent && real(bytes, 8) == -6.f) {
                            constexpr std::uint64_t expected_flags = 0x800400u;
                            if ((std::get<std::uint64_t>(part.at("UpdateFlags")) & expected_flags) !=
                                expected_flags)
                                throw std::runtime_error(
                                    "Inventory rez did not restore named primitive flags");
                            child_parents.push_back(parent);
                        }
                    }
                }
                return root_local && std::find(child_parents.begin(), child_parents.end(), root_local) !=
                                         child_parents.end();
            });
            wait([&](const auto& list) {
                bool read = false, mutation = false, updated = false, deleted = false,
                     protected_update = false, reset = false, reset_event = false, keys = false, bulk = false,
                     bulk_event = false, last = false, on_rez = false, inventory_debug = false,
                     inventory_metadata = false, viewer_fps = false, script_fps = false;
                for (auto i = start; i < list.size(); ++i) {
                    if (list[i].spec->name == "SimStats") {
                        const auto& stats = list[i].blocks.at("Stat");
                        constexpr std::uint64_t expected_ids[] = {
                            0,  1,  2,  3,  13, 14, 11, 12, 4,  5,  7,  9,  6,  17, 18, 24, 8,  19, 20,
                            15, 33, 32, 27, 21, 22, 23, 25, 26, 31, 38, 34, 35, 36, 37, 39, 40, 30, 10};
                        if (!std::equal(stats.begin(), stats.end(), std::begin(expected_ids),
                                        std::end(expected_ids), [](const auto& stat, auto id) {
                                            return std::get<std::uint64_t>(stat.at("StatID")) == id;
                                        }))
                            throw std::runtime_error("Unexpected viewer statistics field order");
                        const auto has_stat = [&](std::uint64_t id, double minimum) {
                            return std::any_of(stats.begin(), stats.end(), [&](const auto& stat) {
                                return std::get<std::uint64_t>(stat.at("StatID")) == id &&
                                       std::get<double>(stat.at("StatValue")) >= minimum;
                            });
                        };
                        viewer_fps |= has_stat(1, 0.1) && has_stat(11, 2) && has_stat(13, 1) &&
                                      has_stat(14, 0) && has_stat(15, 2) && has_stat(10, 0.000001) &&
                                      has_stat(17, 0) && has_stat(18, 1) && has_stat(24, 0) &&
                                      has_stat(31, 0) && has_stat(6, 0.001) && has_stat(32, 0) &&
                                      has_stat(33, 0.001);
                    }
                    if (list[i].spec->name != "ChatFromSimulator")
                        continue;
                    const auto& chat = list[i].block("ChatData");
                    const auto& bytes = std::get<std::vector<std::uint8_t>>(chat.at("Message"));
                    const std::string message(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0));
                    if (message == "Stopped script ran")
                        throw std::runtime_error("A script saved as stopped ran on rez");
                    if (std::get<Uuid>(chat.at("SourceID")) != linked_root)
                        continue;
                    if (message == "llGetInventoryDesc: Item Missing description fixture not found") {
                        if (std::get<std::uint64_t>(chat.at("ChatType")) != 6 ||
                            std::get<Uuid>(chat.at("OwnerID")) != agent)
                            throw std::runtime_error(
                                "Inventory description error has incorrect viewer chat metadata");
                        inventory_debug = true;
                    }
                    read |= message == "Linkset data 0/:hello///world/2/262118";
                    script_fps |= message == "Region FPS sampled";
                    inventory_metadata |=
                        message ==
                        "Inventory metadata 12345678-1234-4234-8234-123456789abc/2106-02-07T06:28:15Z/<0.000000, 0.000000, -3.000000>";
                    mutation |= message == "Linkset mutation 0/5/3/0/value";
                    updated |= message == "Linkset event 1/new/value";
                    deleted |= message == "Linkset event 2/secret/";
                    protected_update |= message == "Linkset event 1/hidden/";
                    reset |= message == "Linkset reset 0/4096";
                    reset_event |= message == "Linkset event 0//";
                    keys |= message == "Linkset keys new/hidden/1";
                    bulk |= message == "Linkset bulk 2, 1";
                    bulk_event |= message == "Linkset event 3/public,new/";
                    last |= message == "Linkset last 0/262144";
                    on_rez |= message == "On rez 0/128/00000000-0000-0000-0000-000000000000";
                }
                return read && mutation && updated && deleted && protected_update && reset && reset_event &&
                       keys && bulk && bulk_event && last && on_rez && inventory_debug &&
                       inventory_metadata && viewer_fps && script_fps;
            });
            Uuid permission_script;
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ScriptQuestion") {
                        const auto& data = packet.block("Data");
                        if (std::get<Uuid>(data.at("TaskID")) != linked_root)
                            continue;
                        if (std::get<std::int64_t>(data.at("Questions")) != 32784)
                            throw std::runtime_error("Unexpected script permission request mask");
                        const auto& owner = std::get<std::vector<std::uint8_t>>(data.at("ObjectOwner"));
                        if (std::string(owner.begin(), std::find(owner.begin(), owner.end(), 0)) !=
                            "Native Viewer")
                            throw std::runtime_error("Unexpected owner name in script permission question");
                        permission_script = std::get<Uuid>(data.at("ItemID"));
                        return !permission_script.is_zero();
                    }
                return false;
            });
            auto answer = send("ScriptAnswerYes");
            const Uuid viewer_animation("24242424-2424-4424-8424-242424242424");
            auto viewer_start = send("AgentAnimation");
            auto animation_block = lludp::make_block(viewer_start.spec->blocks.at(1));
            animation_block["AnimID"] = viewer_animation;
            animation_block["StartAnim"] = true;
            viewer_start.blocks["AnimationList"].push_back(std::move(animation_block));
            transmit(viewer_start);
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "AvatarAnimation" &&
                        std::get<Uuid>(packet.block("Sender").at("ID")) == agent) {
                        const auto animations = packet.blocks.find("AnimationList");
                        if (animations != packet.blocks.end())
                            for (const auto& entry : animations->second)
                                if (std::get<Uuid>(entry.at("AnimID")) == viewer_animation)
                                    return true;
                    }
                return false;
            });
            answer.block("Data")["TaskID"] = linked_root;
            answer.block("Data")["ItemID"] = permission_script;
            answer.block("Data")["Questions"] = std::int64_t{32816};
            transmit(answer);
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ChatFromSimulator") {
                        const auto& chat = packet.block("ChatData");
                        if (std::get<Uuid>(chat.at("SourceID")) != linked_root)
                            continue;
                        const auto& bytes = std::get<std::vector<std::uint8_t>>(chat.at("Message"));
                        if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                            "Runtime permissions 32784/32912/" + agent.str())
                            return true;
                    }
                return false;
            });
            wait([&](const auto& list) {
                const std::array expected{Uuid("9b29cd61-c45b-5689-ded2-91756b8d76a9"),
                                          Uuid("18181818-1818-4818-8818-181818181818")};
                std::array<bool, 2> started{}, stopped{};
                for (const auto& packet : list) {
                    if (packet.spec->name != "AvatarAnimation" ||
                        std::get<Uuid>(packet.block("Sender").at("ID")) != agent)
                        continue;
                    const auto animations = packet.blocks.find("AnimationList");
                    for (std::size_t i = 0; i < expected.size(); ++i) {
                        const bool present =
                            animations != packet.blocks.end() &&
                            std::any_of(animations->second.begin(), animations->second.end(),
                                        [&](const auto& entry) {
                                            return std::get<Uuid>(entry.at("AnimID")) == expected[i];
                                        });
                        if (present) {
                            if (std::none_of(animations->second.begin(), animations->second.end(),
                                             [&](const auto& entry) {
                                                 return std::get<Uuid>(entry.at("AnimID")) ==
                                                        viewer_animation;
                                             }))
                                throw std::runtime_error("Script animation replaced the viewer animation");
                            const auto sources = packet.blocks.find("AnimationSourceList");
                            if (sources == packet.blocks.end() ||
                                std::none_of(sources->second.begin(), sources->second.end(),
                                             [&](const auto& source) {
                                                 return std::get<Uuid>(source.at("ObjectID")) == linked_root;
                                             }))
                                continue; // A movement override can use the same asset without a script source block.
                            started[i] = true;
                        } else if (started[i])
                            stopped[i] = true;
                    }
                }
                return stopped[0] && stopped[1];
            });
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ChatFromSimulator") {
                        const auto& data = packet.block("ChatData");
                        if (std::get<Uuid>(data.at("SourceID")) != linked_root)
                            continue;
                        const auto& bytes = std::get<std::vector<std::uint8_t>>(data.at("Message"));
                        if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                            "Animation list 2408fe9e-df1d-1d7d-f4ff-1384fa7b350f, " + viewer_animation.str() +
                                ", 9b29cd61-c45b-5689-ded2-91756b8d76a9/Standing/wAlKiNg/")
                            return true;
                    }
                return false;
            });
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ChatFromSimulator") {
                        const auto& data = packet.block("ChatData");
                        if (std::get<Uuid>(data.at("SourceID")) != linked_root)
                            continue;
                        const auto& bytes = std::get<std::vector<std::uint8_t>>(data.at("Message"));
                        if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                            "Override Inventory wave/18181818-1818-4818-8818-181818181818, " +
                                viewer_animation.str())
                            return true;
                    }
                return false;
            });
            wait([&](const auto& list) {
                const Uuid override_id("18181818-1818-4818-8818-181818181818");
                const Uuid standing("2408fe9e-df1d-1d7d-f4ff-1384fa7b350f");
                std::int64_t override_sequence = 0;
                for (const auto& packet : list) {
                    if (packet.spec->name != "AvatarAnimation" ||
                        std::get<Uuid>(packet.block("Sender").at("ID")) != agent)
                        continue;
                    const auto animations = packet.blocks.find("AnimationList");
                    if (animations == packet.blocks.end())
                        continue;
                    const auto sources = packet.blocks.find("AnimationSourceList");
                    if (sources != packet.blocks.end() && !sources->second.empty())
                        continue;
                    for (const auto& entry : animations->second) {
                        const auto id = std::get<Uuid>(entry.at("AnimID"));
                        const auto number = std::get<std::int64_t>(entry.at("AnimSequenceID"));
                        if (id == override_id)
                            override_sequence = number;
                        if (id == standing && override_sequence > 0 && number > override_sequence)
                            return true;
                    }
                }
                return false;
            });
            auto viewer_stop = viewer_start;
            viewer_stop.sequence = sequence++;
            viewer_stop.block("AnimationList")["StartAnim"] = false;
            const auto before_stop = task_io.snapshot().size();
            transmit(viewer_stop);
            wait([&](const auto& list) {
                for (auto i = before_stop; i < list.size(); ++i) {
                    const auto& packet = list[i];
                    if (packet.spec->name != "AvatarAnimation" ||
                        std::get<Uuid>(packet.block("Sender").at("ID")) != agent)
                        continue;
                    const auto animations = packet.blocks.find("AnimationList");
                    if (animations == packet.blocks.end() ||
                        std::none_of(animations->second.begin(), animations->second.end(),
                                     [&](const auto& entry) {
                                         return std::get<Uuid>(entry.at("AnimID")) == viewer_animation;
                                     }))
                        return true;
                }
                return false;
            });
            wait([&](const auto& list) {
                return std::any_of(list.begin(), list.end(), [&](const auto& packet) {
                    if (packet.spec->name != "ChatFromSimulator")
                        return false;
                    const auto& data = packet.block("ChatData");
                    if (std::get<Uuid>(data.at("SourceID")) != linked_root)
                        return false;
                    const auto& bytes = std::get<std::vector<std::uint8_t>>(data.at("Message"));
                    return std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                           "Permission reset 128";
                });
            });
            answer.sequence = sequence++;
            transmit(answer);
            auto check_permissions = send("ObjectGrab");
            check_permissions.block("ObjectData")["LocalID"] = linked_local;
            transmit(check_permissions);
            wait([&](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "ChatFromSimulator") {
                        const auto& data = packet.block("ChatData");
                        if (std::get<Uuid>(data.at("SourceID")) != linked_root)
                            continue;
                        const auto& bytes = std::get<std::vector<std::uint8_t>>(data.at("Message"));
                        if (std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)) ==
                            "Permission after replay 128/0")
                            return true;
                    }
                return false;
            });
            if (std::any_of(argv + 1, argv + argc,
                            [](const char* arg) { return std::string_view(arg) == "--stored-linkset"; })) {
                std::cout << "Linkset backup query " << linked_root.str() << std::endl;
                std::string backup;
                if (!std::getline(std::cin, backup) || backup.size() != 73 || backup[36] != ' ')
                    throw std::runtime_error("Linkset data backup verification failed");
                const Uuid child(backup.substr(0, 36)), item(backup.substr(37));
                auto running = send("GetScriptRunning");
                running.block("Script")["ObjectID"] = child;
                running.block("Script")["ItemID"] = item;
                transmit(running);
                std::cout << "Script running query " << child.str() << ' ' << item.str() << " 0" << std::endl;
                if (!std::getline(std::cin, backup) || backup != "OK")
                    throw std::runtime_error("Rezzed child script did not retain its stopped state");
            }
        }
        if (walk) {
            auto animation = [&](const char* id, std::size_t after = 0) {
                Uuid expected(id);
                wait([&](const auto& list) {
                    for (auto i = after; i < list.size(); ++i) {
                        const auto& p = list[i];
                        if (p.spec->name == "AvatarAnimation" &&
                            std::get<Uuid>(p.block("Sender").at("ID")) == agent)
                            for (const auto& entry : p.blocks.at("AnimationList"))
                                if (std::get<Uuid>(entry.at("AnimID")) == expected &&
                                    std::get<std::int64_t>(entry.at("AnimSequenceID")) > 0)
                                    return true;
                    }
                    return false;
                });
            };
            animation("2408fe9e-df1d-1d7d-f4ff-1384fa7b350f");
            struct Motion {
                float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, pitch = 0;
            };
            auto motion = [&](const auto& p) -> std::optional<Motion> {
                if (p.spec->name != "ObjectUpdate")
                    return {};
                for (auto& b : p.blocks.at("ObjectData"))
                    if (std::get<Uuid>(b.at("FullID")) == agent &&
                        std::get<std::uint64_t>(b.at("PCode")) == 47) {
                        auto& bytes = std::get<std::vector<std::uint8_t>>(b.at("ObjectData"));
                        return Motion{real(bytes, 16), real(bytes, 20), real(bytes, 24), real(bytes, 28),
                                      real(bytes, 32), real(bytes, 36), real(bytes, 52)};
                    }
                return {};
            };
            auto mark = [&] {
                std::lock_guard lock(mutex);
                return packets.size();
            };
            auto latest = [&]() {
                std::lock_guard lock(mutex);
                for (auto p = packets.rbegin(); p != packets.rend(); ++p)
                    if (auto m = motion(*p))
                        return *m;
                throw std::runtime_error("Missing physics avatar");
            };
            auto control = [&](std::uint32_t flags, bool turned = false, bool pitched = false) {
                auto p = send("AgentUpdate");
                auto& b = p.block("AgentData");
                b["BodyRotation"] = pitched  ? std::vector<double>{.5, 0, 0, .8660254}
                                    : turned ? std::vector<double>{0, 0, .70710677, .70710677}
                                             : std::vector<double>{0, 0, 0, 1};
                b["HeadRotation"] = b["BodyRotation"];
                b["CameraAtAxis"] = std::vector<double>{1, 0, 0};
                b["ControlFlags"] = std::uint64_t(flags);
                transmit(p);
            };
            auto observe = [&](std::size_t after, auto predicate) {
                wait([&](const auto& list) {
                    for (auto i = after; i < list.size(); ++i)
                        if (auto m = motion(list[i]); m && predicate(*m))
                            return true;
                    return false;
                });
            };
            auto after = mark();
            control(0);
            observe(after, [](const Motion& m) { return m.z > 21 && m.z < 23 && std::abs(m.vz) < .1f; });
            auto ground = latest();
            const Uuid overlay("24242424-2424-4424-8424-242424242424");
            auto overlay_packet = send("AgentAnimation");
            auto overlay_block = lludp::make_block(overlay_packet.spec->blocks.at(1));
            overlay_block["AnimID"] = overlay;
            overlay_block["StartAnim"] = true;
            overlay_packet.blocks["AnimationList"].push_back(std::move(overlay_block));
            transmit(overlay_packet);
            animation("24242424-2424-4424-8424-242424242424");
            after = mark();
            control(1u | 0x400u);
            observe(after, [&](const Motion& m) {
                return m.x > ground.x + 1.2f && std::abs(m.y - ground.y) < .15f && m.z > 21;
            });
            animation("6ed24bd8-91aa-4b12-ccc7-c97c857ab4e0", after);
            auto forward = latest();
            wait([&](const auto& list) {
                const Uuid walking("6ed24bd8-91aa-4b12-ccc7-c97c857ab4e0");
                for (auto i = after; i < list.size(); ++i) {
                    const auto& packet = list[i];
                    if (packet.spec->name != "AvatarAnimation" ||
                        std::get<Uuid>(packet.block("Sender").at("ID")) != agent)
                        continue;
                    bool has_overlay = false, has_walk = false;
                    for (const auto& entry : packet.blocks.at("AnimationList")) {
                        const auto id = std::get<Uuid>(entry.at("AnimID"));
                        has_overlay |= id == overlay;
                        has_walk |= id == walking;
                    }
                    if (has_walk && has_overlay)
                        return true;
                }
                return false;
            });
            auto stop_walking = overlay_packet;
            stop_walking.sequence = sequence++;
            stop_walking.block("AnimationList")["AnimID"] = Uuid("6ed24bd8-91aa-4b12-ccc7-c97c857ab4e0");
            stop_walking.block("AnimationList")["StartAnim"] = false;
            const auto before_default_stop = mark();
            transmit(stop_walking);
            wait([&](const auto& list) {
                const Uuid standing("2408fe9e-df1d-1d7d-f4ff-1384fa7b350f");
                const Uuid walking("6ed24bd8-91aa-4b12-ccc7-c97c857ab4e0");
                for (auto i = before_default_stop; i < list.size(); ++i) {
                    const auto& packet = list[i];
                    if (packet.spec->name != "AvatarAnimation" ||
                        std::get<Uuid>(packet.block("Sender").at("ID")) != agent)
                        continue;
                    bool has_overlay = false, has_stand = false, has_walk = false;
                    for (const auto& entry : packet.blocks.at("AnimationList")) {
                        const auto id = std::get<Uuid>(entry.at("AnimID"));
                        has_overlay |= id == overlay;
                        has_stand |= id == standing;
                        has_walk |= id == walking;
                    }
                    if (has_overlay && has_stand && !has_walk)
                        return true;
                }
                return false;
            });
            overlay_packet.sequence = sequence++;
            overlay_packet.block("AnimationList")["StartAnim"] = false;
            transmit(overlay_packet);
            after = mark();
            control(1u | 0x400u, true);
            observe(after, [&](const Motion& m) { return m.y > forward.y + 1.0f; });
            auto run = send("SetAlwaysRun");
            run.block("AgentData")["AlwaysRun"] = true;
            transmit(run);
            after = mark();
            control(1u | 0x400u, true);
            observe(after, [](const Motion& m) { return m.vy > 4.f; });
            animation("05ddbff8-aaa9-92a1-2b74-8fe77a29b445", after);
            after = mark();
            control(0, true);
            observe(after, [](const Motion& m) {
                return std::abs(m.vx) < .1f && std::abs(m.vy) < .1f && std::abs(m.vz) < .1f;
            });
            ground = latest();
            after = mark();
            control(0x10u | 0x8000u, true);
            observe(after, [&](const Motion& m) { return m.z > ground.z + 1.f; });
            animation("2305bd75-1ca9-b03b-1faa-b176b8a8c49e", after);
            control(0, true);
            after = mark();
            observe(after, [&](const Motion& m) { return m.z < ground.z + .3f && std::abs(m.vz) < .2f; });
            ground = latest();
            if (block_flight) {
                after = mark();
                control(0x2000u | 1u | 0x400u, false, true);
                observe(after, [&](const Motion& m) { return m.x > ground.x + 0.5f; });
                if (std::abs(latest().pitch) > 0.01f)
                    throw std::runtime_error("Avatar update retained flight-only pitch with flight blocked");
                if (std::abs(latest().z - ground.z) > .3f)
                    throw std::runtime_error("Avatar left the ground with flight blocked");
                const Uuid hover("4ae8016b-31b9-03bb-c401-b1ea941db41d");
                const auto samples = task_io.snapshot();
                for (auto i = after; i < samples.size(); ++i) {
                    const auto& packet = samples[i];
                    if (packet.spec->name != "AvatarAnimation" ||
                        std::get<Uuid>(packet.block("Sender").at("ID")) != agent)
                        continue;
                    for (const auto& entry : packet.blocks.at("AnimationList"))
                        if (std::get<Uuid>(entry.at("AnimID")) == hover)
                            throw std::runtime_error("Avatar entered hover with flight blocked");
                }
                control(0);
                after = mark();
                control(0x2000u | 0x10u | 0x1000u, false, true);
                observe(after, [&](const Motion& m) { return m.z > ground.z + 0.5f; });
                if (std::abs(latest().pitch) > 0.01f)
                    throw std::runtime_error("Ascent controls activated flight despite BlockFly");
                control(0);
            } else {
                after = mark();
                control(0x2000u | 0x10u | 0x1000u, true);
                observe(after, [&](const Motion& m) { return m.z > ground.z + 2.f; });
                control(0x2000u, true);
                after = mark();
                observe(after,
                        [&](const Motion& m) { return m.z > ground.z + 1.f && std::abs(m.vz) < .15f; });
                animation("4ae8016b-31b9-03bb-c401-b1ea941db41d", after);
                after = mark();
                control(0);
                observe(after, [](const Motion& m) { return m.z > 21 && m.z < 23 && std::abs(m.vz) < .1f; });
            }
            std::uint64_t region_handle = 0;
            {
                std::lock_guard lock(mutex);
                for (const auto& packet : packets)
                    if (packet.spec->name == "AgentMovementComplete") {
                        region_handle = std::get<std::uint64_t>(packet.block("Data").at("RegionHandle"));
                        break;
                    }
            }
            auto teleport = send("TeleportLocationRequest");
            auto& info = teleport.block("Info");
            info["RegionHandle"] = region_handle;
            info["Position"] = std::vector<double>{120, 121, 45};
            info["LookAt"] = std::vector<double>{1, 0, 0};
            after = mark();
            transmit(teleport);
            observe(after, [](const Motion& m) {
                return std::abs(m.x - 120) < .15f && std::abs(m.y - 121) < .15f && m.z < 44 && m.z > 20;
            });
            wait([](const auto& list) {
                for (const auto& packet : list)
                    if (packet.spec->name == "SimStats") {
                        double simulator = 0., physics = 0., frame_ms = 0., physics_ms = 0.,
                               physics_step_ms = 0., agent_updates = 0., agent_ms = 0.;
                        for (const auto& stat : packet.blocks.at("Stat")) {
                            const auto id = std::get<std::uint64_t>(stat.at("StatID"));
                            const auto value = std::get<double>(stat.at("StatValue"));
                            if (id == 1)
                                simulator = value;
                            if (id == 2)
                                physics = value;
                            if (id == 3)
                                agent_updates = value;
                            if (id == 4)
                                frame_ms = value;
                            if (id == 7)
                                physics_ms = value;
                            if (id == 8)
                                agent_ms = value;
                            if (id == 27)
                                physics_step_ms = value;
                        }
                        if (physics > 0. && physics <= simulator && physics_ms > 0. &&
                            frame_ms >= physics_ms && physics_step_ms == 20. && agent_updates > 0. &&
                            agent_ms > 0. && std::abs(1000. / frame_ms - simulator) < 0.06)
                            return true;
                    }
                return false;
            });
            std::cout
                << "Verified native walking, yaw turn, running, stopping, jump, flight, hover, animations and local teleport physics\n";
            if (std::any_of(argv, argv + argc, [](const char* arg) {
                    return std::string_view(arg) == "--verify-estate-debug";
                })) {
                const auto set_physics_disabled = [&](bool disabled) {
                    auto request = send("EstateOwnerMessage");
                    const std::string method = "setregiondebug";
                    request.block("MethodData")["Method"] = std::vector<std::uint8_t>(method.begin(), method.end());
                    for (const auto parameter : {'N', 'N', disabled ? 'Y' : 'N'}) {
                        auto entry = lludp::make_block(request.spec->blocks[2]);
                        entry["Parameter"] = std::vector<std::uint8_t>{static_cast<std::uint8_t>(parameter), 0};
                        request.blocks["ParamList"].push_back(std::move(entry));
                    }
                    const auto after = mark();
                    transmit(request);
                    transmit(send("RequestRegionInfo"));
                    wait([&](const auto& list) {
                        for (auto i = after; i < list.size(); ++i) {
                            const auto& packet = list[i];
                            if (packet.spec->name == "RegionInfo" &&
                                ((std::get<std::uint64_t>(packet.block("RegionInfo").at("RegionFlags")) &
                                  0x4000u) != 0) == disabled)
                                return true;
                        }
                        return false;
                    });
                    wait([&](const auto& list) {
                        for (auto i = after; i < list.size(); ++i) {
                            const auto& packet = list[i];
                            if (packet.spec->name != "SimStats" ||
                                ((std::get<std::uint64_t>(packet.block("Region").at("RegionFlags")) &
                                  0x4000u) != 0) != disabled)
                                continue;
                            for (const auto& stat : packet.blocks.at("Stat"))
                                if (std::get<std::uint64_t>(stat.at("StatID")) == 2 &&
                                    (std::get<double>(stat.at("StatValue")) == 0.) == disabled)
                                    return true;
                        }
                        return false;
                    });
                };
                set_physics_disabled(true);
                set_physics_disabled(false);
            }
        }
        if (profiles_disabled && !advanced_profiles) {
            std::lock_guard lock(mutex);
            if (has(packets, "AvatarPropertiesReply") || has(packets, "AvatarInterestsReply"))
                throw std::runtime_error("Disabled basic profile module sent replies");
            std::cout << "PASS: configured UserProfiles disables basic replies" << std::endl;
        }
        if (advanced_profiles) {
            auto local_message = send("ImprovedInstantMessage");
            auto& message = local_message.block("MessageBlock");
            message["ToAgentID"] = agent;
            message["Message"] = std::vector<std::uint8_t>{'L', 'o', 'c', 'a', 'l', ' ', 'I', 'M', 0};
            transmit(local_message);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "ImprovedInstantMessage")
                        continue;
                    const auto& data = packet.block("MessageBlock");
                    if (std::get<Uuid>(packet.block("AgentData").at("AgentID")) != agent ||
                        std::get<Uuid>(data.at("ToAgentID")) != agent ||
                        std::get<std::vector<std::uint8_t>>(data.at("Message")) !=
                            std::vector<std::uint8_t>{'L', 'o', 'c', 'a', 'l', ' ', 'I', 'M', 0})
                        throw std::runtime_error("Local viewer instant message differs from the request");
                    return true;
                }
                return false;
            });
            transmit(send("RetrieveInstantMessages"));
            const Uuid offline_sender("93939393-9393-4393-8393-939393939393");
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "ImprovedInstantMessage" ||
                        std::get<Uuid>(packet.block("AgentData").at("AgentID")) != offline_sender)
                        continue;
                    const auto& data = packet.block("MessageBlock");
                    if (std::get<Uuid>(data.at("ToAgentID")) != agent ||
                        std::get<std::uint64_t>(data.at("Offline")) != 1 ||
                        std::get<std::vector<std::uint8_t>>(data.at("BinaryBucket")) !=
                            std::vector<std::uint8_t>{0, 1, 255} ||
                        std::get<std::vector<std::uint8_t>>(data.at("Message")) !=
                            std::vector<std::uint8_t>{'S', 't', 'o', 'r', 'e', 'd', ' ', 'I', 'M', 0})
                        throw std::runtime_error("Offline message differs from the service response");
                    return true;
                }
                return false;
            });
            const Uuid offline_recipient("94949494-9494-4494-8494-949494949494");
            auto undelivered = send("ImprovedInstantMessage");
            undelivered.block("MessageBlock")["ToAgentID"] = offline_recipient;
            undelivered.block("MessageBlock")["Message"] = message.at("Message");
            undelivered.block("MessageBlock")["BinaryBucket"] = std::vector<std::uint8_t>{0, 1, 255};
            transmit(undelivered);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "ImprovedInstantMessage" ||
                        std::get<Uuid>(packet.block("AgentData").at("AgentID")) != offline_recipient)
                        continue;
                    const auto& text =
                        std::get<std::vector<std::uint8_t>>(packet.block("MessageBlock").at("Message"));
                    const std::string expected = "User is not logged in. Message saved.";
                    if (text.size() != expected.size() + 1 || text.back() != 0 ||
                        !std::equal(expected.begin(), expected.end(), text.begin()))
                        throw std::runtime_error("Offline storage acknowledgement differs from the original");
                    return true;
                }
                return false;
            });
            const Uuid full_mailbox("95959595-9595-4595-8595-959595959595");
            auto rejected = send("ImprovedInstantMessage");
            rejected.block("MessageBlock")["ToAgentID"] = full_mailbox;
            rejected.block("MessageBlock")["Message"] = message.at("Message");
            transmit(rejected);
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "ImprovedInstantMessage" ||
                        std::get<Uuid>(packet.block("AgentData").at("AgentID")) != full_mailbox)
                        continue;
                    const auto& text =
                        std::get<std::vector<std::uint8_t>>(packet.block("MessageBlock").at("Message"));
                    const std::string expected = "User is not logged in. Message not saved: Mailbox full";
                    if (text.size() != expected.size() + 1 || text.back() != 0 ||
                        !std::equal(expected.begin(), expected.end(), text.begin()))
                        throw std::runtime_error("Offline storage rejection differs from the original");
                    return true;
                }
                return false;
            });
            std::cout << "Ready for grid kick" << std::endl;
            std::string result;
            if (!std::getline(std::cin, result))
                throw std::runtime_error("Missing remote message recipient");
            auto remote_message = send("ImprovedInstantMessage");
            remote_message.block("MessageBlock")["ToAgentID"] = Uuid(result);
            remote_message.block("MessageBlock")["Message"] = message.at("Message");
            transmit(remote_message);
            if (!std::getline(std::cin, result) || result != "OK")
                throw std::runtime_error("Grid kick verification failed");
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "ImprovedInstantMessage")
                        continue;
                    if (std::get<Uuid>(packet.block("AgentData").at("AgentID")) !=
                        Uuid("6571e388-6218-4574-87db-f9379718315e"))
                        continue;
                    const auto& data = packet.block("MessageBlock");
                    const auto& text = std::get<std::vector<std::uint8_t>>(data.at("Message"));
                    const std::string expected("Grid message\0", 13);
                    if (std::get<Uuid>(data.at("ToAgentID")) != agent ||
                        std::get<std::uint64_t>(data.at("Dialog")) != 0 ||
                        std::get<std::vector<std::uint8_t>>(data.at("BinaryBucket")) !=
                            std::vector<std::uint8_t>{0, 1, 255} ||
                        !std::equal(text.begin(), text.end(), expected.begin(), expected.end()))
                        throw std::runtime_error("Grid instant message packet differs from the request");
                    return true;
                }
                return false;
            });
            wait([&](const auto& list) {
                for (const auto& packet : list) {
                    if (packet.spec->name != "KickUser")
                        continue;
                    const auto& user = packet.block("UserInfo");
                    const auto& reason = std::get<std::vector<std::uint8_t>>(user.at("Reason"));
                    const std::string expected("New login detected\0", 19);
                    if (std::get<Uuid>(user.at("AgentID")) != agent ||
                        std::get<Uuid>(user.at("SessionID")) != session ||
                        !std::equal(reason.begin(), reason.end(), expected.begin(), expected.end()))
                        throw std::runtime_error(
                            "Grid kick packet differs from original viewer notification");
                    return true;
                }
                return false;
            });
        } else {
            for (int arg = 11; arg < argc; ++arg) {
                const std::string_view option(argv[arg]);
                constexpr std::string_view prefix = "--udp-texture=";
                if (!option.starts_with(prefix))
                    continue;
                std::ifstream input(std::filesystem::u8path(option.substr(prefix.size())), std::ios::binary);
                const std::vector<std::uint8_t> expected{std::istreambuf_iterator<char>(input), {}};
                if (expected.size() <= 600)
                    throw std::runtime_error("Missing multi-packet texture fixture");
                const Uuid image("c228d1cf-4b5d-4ba8-84f4-899a0796aa97");
                auto request = send("RequestImage");
                auto row = lludp::make_block(request.spec->blocks.at(1));
                row["Image"] = image;
                row["Packet"] = std::uint64_t{1};
                row["DownloadPriority"] = 1.0;
                request.blocks["RequestImage"].push_back(std::move(row));
                transmit(request);
                const auto part_count = 2 + (expected.size() - 601) / 1000;
                wait([&](const auto& received) {
                    std::vector<const std::vector<std::uint8_t>*> parts(part_count);
                    for (const auto& reply : received) {
                        const bool header = reply.spec->name == "ImageData";
                        if (!header && reply.spec->name != "ImagePacket")
                            continue;
                        const auto& identity = reply.block("ImageID");
                        if (std::get<Uuid>(identity.at("ID")) != image)
                            continue;
                        if (header && (std::get<std::uint64_t>(identity.at("Size")) != expected.size() ||
                                       std::get<std::uint64_t>(identity.at("Packets")) != part_count ||
                                       std::get<std::uint64_t>(identity.at("Codec")) != 2))
                            throw std::runtime_error("UDP texture header differs from the source image");
                        const auto index = header ? 0 : std::get<std::uint64_t>(identity.at("Packet"));
                        if (index >= part_count)
                            throw std::runtime_error("UDP texture part number out of range");
                        parts[index] =
                            &std::get<std::vector<std::uint8_t>>(reply.block("ImageData").at("Data"));
                    }
                    if (std::find(parts.begin(), parts.end(), nullptr) != parts.end())
                        return false;
                    std::vector<std::uint8_t> assembled;
                    for (const auto* part : parts)
                        assembled.insert(assembled.end(), part->begin(), part->end());
                    if (assembled != expected)
                        throw std::runtime_error("UDP texture bytes differ from the source image");
                    return true;
                });
            }
            if (std::any_of(argv + std::min(argc, 11), argv + argc,
                            [](const char* arg) { return std::string_view(arg) == "--verify-denied-home"; })) {
                auto request = send("SetStartLocationRequest");
                auto& location = request.block("StartLocationData");
                location["LocationID"] = std::uint64_t{1};
                location["LocationPos"] = std::vector<double>{128, 128, 60};
                location["LocationLookAt"] = std::vector<double>{0, 1, 0};
                transmit(request);
                wait([&](const auto& replies) {
                    return std::any_of(replies.begin(), replies.end(), [&](const auto& reply) {
                        if (reply.spec->name != "AgentAlertMessage" ||
                            std::get<Uuid>(reply.block("AgentData").at("AgentID")) != agent)
                            return false;
                        const auto& message =
                            std::get<std::vector<std::uint8_t>>(reply.block("AlertData").at("Message"));
                        return std::string(message.begin(), std::find(message.begin(), message.end(), 0)) ==
                               "/You are not allowed to set your home location in this parcel.";
                    });
                });
            }
            if (std::any_of(argv + std::min(argc, 11), argv + argc,
                            [](const char* arg) { return std::string_view(arg) == "--verify-mute-list"; })) {
                const Uuid target("395a3b2b-259e-46fc-9d41-fb4c4404419e");
                const std::string mute_name = "Mut\xc3\xa9";
                std::vector<std::uint8_t> name(mute_name.begin(), mute_name.end());
                name.push_back(0);
                const auto has_empty_mute_list = [](const auto& list) {
                    for (const auto& reply : list) {
                        if (reply.spec->name != "GenericMessage")
                            continue;
                        const auto& method =
                            std::get<std::vector<std::uint8_t>>(reply.block("MethodData").at("Method"));
                        if (std::string(method.begin(), std::find(method.begin(), method.end(), 0)) ==
                            "emptymutelist")
                            return true;
                    }
                    return false;
                };
                const auto update_mute = [&](Uuid id) {
                    auto update = send("UpdateMuteListEntry");
                    auto& entry = update.block("MuteData");
                    entry["MuteID"] = id;
                    entry["MuteName"] = name;
                    entry["MuteType"] = std::int64_t{1};
                    entry["MuteFlags"] = std::uint64_t{3};
                    transmit(update);
                };
                if (std::any_of(argv + std::min(argc, 11), argv + argc,
                                [](const char* arg) { return std::string_view(arg) == "--verify-mute-owner"; })) {
                    update_mute(Uuid("8f677659-aecd-4a0c-bfa2-8b8953a18aba"));
                    wait(has_empty_mute_list);
                }
                update_mute(target);
                transmit(send("MuteListRequest"));
                std::vector<std::uint8_t> filename;
                wait([&](const auto& list) {
                    for (const auto& reply : list) {
                        if (reply.spec->name != "MuteListUpdate")
                            continue;
                        const auto& fields = reply.block("MuteData");
                        if (std::get<Uuid>(fields.at("AgentID")) != agent)
                            continue;
                        filename = std::get<std::vector<std::uint8_t>>(fields.at("Filename"));
                        return true;
                    }
                    return false;
                });
                auto file = send("RequestXfer");
                constexpr std::uint64_t xfer = 0x5d4799a861174eccull;
                file.block("XferID")["ID"] = xfer;
                file.block("XferID")["Filename"] = filename;
                transmit(file);
                const std::string expected = "1 " + target.str() + " " + mute_name + "|3\n";
                wait([&](const auto& list) {
                    for (const auto& reply : list) {
                        if (reply.spec->name != "SendXferPacket" ||
                            std::get<std::uint64_t>(reply.block("XferID").at("ID")) != xfer)
                            continue;
                        const auto& data =
                            std::get<std::vector<std::uint8_t>>(reply.block("DataPacket").at("Data"));
                        if (data.size() != expected.size() + 4 ||
                            std::string(data.begin() + 4, data.end()) != expected)
                            throw std::runtime_error("Mute-list Xfer bytes differ from the stored entry");
                        return true;
                    }
                    return false;
                });
                auto cached = send("MuteListRequest");
                cached.block("MuteData")["MuteCRC"] = std::uint64_t{Crc32::compute(
                    {reinterpret_cast<const std::uint8_t*>(expected.data()), expected.size()})};
                transmit(cached);
                wait([&](const auto& list) { return has(list, "UseCachedMuteList"); });
                auto remove = send("RemoveMuteListEntry");
                remove.block("MuteData")["MuteID"] = target;
                remove.block("MuteData")["MuteName"] = name;
                transmit(remove);
                transmit(send("MuteListRequest"));
                wait(has_empty_mute_list);
            }
            auto logout = send("LogoutRequest");
            transmit(logout);
            wait([&](const auto& p) { return has(p, "LogoutReply"); });
        }
        socket.stop();
        std::cout << "PASS: connected region handshake, root transition, avatar, " << expected.size()
                  << " terrain packets, primitive creation/edit, native LSL execution and logout\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
