// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/rpc.hpp"
#include "opensim/services/caps.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include <chrono>
#include <any>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <pugixml.hpp>

using namespace opensim;
using namespace framework;
using namespace scene;

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Could not read original runtime continuation fixture");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path &path, const std::string &bytes) {
    std::ofstream stream(path, std::ios::binary);
    check(bool(stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))),
          "Could not write isolated metadata cache");
}

void asset_roundtrip(const SceneObject &object, const TaskInventoryItem &item, const std::string &xml,
                     const std::string &hash, const std::string &snapshot) {
    pugi::xml_document document;
    check(bool(document.load_buffer(xml.data(), xml.size())), "Original managed XML did not parse");
    ObjectAssetPart part;
    part.object = object;
    part.inventory.push_back(item);
    part.managed_script_states.emplace(item.item_id, read_managed_script_state(document.document_element()));
    std::u16string output;
    XmlTextWriterIo io;
    io.encoding = [] { return std::shared_ptr<NativeXmlTextEncoding>{}; };
    io.write = [&](auto, auto text) {
        if (text)
            output.append(*text);
    };
    io.write_array = [&](auto text, int offset, int count) { output.append(text->data() + offset, count); };
    io.write_whole_array = [&](auto text) { output.append(text->data(), text->size()); };
    NativeXmlTextWriter writer(std::move(io));
    write_object_asset_xml(writer, part, {});
    const auto encoded = utf8(output);
    check(encoded.find("GroupScriptStates") != encoded.npos, "Object export omitted deferred original script state");
    const auto restored =
        read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(encoded.data()), encoded.size()});
    check(restored && restored->linksets.size() == 1, "Deferred script object asset did not round-trip");
    const auto &states = restored->linksets.front().parts.front().managed_script_states;
    const auto found = states.find(item.item_id);
    check(found != states.end() && found->second.source_hash == hash && found->second.snapshot &&
              !found->second.snapshot->managed_frames.empty() &&
              found->second.original_xml.find(snapshot) != std::string::npos,
          "Object asset round-trip lost the original snapshot/hash/opaque managed state");
}

