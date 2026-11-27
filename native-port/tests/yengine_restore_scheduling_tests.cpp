// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
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
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "Original scheduling fixture missing");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path &path, const std::string &value) {
    std::ofstream file(path, std::ios::binary);
    check(bool(file.write(value.data(), static_cast<std::streamsize>(value.size()))), "Could not provision cache");
}

void exercise(const std::filesystem::path &fixtures, bool replace_task, bool duplicate_restore, bool projection = false,
              bool raw = false, bool stop_before_enable = false, bool full_queue = false) {
    const auto directory = fixtures / (projection ? "pending_changed" : "pending_sleep");
    const auto source = read(directory / "source.lsl");
    const auto original = read(directory / "snapshot.bin");
    const auto metadata = network::parse_osd_json(read(directory / "metadata.json"));
    const auto owner = Uuid::random();
    auto object = SceneObject::primitive(owner, {20, 20, 30});
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Original scheduled continuation", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->owner_id = item->creator_id = owner;
    item->parent_id = item->parent_part_id = object.id;
    item->asset_id = asset->full_id();
    item->type = item->inventory_type = 10;
    item->script_running = true;
    services::NullPresenceStore presence_store;
    services::PresenceService presence(presence_store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "Restore scheduling";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    const auto cache_root =
        std::filesystem::temp_directory_path() / ("opensim-restore-scheduling-" + Uuid::random().str());
    const auto cache_directory = cache_root / "Yengine" / region.id.str();
    std::filesystem::create_directories(cache_directory);
    const auto cache = cache_directory / (script::yengine_object_cache_key(source, asset->full_id()) + ".yobj");

    struct Cleanup {
        std::filesystem::path file;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(file, error);
            auto directory = file.parent_path();
            for (int i = 0; i < 3; ++i) {
                std::filesystem::remove(directory, error);
                directory = directory.parent_path();
            }
        }
    } cleanup{cache};

    if (!raw)
        write(cache, read(directory / "original.yobj"));
    RegionPolicies policies;
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = owner;
    policies.script_parcel_at = [parcel](auto) { return parcel; };
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.can_edit = [owner](Uuid agent, const auto &part) { return agent == owner && part.owner == owner; };
    policies.yengine_state_root = cache_root;
    policies.load_asset = [&asset](Uuid id) { return id == asset->full_id() ? asset : nullptr; };
    policies.store_asset = [&asset](auto stored) { asset = std::move(stored); };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    RegionRuntime runtime(region, presence, std::move(policies));
    runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9024, 0, std::string("127.0.0.1")}));
    runtime.set_scripts_enabled(false);
    const auto binary = std::make_shared<std::vector<std::uint8_t>>(original.begin(), original.end());
    const auto encoded = network::binary_base64(network::osd_from_binary(binary));
    auto xml = "<ScriptState Engine=\"YEngine\" Asset=\"" + asset->full_id().str() + "\" SourceHash=\"" +
               metadata.get("source_hash").text + "\"><Running>True</Running><Snapshot>" + encoded +
               "</Snapshot></ScriptState>";
    if (full_queue) {
        std::string events = "<EventQueue>";
        for (int i = 0; i < 64; ++i)
            events += "<Event Name=\"changed\"><param type=\"System.Int32\">256</param></Event>";
        events += "</EventQueue>";
        xml.insert(xml.find("</ScriptState>"), events);
    }
    RegionRuntime::TaskScriptStates states{{item->item_id, {xml.begin(), xml.end()}}};
    if (projection) {
        pugi::xml_document state;
        check(bool(state.load_buffer(xml.data(), xml.size())), "Could not parse original admission state");
        ObjectAssetPart part;
        part.object = object;
        part.inventory.push_back(*item);
        part.managed_script_states.emplace(item->item_id, read_managed_script_state(state.document_element()));
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
        const auto transfer = std::dynamic_pointer_cast<services::SimulationObject>(
            runtime.get_scene(region.id)->deserialize_object(utf8(output)));
        check(transfer && runtime.create_object(&region, object.position, transfer, true),
              "Normal original crossing admission failed");
    } else {
        check(runtime.world().add(object, false).has_value(), "Could not create scheduled script host");
        runtime.restore_task_inventory(object.id, {item}, states,
                                       RegionRuntime::TaskInventoryRestoreSource::RegionStart);
    }
    if (duplicate_restore)
        runtime.restore_task_inventory(object.id, {item}, states,
                                       RegionRuntime::TaskInventoryRestoreSource::RegionStart);
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
    circuit->agent_id = owner;
    circuit->session_id = Uuid::random();
    circuit->secure_session_id = Uuid::random();
    circuit->circuit_code = 42;
    circuit->caps_path = Uuid::random().str();
    circuit->first_name = "Scheduled";
    circuit->last_name = "Owner";
    circuit->start_position = {20, 20, 30};
    presence.login(owner.str(), circuit->session_id, circuit->secure_session_id);
    services::LoginTransferContext context;
    std::string reason;
    check(runtime.create_agent(nullptr, region, circuit, 128, context, reason), "Could not reserve owner");
    std::uint32_t sequence = 0;
    const auto send = [&](network::lludp::Packet packet) {
        packet.sequence = ++sequence;
        runtime.viewers().receive(endpoint, network::lludp::encode_datagram(packet));
    };
    const auto wait = [&](auto predicate, const char *message, std::chrono::milliseconds duration) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
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
    use.block("CircuitCode")["ID"] = owner;
    use.block("CircuitCode")["SessionID"] = circuit->session_id;
    send(std::move(use));
    wait([&] { return runtime.viewers().agent(owner).has_value(); }, "Owner did not authenticate",
         std::chrono::seconds(4));
    auto movement = network::lludp::make_packet("CompleteAgentMovement");
    movement.block("AgentData")["AgentID"] = owner;
    movement.block("AgentData")["SessionID"] = circuit->session_id;
    movement.block("AgentData")["CircuitCode"] = std::uint64_t{42};
    send(std::move(movement));
    wait([&] { return runtime.viewers().agent(owner)->root; }, "Owner did not arrive", std::chrono::seconds(4));
    const auto checkpoint = [&]() -> std::optional<script::LslCheckpoint> {
        const auto saved = runtime.snapshot_task_script_states(object.id);
        const auto entry = saved.find(item->item_id);
        return entry == saved.end() ? std::nullopt : script::decode_lsl_checkpoint(entry->second);
    };
    if (!duplicate_restore) {
        auto start = network::lludp::make_packet("SetScriptRunning");
        start.block("AgentData")["AgentID"] = owner;
        start.block("AgentData")["SessionID"] = circuit->session_id;
        start.block("Script")["ObjectID"] = object.id;
        start.block("Script")["ItemID"] = item->item_id;
        start.block("Script")["Running"] = true;
        send(std::move(start));
        if (raw) {
            wait(
                [&] {
                    const auto saved = runtime.snapshot_task_script_states(object.id).at(item->item_id);
                    const auto retained = read_yengine_state_xml(saved, item->asset_id);
                    return retained && retained->events.size() == 1;
                },
                "Deferred original constructor notification was not projected", std::chrono::seconds(4));
        } else {
            wait([&] { return checkpoint().has_value(); }, "Owner Start did not translate queued original state",
                 std::chrono::seconds(4));
            const auto translated = checkpoint();
            check(translated && (!translated->function.empty() || !translated->call_stack.empty()),
                  "Disabled region executed or lost original suspended function");
        }
    }
    if (projection) {
        for (int sample = 0; sample < 3; ++sample) {
            const auto saved = runtime.snapshot_task_script_states(object.id).at(item->item_id);
            if (raw) {
                const auto retained = read_yengine_state_xml(saved, item->asset_id);
                check(retained && retained->events.size() == 1 && retained->events.front().name == "changed" &&
                          std::get<std::int32_t>(retained->events.front().arguments.front()) == 256 &&
                          retained->original_xml.find(encoded) != std::string::npos,
                      "Repeated raw projection duplicated notifications or changed original snapshot");
            } else {
                const auto native = script::decode_lsl_checkpoint(saved);
                check(native && native->queued_events.size() == (full_queue ? 64u : 1u) &&
                          native->queued_events.front().name == "changed" &&
                          std::get<std::int32_t>(native->queued_events.front().arguments.front()) == 256,
                      "Repeated native projection duplicated notifications");
            }
        }
        if (stop_before_enable) {
            for (const bool running : {false, true}) {
                auto command = network::lludp::make_packet("SetScriptRunning");
                command.block("AgentData")["AgentID"] = owner;
                command.block("AgentData")["SessionID"] = circuit->session_id;
                command.block("Script")["ObjectID"] = object.id;
                command.block("Script")["ItemID"] = item->item_id;
                command.block("Script")["Running"] = running;
                send(std::move(command));
                wait(
                    [&] {
                        const auto saved = runtime.snapshot_task_script_states(object.id).at(item->item_id);
                        if (const auto native = script::decode_lsl_checkpoint(saved))
                            return native->running == running && native->queued_events.empty();
                        const auto retained = read_yengine_state_xml(saved, item->asset_id);
                        return retained && retained->running == running && retained->events.empty();
                    },
                    "Owner Stop/Start resurrected cleared constructor notifications", std::chrono::seconds(4));
            }
        }
        if (raw)
            write(cache, read(directory / "original.yobj"));
    }
    if (replace_task) {
        // Use the actual script-editor update queue; restore_task_inventory
        // deliberately does not replace an existing inventory registry item.
        const std::string replacement = "integer result;integer count;default{state_entry(){result=99;count=7;}}";
        const auto previous_asset = asset->full_id();
        auto update = std::async(std::launch::async, [&] {
            return runtime.update_task_script(
                owner, item->item_id, object.id, true,
                std::span(reinterpret_cast<const std::uint8_t *>(replacement.data()), replacement.size()));
        });
        try {
            wait([&] { return update.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; },
                 "Normal source update did not complete", std::chrono::seconds(4));
            check(update.get().empty(), "Normal replacement source compilation failed");
            check(asset->full_id() != previous_asset, "Normal source update did not store a new asset");
            const auto updated_inventory = runtime.snapshot_task_inventory(object.id);
            check(updated_inventory && updated_inventory->size() == 1 &&
                      updated_inventory->front().asset_id == asset->full_id(),
                  "Normal source update did not replace the task asset");
        } catch (...) {
            // Resolve any worker waiting on the owner queue before its future
            // destructor joins, including a failure before the job was queued.
            runtime.quiesce_script_updates();
            runtime.stop();
            throw;
        }
    }
    runtime.set_scripts_enabled(true);
    std::string observed = "no native checkpoint";
    try {
        wait(
            [&] {
                const auto completed = checkpoint();
                if (completed) {
                    observed = "function=" + completed->function +
                               " frames=" + std::to_string(completed->call_stack.size()) +
                               " event=" + (completed->active_event ? completed->active_event->name : "none") +
                               " running=" + std::to_string(completed->running) +
                               " sleep=" + std::to_string(completed->sleep_remaining) + " globals=";
                    for (const auto &value : completed->globals)
                        observed += std::holds_alternative<std::int32_t>(value)
                                        ? std::to_string(std::get<std::int32_t>(value)) + ","
                                        : "noninteger,";
                }
                return completed && completed->globals.size() == 2 &&
                       std::get<std::int32_t>(completed->globals[0]) == (replace_task ? 99 : 8) &&
                       std::get<std::int32_t>(completed->globals[1]) == (replace_task                        ? 7
                                                                         : full_queue                        ? 64111
                                                                         : projection && !stop_before_enable ? 1111
                                                                                                             : 111) &&
                       completed->function.empty() && completed->call_stack.empty();
            },
            "Queued restore replayed original entry or ignored newer source", std::chrono::milliseconds(700));
    } catch (const std::exception &error) {
        throw std::runtime_error(std::string(replace_task        ? "newer-source"
                                             : duplicate_restore ? "duplicate-drain"
                                                                 : "owner-start") +
                                 ": " + error.what() + ": " + observed);
    }
    runtime.stop();
}

