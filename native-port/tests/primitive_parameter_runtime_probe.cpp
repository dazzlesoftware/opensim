// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "primitive_parameter_runtime_probe.hpp"
#include "opensim/network/udp_socket.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace scene;

struct PrimitiveErrorViewer::State {
    std::unique_ptr<network::UdpSocket> socket;
    std::mutex mutex;
    std::vector<network::lludp::Packet> packets;
    std::atomic<std::uint32_t> sequence = 1000;

    State() {
        socket = std::make_unique<network::UdpSocket>(
            network::UdpSocketOptions{"127.0.0.1", 0}, [this](const auto &source, auto bytes) {
                auto packet = network::lludp::decode_datagram(bytes);
                if (packet.reliable) {
                    auto ack = network::lludp::make_packet("PacketAck");
                    ack.sequence = sequence++;
                    auto row = network::lludp::make_block(ack.spec->blocks.front());
                    row["ID"] = std::uint64_t{packet.sequence};
                    ack.blocks["Packets"].push_back(std::move(row));
                    socket->send_to(source, network::lludp::encode_datagram(ack));
                }
                std::lock_guard lock(mutex);
                packets.push_back(std::move(packet));
            });
    }

    ~State() {
        socket->stop();
    }
};

PrimitiveErrorViewer::PrimitiveErrorViewer() : state_(std::make_unique<State>()) {}

PrimitiveErrorViewer::~PrimitiveErrorViewer() = default;

network::UdpEndpoint PrimitiveErrorViewer::start() {
    return state_->socket->start();
}

bool PrimitiveErrorViewer::saw_debug(Uuid source, Vector3 position, std::string_view text) const {
    std::lock_guard lock(state_->mutex);
    for (const auto &packet : state_->packets) {
        if (packet.spec->name != "ChatFromSimulator")
            continue;
        const auto &data = packet.block("ChatData");
        if (std::get<Uuid>(data.at("SourceID")) != source || std::get<std::uint64_t>(data.at("ChatType")) != 6)
            continue;
        const auto &bytes = std::get<std::vector<std::uint8_t>>(data.at("Message"));
        std::string_view message(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        if (!message.empty() && message.back() == '\0')
            message.remove_suffix(1);
        const auto &actual = std::get<std::vector<double>>(data.at("Position"));
        if (message == text && actual == std::vector<double>{position.x, position.y, position.z})
            return true;
    }
    return false;
}

void verify_primitive_parameter_runtime(RegionRuntime &runtime,
                                        const std::function<void(std::shared_ptr<AssetBase>)> &store,
                                        const PrimitiveErrorViewer &viewer) {
    auto root = SceneObject::primitive(runtime.descriptor().estate_owner, {227, 213, 30});
    root.name = "Parameter root";
    const auto added = runtime.world().add(root);
    if (!added)
        throw std::runtime_error("Could not create primitive parameter root fixture");
    auto child = SceneObject::primitive(root.owner, {1, 2, 3});
    child.name = "Parameter child";
    child.parent_id = added->local_id;
    if (!runtime.world().add(child))
        throw std::runtime_error("Could not create primitive parameter child fixture");
    constexpr std::string_view source = R"LSL(default {
        state_entry() {
            list own = llGetPrimitiveParams([PRIM_NAME, PRIM_POSITION, PRIM_POS_LOCAL,
                PRIM_LINK_TARGET, LINK_ROOT, PRIM_NAME]);
            list linked = llGetLinkPrimitiveParams(LINK_SET, [PRIM_NAME]);
            list other = osGetPrimitiveParams(llGetLinkKey(LINK_ROOT), [PRIM_NAME]);
            list ossl = osGetLinkPrimitiveParams(LINK_ALL_CHILDREN, [PRIM_NAME]);
            list texture = llGetPrimitiveParams([PRIM_TEXTURE, 0, PRIM_NORMAL, 0, PRIM_ALPHA_MODE, 0]);
            if (llGetListLength(own) == 4 && llList2String(own,0) == "Parameter child" &&
                llList2Vector(own,1) == <228,215,33> && llList2Vector(own,2) == <1,2,3> &&
                llList2String(own,3) == "Parameter root" &&
                llDumpList2String(linked,"|") == "Parameter root|Parameter child" &&
                llList2String(other,0) == "Parameter root" && llList2String(ossl,0) == "Parameter child" &&
                llGetListLength(texture) == 10 && llList2Key(texture,4) == NULL_KEY &&
                llList2Vector(texture,5) == <1,1,0> && llList2Integer(texture,8) == PRIM_ALPHA_MODE_BLEND)
                llSetObjectDesc("Primitive queries verified");
            else llSetObjectDesc("Primitive query mismatch");
        }
    })LSL";
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Primitive getter runtime fixture", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    store(asset);
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = item->parent_part_id = child.id;
    item->owner_id = item->creator_id = root.owner;
    item->asset_id = asset->full_id();
    item->type = item->inventory_type = 10;
    item->script_running = true;
    item->name = "Primitive getter runtime fixture";
    runtime.restore_task_inventory(child.id, {item});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (runtime.world().object(child.id)->description.empty() && std::chrono::steady_clock::now() < deadline) {
        runtime.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto description = runtime.world().object(child.id)->description;
    if (description != "Primitive queries verified")
        throw std::runtime_error("Primitive getter runtime integration: " + description);
    constexpr std::string_view error_source = R"LSL(default { state_entry() {
        list kept = osGetPrimitiveParams(llGetLinkKey(LINK_ROOT),
            [PRIM_NAME, PRIM_LINK_TARGET, <1,2,3>, PRIM_NAME]);
        llSetObjectDesc(llList2String(kept,0));
    } })LSL";
    auto error_asset = std::make_shared<AssetBase>(Uuid::random(), "Primitive getter error fixture", 10, "");
    error_asset->data = std::make_shared<std::vector<std::uint8_t>>(error_source.begin(), error_source.end());
    store(error_asset);
    auto error_item = std::make_shared<TaskInventoryItem>(*item);
    error_item->item_id = Uuid::random();
    error_item->asset_id = error_asset->full_id();
    runtime.restore_task_inventory(child.id, {error_item});
    const auto error_seen = [&] {
        return viewer.saw_debug(child.id, root.position, ": Error PRIM_LINK_TARGET: parameter must be integer");
    };
    const auto error_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while ((!error_seen() || runtime.world().object(child.id)->description != root.name) &&
           std::chrono::steady_clock::now() < error_deadline) {
        runtime.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!error_seen() || runtime.world().object(child.id)->description != root.name)
        throw std::runtime_error(
            "Primitive getter error did not retain its partial result and root-position UDP metadata");
    runtime.world().remove(root.id);
}
