// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace scene;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void near(Vector3 actual, Vector3 expected, const char *message) {
    check(std::abs(actual.x - expected.x) < .0002f && std::abs(actual.y - expected.y) < .0002f &&
              std::abs(actual.z - expected.z) < .0002f,
          message);
}

std::optional<int> counter(RegionRuntime &runtime, Uuid part, Uuid item) {
    const auto snapshots = runtime.snapshot_task_script_states(part);
    const auto found = snapshots.find(item);
    if (found == snapshots.end())
        return {};
    const auto checkpoint = script::decode_lsl_checkpoint(found->second);
    if (!checkpoint || !checkpoint->running || checkpoint->globals.size() != 1)
        return {};
    return std::get<std::int32_t>(checkpoint->globals.front());
}

struct WireState {
    network::UdpSocket *socket = nullptr;
    std::mutex mutex;
    std::set<std::uint32_t> acknowledgements;
    std::map<Uuid, std::pair<Uuid, std::int64_t>> permission_questions;
};

void scenario(bool root_selected, bool child_selected) {
    services::NullPresenceStore store;
    services::PresenceService presence(store);
    services::GridRegion region;
    region.id = Uuid::random();
    region.name = "Selected temporary attachment";
    region.external_host = "127.0.0.1";
    region.internal_endpoint = services::RegionEndpoint{"127.0.0.1", 0};
    region.size_x = region.size_y = 256;
    auto circuit = std::make_shared<AgentCircuitData>();
    circuit->agent_id = Uuid::random();
    circuit->session_id = Uuid::random();
    circuit->secure_session_id = Uuid::random();
    circuit->circuit_code = 42;
    circuit->caps_path = Uuid::random().str();
    circuit->first_name = "Selected";
    circuit->last_name = "Wearer";
    circuit->start_position = {100, 100, 30};
    const auto owner = circuit->agent_id;
    auto parcel = std::make_shared<ScriptParcel>();
    parcel->id = Uuid::random();
    parcel->owner = owner;
    auto assets = std::make_shared<std::map<Uuid, std::shared_ptr<AssetBase>>>();
    std::mutex log_mutex;
    std::vector<std::string> logs;
    double now = 1000;
    RegionPolicies policies;
    policies.access = [](Uuid, const auto &, auto &) { return true; };
    policies.authorize = [](const auto &, auto &) { return true; };
    policies.keyframed_motion_clock = [&] { return now; };
    policies.script_parcel_at = [parcel](auto) { return parcel; };
    policies.client_flags = [](Uuid, const auto &part) { return part.flags | 4u; };
    policies.load_asset = [assets](Uuid id) {
        const auto found = assets->find(id);
        return found == assets->end() ? nullptr : found->second;
    };
    policies.schedule_asset_fetch = [](auto action) { action(); };
    policies.log = [&](const std::string &message) {
        std::lock_guard lock(log_mutex);
        logs.push_back(message);
    };
    RegionRuntime runtime(region, presence, std::move(policies));
    const auto endpoint = runtime.start(std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 9023, 0, std::string("127.0.0.1")}));
    std::function<std::string()> timeout_details;
    const auto wait = [&](auto predicate, const char *message) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        do {
            runtime.poll();
            if (predicate())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        std::string details = timeout_details ? timeout_details() : std::string{};
        {
            std::lock_guard lock(log_mutex);
            for (const auto &line : logs)
                details += "\nLOG: " + line;
        }
        throw std::runtime_error(std::string(message) + details);
    };
    auto wire = std::make_shared<WireState>();
    network::UdpSocket socket(network::UdpSocketOptions{}, [wire](const auto &sender, auto bytes) {
        const auto packet = network::lludp::decode_datagram(bytes, 65536, false);
        if (packet.reliable) {
            auto ack = network::lludp::make_packet("PacketAck");
            ack.reliable = false;
            ack.blocks["Packets"].push_back({{"ID", std::uint64_t(packet.sequence)}});
            wire->socket->send_to(sender, network::lludp::encode_datagram(ack));
        }
        std::lock_guard lock(wire->mutex);
        wire->acknowledgements.insert(packet.acknowledgements.begin(), packet.acknowledgements.end());
        if (packet.spec->name == "PacketAck")
            for (const auto &row : packet.blocks.at("Packets"))
                wire->acknowledgements.insert(static_cast<std::uint32_t>(std::get<std::uint64_t>(row.at("ID"))));
        if (packet.spec->name == "ScriptQuestion") {
            const auto &data = packet.block("Data");
            wire->permission_questions.insert_or_assign(
                std::get<Uuid>(data.at("ItemID")),
                std::pair{std::get<Uuid>(data.at("TaskID")), std::get<std::int64_t>(data.at("Questions"))});
        }
    });
    wire->socket = &socket;
    socket.start();
    std::uint32_t sequence = 0;
    const auto send = [&](network::lludp::Packet packet) {
        packet.sequence = ++sequence;
        check(socket.send_to(endpoint, network::lludp::encode_datagram(packet)), "Could not send authenticated packet");
        return packet.sequence;
    };
    presence.login(owner.str(), circuit->session_id, circuit->secure_session_id);
    services::LoginTransferContext context;
    std::string reason;
    check(runtime.create_agent(nullptr, region, circuit, 128, context, reason), "Could not reserve attachment wearer");
    auto use = network::lludp::make_packet("UseCircuitCode");
    use.block("CircuitCode")["Code"] = std::uint64_t{42};
    use.block("CircuitCode")["ID"] = owner;
    use.block("CircuitCode")["SessionID"] = circuit->session_id;
    send(use);
    wait([&] { return runtime.viewers().agent(owner).has_value(); }, "Wearer did not authenticate its UDP circuit");
    auto movement = network::lludp::make_packet("CompleteAgentMovement");
    movement.block("AgentData")["AgentID"] = owner;
    movement.block("AgentData")["SessionID"] = circuit->session_id;
    movement.block("AgentData")["CircuitCode"] = std::uint64_t{42};
    send(movement);
    wait([&] { return runtime.viewers().agent(owner)->root; }, "Wearer did not complete arrival");
    auto ground = SceneObject::primitive(owner, {20, 20, 30});
    ground.last_attachment_point = 5;
    ground.saved_attachment_position = {1, 2, 3};
    const auto root = runtime.world().add(ground, false).value();
    auto child = SceneObject::primitive(owner, {0, 1, 0});
    child.parent_id = root.local_id;
    child = runtime.world().add(child, false).value();
    const auto add_script = [&](Uuid part, const std::string &source) {
        auto asset = std::make_shared<AssetBase>(Uuid::random(), "Selected attach script", 10, "");
        asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
        assets->emplace(asset->full_id(), asset);
        auto item = std::make_shared<TaskInventoryItem>();
        item->item_id = Uuid::random();
        item->parent_id = item->parent_part_id = part;
        item->asset_id = asset->full_id();
        item->owner_id = item->creator_id = owner;
        item->type = item->inventory_type = 10;
        item->script_running = true;
        runtime.restore_task_inventory(part, {item});
        return item->item_id;
    };
    const auto observer =
        add_script(root.id, "integer count; default {moving_start() {count+=100;} moving_end() {count+=1000;}}");
    wait([&] { return counter(runtime, root.id, observer) == 0; }, "Motion observer did not load");
    KeyframedMotionFrame frame;
    frame.position = Vector3{4, 0, 0};
    frame.time_ms = 200;
    check(runtime.install_keyframed_motion(root, script::KeyframedMotionMode::Forward, {frame}),
          "Could not prepare selected ground motion");
    wait([&] { return counter(runtime, root.id, observer) == 100; }, "Ground motion did not start");
    now += 50;
    runtime.poll();
    near(runtime.world().object(root.id)->position, {21, 20, 30}, "Ground motion did not reach its first tick");
    const auto select = [&](const SceneObject &part, bool selected) {
        auto packet = network::lludp::make_packet(selected ? "ObjectSelect" : "ObjectDeselect");
        packet.block("AgentData")["AgentID"] = owner;
        packet.block("AgentData")["SessionID"] = circuit->session_id;
        packet.blocks["ObjectData"].push_back({{"ObjectLocalID", std::uint64_t(part.local_id)}});
        const auto sent = send(std::move(packet));
        wait(
            [&] {
                std::lock_guard lock(wire->mutex);
                return wire->acknowledgements.contains(sent);
            },
            "Viewer selection packet was not acknowledged");
    };
    if (root_selected)
        select(root, true);
    if (child_selected)
        select(child, true);
    check(runtime.world().part_selected(root.id) == root_selected &&
              runtime.world().part_selected(child.id) == child_selected,
          "Viewer selection did not retain the addressed per-part flags");
    check(runtime.world().teleport_group(root.id)->selected && runtime.keyframed_motion_state(root.id)->selected &&
              runtime.keyframed_motion_state(root.id)->timer_stopped,
          "Authenticated part selection did not pause group motion");
    now += 50;
    runtime.poll();
    check(runtime.keyframed_motion_state(root.id)->current.time_ms == 200, "Selected ground motion advanced");
    const auto attach = add_script(
        child.id, "integer done; default {state_entry() {llRequestPermissions(llGetOwner(),PERMISSION_ATTACH);}"
                  "run_time_permissions(integer permissions) {if(permissions & PERMISSION_ATTACH) {"
                  "llAttachToAvatarTemp(ATTACH_LHAND); done=1;}}}");
    timeout_details = [&] {
        std::ostringstream out;
        out << "\nselection scenario root=" << root_selected << " child=" << child_selected << " clock=" << now;
        if (const auto part = runtime.world().object(root.id))
            out << " root.is_attachment=" << part->is_attachment << " root.parent=" << part->parent_id;
        else
            out << " root=missing";
        if (const auto group = runtime.world().teleport_group(root.id))
            out << " group.selected=" << group->selected;
        else
            out << " group=missing";
        out << " root.part_selected=" << runtime.world().part_selected(root.id)
            << " child.part_selected=" << runtime.world().part_selected(child.id);
        if (const auto motion = runtime.keyframed_motion_state(root.id))
            out << " KFM.running=" << motion->running << " KFM.selected=" << motion->selected
                << " KFM.timer_stopped=" << motion->timer_stopped << " KFM.remaining=" << motion->current.time_ms
                << " KFM.frames=" << motion->frames.size();
        else
            out << " KFM=absent";
        const auto observer_count = counter(runtime, root.id, observer);
        out << " observer.count=" << (observer_count ? std::to_string(*observer_count) : "unavailable");
        const auto states = runtime.snapshot_task_script_states(child.id);
        if (const auto found = states.find(attach); found != states.end()) {
            if (const auto state = script::decode_lsl_checkpoint(found->second)) {
                out << " script.running=" << state->running << " script.instruction=" << state->instruction
                    << " script.globals=" << state->globals.size();
                if (state->globals.size() == 1 && std::holds_alternative<std::int32_t>(state->globals.front()))
                    out << " script.done=" << std::get<std::int32_t>(state->globals.front());
                if (state->active_event)
                    out << " script.event=" << state->active_event->name;
                out << " checkpoint.granter=" << state->permissions_granter.str()
                    << " checkpoint.permissions=" << state->permissions_mask;
            } else
                out << " script.checkpoint=invalid";
        } else
            out << " script.checkpoint=absent";
        if (const auto inventory = runtime.snapshot_task_inventory(child.id))
            for (const auto &item : *inventory)
                if (item.item_id == attach)
                    out << " task.running=" << item.script_running << " task.granter=" << item.permissions_granter.str()
                        << " task.permissions=" << item.permissions_mask;
        return out.str();
    };
    wait(
        [&] {
            std::lock_guard lock(wire->mutex);
            const auto found = wire->permission_questions.find(attach);
            return found != wire->permission_questions.end() && found->second.first == child.id &&
                   found->second.second == 0x20;
        },
        "Attachment script did not request viewer permission through the normal entry point");
    check(!runtime.world().object(root.id)->is_attachment && counter(runtime, child.id, attach) == 0,
          "Selected ground object attached before the viewer granted permission");
    auto answer = network::lludp::make_packet("ScriptAnswerYes");
    answer.block("AgentData")["AgentID"] = owner;
    answer.block("AgentData")["SessionID"] = circuit->session_id;
    answer.block("Data")["TaskID"] = child.id;
    answer.block("Data")["ItemID"] = attach;
    answer.block("Data")["Questions"] = std::int64_t{0x20};
    send(std::move(answer));
    wait(
        [&] {
            return counter(runtime, child.id, attach) == 1 && runtime.world().object(root.id)->is_attachment &&
                   counter(runtime, root.id, observer) == 200;
        },
        "Normal selected temporary attachment did not clear group selection and resume motion");
    auto state = runtime.keyframed_motion_state(root.id).value();
    check(!runtime.world().teleport_group(root.id)->selected && !state.selected && !state.timer_stopped &&
              state.running,
          "Nonsilent attach retained selected group motion");
    check(runtime.world().part_selected(root.id) == root_selected &&
              runtime.world().part_selected(child.id) == child_selected,
          "Nonsilent attach cleared individual part selections instead of only the group flag");
    near(state.serialized_position, {100, 100, 30}, "Selected attach rebased before assigning the wearer");
    near(*state.current.position, {103, 100, 30}, "Selected attach did not rebase the active target against wearer");
    near(runtime.world().object(root.id)->position, {1, 2, 3}, "Selected attach changed its chosen offset");
    now += 50;
    runtime.poll();
    near(*runtime.world().object(root.id)->attached_group_position, {101, 100, 30},
         "Resumed selected attachment did not write hidden GroupPosition");

    if (root_selected && child_selected) {
        select(root, false);
        check(!runtime.world().teleport_group(root.id)->selected && !runtime.keyframed_motion_state(root.id)->selected,
              "Deselect after forced group clear rescanned and reselected the still-selected child");
        check(!runtime.world().part_selected(root.id) && runtime.world().part_selected(child.id),
              "Deselecting root after attach changed its still-selected child");
    }
    const auto &still_selected = child_selected ? child : root;
    // The per-part flag remains true across nonsilent attach. Repeated selection
    // must still call PartSelectChanged(true) and reassert the separate group flag.
    select(still_selected, true);
    check(runtime.world().teleport_group(root.id)->selected && runtime.keyframed_motion_state(root.id)->selected &&
              runtime.keyframed_motion_state(root.id)->timer_stopped,
          "Repeated selection of an already-selected attached part did not pause the group");
    now += 50;
    runtime.poll();
    check(runtime.keyframed_motion_state(root.id)->current.time_ms == 150, "Reselected attachment advanced");
    select(still_selected, false);
    wait(
        [&] {
            return !runtime.world().teleport_group(root.id)->selected &&
                   !runtime.keyframed_motion_state(root.id)->selected && counter(runtime, root.id, observer) == 300;
        },
        "Last part deselection did not resume the attachment once");
    now += 50;
    runtime.poll();
    near(*runtime.world().object(root.id)->attached_group_position, {101.5f, 100, 30},
         "Deselected attachment did not retain its active target and cursor");
    near(runtime.world().object(root.id)->position, {1, 2, 3},
         "Selection changes altered the visible attachment offset");
    socket.stop();
    runtime.stop();
}
} // namespace

int main() {
    try {
        scenario(true, false);
        scenario(false, true);
        scenario(true, true);
        std::cout << "PASS: selected root/child temporary attach and independent part/group selection lifecycle\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