void deferred_controls(RegionRuntime &runtime, services::GridRegion &region, services::PresenceService &presence,
                       const SceneObject &object, const TaskInventoryItem &item, bool reset = true) {
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
    auto circuit = std::make_shared<AgentCircuitData>();
    circuit->agent_id = object.owner;
    circuit->session_id = Uuid::random();
    circuit->secure_session_id = Uuid::random();
    circuit->circuit_code = 42;
    circuit->caps_path = Uuid::random().str();
    circuit->first_name = "Deferred";
    circuit->last_name = "Owner";
    circuit->start_position = {20, 20, 30};
    presence.login(object.owner.str(), circuit->session_id, circuit->secure_session_id);
    services::LoginTransferContext context;
    std::string reason;
    check(runtime.create_agent(nullptr, region, circuit, 128, context, reason),
          "Could not reserve deferred script owner");
    std::uint32_t sequence = 0;
    const auto send = [&](network::lludp::Packet packet) {
        packet.sequence = ++sequence;
        runtime.viewers().receive(endpoint, network::lludp::encode_datagram(packet));
    };
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
    auto use = network::lludp::make_packet("UseCircuitCode");
    use.block("CircuitCode")["Code"] = std::uint64_t{42};
    use.block("CircuitCode")["ID"] = object.owner;
    use.block("CircuitCode")["SessionID"] = circuit->session_id;
    send(std::move(use));
    wait([&] { return runtime.viewers().agent(object.owner).has_value(); }, "Deferred owner did not authenticate");
    auto movement = network::lludp::make_packet("CompleteAgentMovement");
    movement.block("AgentData")["AgentID"] = object.owner;
    movement.block("AgentData")["SessionID"] = circuit->session_id;
    movement.block("AgentData")["CircuitCode"] = std::uint64_t{42};
    send(std::move(movement));
    wait([&] { return runtime.viewers().agent(object.owner)->root; }, "Deferred owner did not complete arrival");
    const auto command = [&](const char *name, std::optional<bool> running = {}) {
        auto packet = network::lludp::make_packet(name);
        packet.block("AgentData")["AgentID"] = object.owner;
        packet.block("AgentData")["SessionID"] = circuit->session_id;
        packet.block("Script")["ObjectID"] = object.id;
        packet.block("Script")["ItemID"] = item.item_id;
        if (running)
            packet.block("Script")["Running"] = *running;
        send(std::move(packet));
    };
    const auto query_running = [&](bool expected) {
        check(runtime.capabilities() != nullptr, "Deferred script owner has no capability module");
        const auto caps = std::dynamic_pointer_cast<services::Caps>(
            runtime.capabilities()->capabilities().get_caps_for_user(circuit->circuit_code));
        std::shared_ptr<services::CapabilityPollHandler> event_queue;
        check(caps && caps->try_get_poll("EventQueueGet", event_queue), "Deferred script owner has no EventQueueGet");
        // GetScriptRunning has no AgentData block. Authentication comes from
        // the already admitted UDP circuit and its bound viewer endpoint.
        auto query = network::lludp::make_packet("GetScriptRunning");
        query.block("Script")["ObjectID"] = object.id;
        query.block("Script")["ItemID"] = item.item_id;
        send(std::move(query));
        const auto poll_id = Uuid::random();
        wait(
            [&] {
                if (!event_queue->has_events(poll_id, object.owner))
                    return false;
                const auto reply = event_queue->get_events(poll_id, object.owner);
                if (!reply || !reply->contains("bin_response_data"))
                    return false;
                const auto bytes = std::any_cast<server::PollBytes>(reply->get("bin_response_data"));
                check(bool(bytes), "Script-running event queue returned no LLSD body");
                const auto events = network::parse_llsd_xml(std::string(bytes->begin(), bytes->end()));
                for (const auto &event : events.get("events").array) {
                    if (event.get("message").text != "ScriptRunningReply")
                        continue;
                    const auto &scripts = event.get("body").get("Script").array;
                    check(scripts.size() == 1, "Script-running event did not contain exactly one script record");
                    const auto &script = scripts.front();
                    if (network::osd_uuid(script.get("ObjectID")) != object.id ||
                        network::osd_uuid(script.get("ItemID")) != item.item_id)
                        continue;
                    check(network::osd_boolean(script.get("Running")) == expected &&
                              network::osd_boolean(script.get("Mono")),
                          "Deferred GetScriptRunning exposed the wrong retained Running/Mono status");
                    return true;
                }
                return false;
            },
            "Authenticated deferred GetScriptRunning did not produce its observable event-queue reply");
    };
    const auto baseline = runtime.snapshot_task_script_states(object.id);
    const auto initial = baseline.find(item.item_id);
    check(initial != baseline.end(), "Concurrent snapshot fixture has no retained initial state");
    const std::string original_xml(initial->second.begin(), initial->second.end());
    const auto snapshot_begin = original_xml.find("<Snapshot>");
    const auto snapshot_end = original_xml.find("</Snapshot>");
    check(snapshot_begin != original_xml.npos && snapshot_end > snapshot_begin,
          "Concurrent snapshot fixture has no original managed payload");
    const auto original_snapshot = original_xml.substr(snapshot_begin, snapshot_end + 11 - snapshot_begin);
    const auto hash_begin = original_xml.find("SourceHash=\"");
    check(hash_begin != original_xml.npos && hash_begin + 45 <= original_xml.size(),
          "Concurrent snapshot fixture has no original source hash");
    const auto original_hash = original_xml.substr(hash_begin, 45);
    std::atomic<unsigned> samples = 0;
    std::atomic<bool> saw_stopped = false, saw_started_after_stop = false, snapshot_failed = false;
    std::mutex failure_mutex;
    std::string snapshot_error;
    std::jthread snapshots([&](std::stop_token stop) {
        try {
            while (!stop.stop_requested()) {
                const auto states = runtime.snapshot_task_script_states(object.id);
                const auto found = states.find(item.item_id);
                check(found != states.end() && !script::decode_lsl_checkpoint(found->second),
                      "Concurrent snapshot lost opaque state or replaced it with native execution");
                const std::string xml(found->second.begin(), found->second.end());
                check(xml.find("<ScriptState") != xml.npos && xml.find(original_snapshot) != xml.npos &&
                          xml.find(item.asset_id.str()) != xml.npos && xml.find(original_hash) != xml.npos,
                      "Concurrent snapshot observed incomplete original managed XML");
                if (xml.find("<Running>False</Running>") != xml.npos)
                    saw_stopped = true;
                else {
                    check(xml.find("<Running>True</Running>") != xml.npos,
                          "Concurrent snapshot observed an invalid retained Running value");
                    if (saw_stopped.load())
                        saw_started_after_stop = true;
                }
                ++samples;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch (const std::exception &error) {
            std::lock_guard lock(failure_mutex);
            snapshot_error = error.what();
            snapshot_failed = true;
        } catch (...) {
            std::lock_guard lock(failure_mutex);
            snapshot_error = "Unknown concurrent snapshot failure";
            snapshot_failed = true;
        }
    });
    for (bool running : {false, true}) {
        command("SetScriptRunning", running);
        wait(
            [&] {
                if (snapshot_failed.load())
                    return true;
                const auto states = runtime.snapshot_task_script_states(object.id);
                const auto found = states.find(item.item_id);
                if (found == states.end() || script::decode_lsl_checkpoint(found->second))
                    return false;
                const std::string xml(found->second.begin(), found->second.end());
                return xml.find(running ? "<Running>True</Running>" : "<Running>False</Running>") != xml.npos &&
                       runtime.active_script_count() == 0 &&
                       (running ? saw_started_after_stop.load() : saw_stopped.load());
            },
            "Owner Stop/Start did not update retained XML while leaving deferred script inactive");
        if (snapshot_failed.load())
            break;
        query_running(running);
    }
    snapshots.request_stop();
    snapshots.join();
    {
        std::lock_guard lock(failure_mutex);
        if (snapshot_failed.load())
            throw std::runtime_error(snapshot_error);
    }
    check(samples.load() >= 2 && saw_stopped.load() && saw_started_after_stop.load(),
          "Concurrent snapshot reader did not observe both retained Stop/Start transitions");
    if (reset) {
        command("ScriptReset");
        wait(
            [&] {
                const auto states = runtime.snapshot_task_script_states(object.id);
                const auto found = states.find(item.item_id);
                if (found == states.end())
                    return false;
                const auto native = script::decode_lsl_checkpoint(found->second);
                return native && native->globals.size() == 2 && std::get<std::int32_t>(native->globals[0]) == 10 &&
                       std::get<std::int32_t>(native->globals[1]) == 1 && !native->active_event &&
                       runtime.active_script_count() == 1;
            },
            "Explicit owner ScriptReset did not discard deferred state and execute a fresh native script");
    }
    socket.stop();
}

void exercise(const std::filesystem::path &fixtures, bool split_cache) {
    const auto source = read(fixtures / "pending/source.lsl");
    const auto original_snapshot = read(fixtures / "pending/snapshot.bin");
    const auto original_code = read(fixtures / "pending/original.yobj");
    const auto wrong_code = read(fixtures / "nested/original.yobj");
    const auto metadata = network::parse_osd_json(read(fixtures / "pending/metadata.json"));
    const auto hash = metadata.get("source_hash").text;
    check(hash.size() == 32, "Original runtime continuation source hash unavailable");
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = split_cache ? "Split managed continuation cache" : "Flat managed continuation cache";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto owner = Uuid::random();
    const auto object = SceneObject::primitive(owner, {20, 20, 30});
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Original pending continuation", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = item->parent_part_id = object.id;
    item->asset_id = asset->full_id();
    item->owner_id = item->creator_id = owner;
    item->type = item->inventory_type = 10;
    item->script_running = true;
    const auto state_root =
        std::filesystem::temp_directory_path() / ("opensim-managed-runtime-" + Uuid::random().str());
    const auto directory = state_root / "Yengine" / region.id.str();
    const auto filename = script::yengine_object_cache_key(source, asset->full_id()) + ".yobj";
    check(filename.size() > 5, "Original source metadata cache key is empty");
    const auto cache_file = split_cache ? directory / filename.substr(0, 1) / filename.substr(1) : directory / filename;
    std::filesystem::create_directories(cache_file.parent_path());

    struct Cleanup {
        std::filesystem::path file, region_directory, state_root;
        bool split;

        ~Cleanup() {
            // Remove only explicitly created files/directories; never recurse
            // through a shared workspace, application data or simulator cache.
            std::error_code error;
            std::filesystem::remove(file, error);
            if (split)
                std::filesystem::remove(file.parent_path(), error);
            std::filesystem::remove(region_directory, error);
            std::filesystem::remove(region_directory.parent_path(), error);
            std::filesystem::remove(state_root, error);
        }
    } cleanup{cache_file, directory, state_root, split_cache};

    const auto binary = std::make_shared<std::vector<std::uint8_t>>(original_snapshot.begin(), original_snapshot.end());
    const auto encoded_snapshot = network::binary_base64(network::osd_from_binary(binary));
    const auto xml = "<ScriptState Engine=\"YEngine\" Asset=\"" + asset->full_id().str() + "\" SourceHash=\"" + hash +
                     "\"><Running>True</Running><Snapshot>" + encoded_snapshot + "</Snapshot></ScriptState>";
    asset_roundtrip(object, *item, xml, hash, encoded_snapshot);
    RegionRuntime::TaskScriptStates states;
    states.emplace(item->item_id, std::vector<std::uint8_t>(xml.begin(), xml.end()));
    const auto make_runtime = [&](bool scripts_disabled = false, bool missing_asset = false, bool deny_run = false) {
        RegionPolicies policies;
        policies.access = [](Uuid, const auto &, auto &) { return true; };
        policies.authorize = [](const auto &, auto &) { return true; };
        policies.can_edit = [](Uuid agent, const auto &part) { return agent == part.owner; };
        policies.scripts_disabled = scripts_disabled;
        policies.can_run_script = [deny_run](const auto &, const auto &) { return !deny_run; };
        policies.yengine_state_root = state_root;
        policies.load_asset = [asset, missing_asset](Uuid id) {
            return !missing_asset && id == asset->full_id() ? asset : nullptr;
        };
        policies.schedule_asset_fetch = [](auto action) { action(); };
        auto runtime = std::make_unique<RegionRuntime>(region, presence, std::move(policies));
        runtime->start(std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 9024, 0, std::string("127.0.0.1")}));
        check(runtime->world().add(object, false).has_value(), "Could not restore original script object");
        runtime->restore_task_inventory(object.id, {std::make_shared<TaskInventoryItem>(*item)}, states,
                                        RegionRuntime::TaskInventoryRestoreSource::RegionStart);
        return runtime;
    };
    const auto retained = [&](RegionRuntime &runtime) {
        for (int poll = 0; poll < 8; ++poll)
            runtime.poll();
        const auto current = runtime.snapshot_task_script_states(object.id);
        const auto found = current.find(item->item_id);
        check(found != current.end() && !script::decode_lsl_checkpoint(found->second),
              "Untranslated managed state was discarded or replaced by a fresh native event");
        const std::string retained_xml(found->second.begin(), found->second.end());
        check(retained_xml.find("<ScriptState") != retained_xml.npos &&
                  retained_xml.find(encoded_snapshot) != retained_xml.npos &&
                  retained_xml.find(hash) != retained_xml.npos &&
                  retained_xml.find(asset->full_id().str()) != retained_xml.npos && runtime.active_script_count() == 0,
              "Untranslated managed script did not retain its original XML and stay inactive");
        return current;
    };
    for (int rejection = 0; rejection < 3; ++rejection) {
        auto rejected = make_runtime(rejection == 0, rejection == 1, rejection == 2);
        try {
            const auto preserved = retained(*rejected);
            check(preserved == states, "Early script admission refusal changed the original opaque managed state");
            // Compile admission can be refused while the region's script
            // engine remains enabled; retained running status must reply.
            deferred_controls(*rejected, region, presence, object, *item, false);
            check(retained(*rejected) == preserved,
                  "Repeated snapshots lost managed state after early script admission refusal");
        } catch (const std::exception &error) {
            rejected->stop();
            const char *reason = rejection == 0   ? "scripts_disabled"
                                 : rejection == 1 ? "missing source asset"
                                                  : "can_run_script denied";
            throw std::runtime_error(std::string(reason) + ": " + error.what());
        }
        rejected->stop();
    }
    {
        auto missing = make_runtime();
        states = retained(*missing);
        // Keep the pre-control opaque state for subsequent fresh runtimes;
        // explicit Reset intentionally starts a new script in this instance.
        deferred_controls(*missing, region, presence, object, *item);
        missing->stop();
    }
    write(cache_file, wrong_code);
    {
        auto wrong = make_runtime();
        const auto preserved = retained(*wrong);
        check(preserved == states, "Wrong source metadata changed the retained managed continuation");
        states = preserved;
        wrong->stop();
    }
    write(cache_file, original_code);
    {
        auto restored = make_runtime();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        bool finished = false;
        do {
            restored->poll();
            const auto current = restored->snapshot_task_script_states(object.id);
            if (const auto found = current.find(item->item_id); found != current.end()) {
                if (const auto native = script::decode_lsl_checkpoint(found->second))
                    finished = native->globals.size() == 2 && std::get<std::int32_t>(native->globals[0]) == 10 &&
                               std::get<std::int32_t>(native->globals[1]) == 1 && !native->active_event;
            }
            if (finished)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        check(finished && restored->active_script_count() == 1,
              "Fresh region did not resume the retained original state to result10/count1 without replay");
        const auto inventory = restored->snapshot_task_inventory(object.id);
        check(inventory && inventory->size() == 1 && inventory->front().asset_id == asset->full_id(),
              "Managed continuation admission changed its script asset");
        restored->stop();
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original managed continuation fixture directory required");
        exercise(argv[1], false);
        exercise(argv[1], true);
        std::cout
            << "PASS: managed runtime state remains opaque until verified metadata permits no-replay resumption\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
