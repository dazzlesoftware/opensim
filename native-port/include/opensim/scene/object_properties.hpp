// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/lludp.hpp"
namespace opensim::scene {
// Source CreateObjectPropertiesBlock input. Permission/sale fields belong to
// root; identity, dates, inventory serial and strings belong to requested part.
// Defaults match new SceneObjectPart(owner,shape,...), except creation_date
// must be captured as signed Unix seconds at actual rez time by the scene.
struct ObjectPropertiesData {
 framework::Uuid id,creator,owner,group,last_owner,from_inventory_item;
 std::int32_t creation_date=0;
 std::uint32_t base_mask=0x9e000,owner_mask=0x9e000,group_mask=0,everyone_mask=0,next_owner_mask=0x82000;
 std::uint8_t sale_type=0;
 std::int32_t sale_price=0;
 std::uint32_t category=0,inventory_serial=0;
 std::string name="Object",description,touch_name,sit_name;
};
// One source full-properties block. Send on Task throttle, without automatic
// packet splitting. Host queue/prioritization and multiobject batching remain
// separate. Valid UTF-8 strings use source SDK byte limits (63/127/36/36),
// embedded NUL stops encoding; empty fields have zero length, no extra NUL.
network::lludp::Packet object_properties(const ObjectPropertiesData&);
}
