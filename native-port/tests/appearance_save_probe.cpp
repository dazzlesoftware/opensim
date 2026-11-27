// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "appearance_save_probe.hpp"
#include "opensim/script/asset_text.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace opensim;
using namespace framework;
using namespace scene;
using namespace services;

void verify_script_appearance_saves(RegionRuntime &runtime, IPresenceService &presence, const RegionPolicies &policies,
                                    const std::shared_ptr<AgentCircuitData> &viewer,
                                    const std::vector<Uuid> &persisted_bakes) {
    const auto previous = runtime.viewers().agent(viewer->agent_id)->appearance();
    auto appearance = std::make_shared<AvatarAppearance>(*previous);
    const auto texture = Uuid::random();
    appearance->texture->create_face(8)->texture_id(texture);
    appearance->set_attachment(2, Uuid::random(), Uuid::random());
    appearance->set_attachment(31, Uuid::random(), Uuid::random());
    runtime.viewers().update_appearance(viewer->agent_id, viewer->session_id, appearance);

    auto npc = std::make_shared<AgentCircuitData>();
    npc->agent_id = Uuid::random();
    npc->session_id = Uuid::random();
    npc->appearance = std::make_shared<AvatarAppearance>(*appearance);
    npc->start_position = {226, 215, 30};
    if (!runtime.viewers().add_npc(npc, Uuid::random()))
        throw std::runtime_error("Could not create the appearance-saving NPC fixture");

    auto child = std::make_shared<AgentCircuitData>();
    child->agent_id = Uuid::random();
    child->session_id = Uuid::random();
    child->secure_session_id = Uuid::random();
    child->circuit_code = 86222;
    child->caps_path = "appearance-saving-child";
    child->child = true;
    child->neighbour_child = true;
    child->appearance = std::make_shared<AvatarAppearance>(*appearance);
    child->start_position = {227, 215, 30};
    presence.login(child->agent_id.str(), child->session_id, child->secure_session_id);
    LoginTransferContext transfer;
    std::string reason;
    auto destination = runtime.descriptor();
    if (!runtime.create_agent(nullptr, destination, child, 0, transfer, reason))
        throw std::runtime_error("Could not reserve the appearance-saving child fixture: " + reason);
    auto connect = network::lludp::make_packet("UseCircuitCode");
    connect.sequence = 1;
    connect.block("CircuitCode")["Code"] = std::uint64_t{child->circuit_code};
    connect.block("CircuitCode")["ID"] = child->agent_id;
    connect.block("CircuitCode")["SessionID"] = child->session_id;
    runtime.viewers().receive({"127.0.0.1", 8623}, network::lludp::encode_datagram(connect));
    const auto admitted_child = runtime.viewers().agent(child->agent_id);
    if (!admitted_child || admitted_child->root)
        throw std::runtime_error("Appearance-saving child fixture is not a child presence");

    const auto host = SceneObject::primitive(viewer->agent_id, {228, 215, 30});
    if (!runtime.world().add(host))
        throw std::runtime_error("Could not rez the appearance-saving script host");
    const auto source = "default { state_entry() { "
                        "key original = osOwnerSaveAppearance(\"Owner outfit\"); "
                        "key replacement = osOwnerSaveAppearance(\"Owner outfit\", FALSE); "
                        "key withHuds = osOwnerSaveAppearance(\"Owner with HUDs\", TRUE); "
                        "key agent = osAgentSaveAppearance(\"" +
                        npc->agent_id.str() +
                        "\", \"Agent outfit\"); "
                        "if (original == NULL_KEY || replacement == NULL_KEY || original == replacement || "
                        "withHuds == NULL_KEY || agent == NULL_KEY || "
                        "osAgentSaveAppearance(\"invalid\", \"Invalid\") != NULL_KEY || "
                        "osAgentSaveAppearance(NULL_KEY, \"Missing\") != NULL_KEY || "
                        "osAgentSaveAppearance(\"" +
                        child->agent_id.str() +
                        "\", \"Child\") != NULL_KEY) "
                        "llSetObjectDesc(\"Appearance save returned an unexpected key\"); "
                        "else llSetObjectDesc(\"Appearance saves complete\"); } }";
    auto asset = std::make_shared<AssetBase>(Uuid::random(), "Appearance-saving fixture", 10, "");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(source.begin(), source.end());
    policies.store_asset(asset);
    auto item = std::make_shared<TaskInventoryItem>();
    item->item_id = Uuid::random();
    item->parent_id = item->parent_part_id = host.id;
    item->owner_id = item->creator_id = host.owner;
    item->asset_id = asset->full_id();
    item->type = item->inventory_type = 10;
    item->script_running = true;
    item->name = "Appearance-saving fixture";
    runtime.restore_task_inventory(host.id, {item});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (runtime.world().object(host.id)->description.empty() && std::chrono::steady_clock::now() < deadline) {
        runtime.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (runtime.world().object(host.id)->description != "Appearance saves complete")
        throw std::runtime_error("Appearance-saving script failed: " + runtime.world().object(host.id)->description);
    const auto inventory = runtime.snapshot_task_inventory(host.id);
    if (!inventory || inventory->size() != 4)
        throw std::runtime_error("Appearance saves did not replace the named notecard or retained a rejected save");
    for (const auto &card : *inventory) {
        if (card.item_id == item->item_id)
            continue;
        if (card.owner_id != viewer->agent_id || card.description != "Avatar Appearance" ||
            card.type != static_cast<int>(AssetType::Notecard))
            throw std::runtime_error("Saved appearance notecard has incorrect inventory metadata");
        const auto saved = policies.load_asset(card.asset_id);
        if (!saved || !saved->data)
            throw std::runtime_error("Saved appearance notecard has no stored asset");
        const auto lines = script::notecard_lines(saved->data);
        std::string xml;
        for (const auto &line : lines)
            xml += line + "\n";
        const auto packed = network::parse_llsd_xml(xml);
        const auto &attachments = packed.get("attachments").array;
        const auto expected = card.name == "Owner outfit" ? 1u : 2u;
        if (attachments.size() != expected || packed.get("textures").array.at(8).text != texture.str())
            throw std::runtime_error("Appearance notecard lost its baked texture or HUD filtering");
    }
    if (std::count(persisted_bakes.begin(), persisted_bakes.end(), texture) != 4)
        throw std::runtime_error("Appearance saving did not persist the baked texture before each save");
    runtime.close_agent_session(child->agent_id, child->session_id.str());
    runtime.viewers().remove_npc(npc->agent_id);
    runtime.viewers().update_appearance(viewer->agent_id, viewer->session_id,
                                        std::make_shared<AvatarAppearance>(*previous));
    runtime.world().remove(host.id);
}
