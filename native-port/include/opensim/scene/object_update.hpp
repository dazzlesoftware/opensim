// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/world.hpp"
#include "opensim/network/lludp.hpp"
namespace opensim::scene {
struct ObjectUpdateOptions {
 std::uint32_t pseudo_crc=0;
 std::uint8_t material=3,click_action=0; // SceneObjectPart defaults: Wood, Touch.
 std::optional<std::uint32_t> generated_client_flags;
 framework::Uuid recipient;
 // Host clears its one-shot CreateSelected state after emitting to the owner.
 bool create_selected=false;
};
// Plain, single-root PCode.Prim projection of active CreatePrimUpdateBlock.
// Caller provides permissions-generated flags and source pseudo CRC when known.
// Attachments/vegetation require their own object models and are rejected here.
network::lludp::Packet object_update(const SceneObject& object,std::uint64_t region_handle,
 float time_dilation,const ObjectUpdateOptions& options={});
}
