// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/framework/nini_xml_text_writer.hpp"
#include "opensim/framework/task_inventory_item.hpp"
#include "opensim/scene/world.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <filesystem>

namespace pugi { class xml_node; }

namespace opensim::scene {

struct ManagedScriptState {
    struct Timer {
        std::int64_t interval_ticks;
        std::int64_t remaining_ticks;
    };
    struct PermissionGrant {
        framework::Uuid granter;
        std::int32_t mask;
    };

    std::optional<Timer> timer;
    std::optional<std::int64_t> elapsed_ticks;
    std::optional<double> minimum_event_delay;
    std::optional<bool> running;
    std::optional<PermissionGrant> local_permission_grant;
    std::vector<script::LslSensorQuery> sensors;
    std::vector<script::LslListenerCheckpoint> listeners;
    std::vector<script::LslEvent> events;
    std::optional<script::YEngineSnapshot> snapshot;
    std::string source_hash;

    bool empty() const {
        return !timer && !elapsed_ticks && !minimum_event_delay && !running && sensors.empty() && listeners.empty() &&
               events.empty() && !snapshot;
    }
};

struct ObjectAssetPart {
    SceneObject object;
    std::uint32_t inventory_serial = 0;
    std::vector<framework::TaskInventoryItem> inventory;
    framework::Vector3 attachment_position;
    std::uint8_t last_attachment_point = 0;
    std::map<framework::Uuid, ManagedScriptState> managed_script_states;
    std::map<framework::Uuid, script::LslCheckpoint> native_script_checkpoints;
};

// Children are supplied in link number order, with transforms relative to root.
void write_object_asset_xml(framework::NativeXmlTextWriter& writer, const ObjectAssetPart& root,
                           std::span<const ObjectAssetPart> children);
void write_crossing_object_xml(framework::NativeXmlTextWriter& writer, const ObjectAssetPart& root,
                               std::span<const ObjectAssetPart> children);

struct ObjectAssetLinkset {
    framework::Vector3 offset;
    // The first part is the root.
    std::vector<ObjectAssetPart> parts;
};

void write_coalesced_object_asset_xml(framework::NativeXmlTextWriter& writer,
                                     std::span<const ObjectAssetLinkset> linksets);

struct ObjectAsset {
    framework::Vector3 bounds;
    std::vector<ObjectAssetLinkset> linksets;
    bool coalesced = false;
};

std::string write_prim_inertia_xml(const framework::PrimInertiaOverride& inertia);
std::optional<framework::PrimInertiaOverride> read_prim_inertia_xml(std::string_view xml);

ManagedScriptState read_managed_script_state(const pugi::xml_node& state);
std::optional<ManagedScriptState> read_yengine_state_file(const std::filesystem::path& directory,
                                                        framework::Uuid item, framework::Uuid asset);

std::optional<ObjectAsset> read_object_asset_xml(std::span<const std::uint8_t> bytes);

}