void opaque_queue_bound() {
    ManagedScriptState state;
    state.original_xml = "<ScriptState opaque=\"preserve\"><Snapshot>original</Snapshot><EventQueue>";
    for (int i = 0; i < 64; ++i)
        state.original_xml += "<Event Name=\"changed\"><UnknownPayload/></Event>";
    state.original_xml += "</EventQueue><UnknownExtension value=\"retained\"/></ScriptState>";
    const std::vector<script::LslEvent> events{{"changed", {std::int32_t{256}}, {}}, {"on_rez", {std::int32_t{0}}, {}}};
    check(append_managed_lifecycle_events(state, events), "Opaque event append failed");
    pugi::xml_document document;
    check(bool(document.load_string(state.original_xml.c_str())), "Projected opaque XML failed to parse");
    std::size_t changed = 0, on_rez = 0;
    for (const auto event : document.document_element().child("EventQueue").children("Event")) {
        changed += std::string_view(event.attribute("Name").value()) == "changed";
        on_rez += std::string_view(event.attribute("Name").value()) == "on_rez";
    }
    check(changed == 64 && on_rez == 1 && state.events.size() == 1 && state.events.front().name == "on_rez" &&
              std::string_view(document.document_element().child_value("Snapshot")) == "original" &&
              std::string_view(document.document_element().child("UnknownExtension").attribute("value").value()) ==
                  "retained",
          "Raw projection exceeded per-event cap or damaged opaque snapshot/extensions");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original fixture directory required");
        opaque_queue_bound();
        exercise(argv[1], false, false);
        exercise(argv[1], false, true);
        exercise(argv[1], true, false);
        exercise(argv[1], false, false, true);
        exercise(argv[1], false, false, true, true);
        exercise(argv[1], false, false, true, false, true);
        exercise(argv[1], false, false, true, true, true);
        exercise(argv[1], false, false, true, false, false, true);
        std::cout << "PASS: authenticated Start compiles queued original restore once and preserves newer admission\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
