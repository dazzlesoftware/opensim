// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/framework/nini_xml_text_writer.hpp"
#include "opensim/framework/task_inventory_item.hpp"
#include "opensim/scene/world.hpp"

namespace opensim::scene {
struct ObjectAssetPart {
 SceneObject object;
 std::uint32_t inventory_serial=0;
 std::vector<framework::TaskInventoryItem> inventory;
};
// Children are supplied in link number order, with transforms relative to root.
void write_object_asset_xml(framework::NativeXmlTextWriter&,const ObjectAssetPart& root,
                            std::span<const ObjectAssetPart> children);
struct ObjectAssetLinkset {
 framework::Vector3 offset;
 // The first part is the root.
 std::vector<ObjectAssetPart> parts;
};
struct ObjectAsset {
 framework::Vector3 bounds;
 std::vector<ObjectAssetLinkset> linksets;
 bool coalesced=false;
};
std::optional<ObjectAsset> read_object_asset_xml(std::span<const std::uint8_t>);
}
