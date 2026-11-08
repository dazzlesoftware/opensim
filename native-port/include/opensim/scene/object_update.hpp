// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/world.hpp"
#include "opensim/scene/avatar_update.hpp"
#include "opensim/network/lludp.hpp"
#include <span>
namespace opensim::scene {
struct ObjectUpdateOptions {
    std::uint32_t pseudo_crc = 0;
    std::uint8_t material = 3, click_action = 0; // SceneObjectPart defaults: Wood, Touch.
    std::optional<std::uint32_t> generated_client_flags;
    framework::Uuid recipient;
    // Host clears its one-shot CreateSelected state after emitting to the owner.
    bool create_selected = false;
    bool disable_facelights = false;
};
struct ObjectTerseMotion {
    framework::Vector3 position, velocity, acceleration, angular_velocity;
    framework::Quaternion rotation;
};
bool object_terse_changed(const ObjectTerseMotion& previous, const ObjectTerseMotion& current,
                          double elapsed_ms);
// PCode.Prim projection of CreatePrimUpdateBlock for world prims and attachments.
// Caller provides permissions-generated flags and source pseudo CRC when known.
network::lludp::Packet object_update(const SceneObject& object, std::uint64_t region_handle, float time_dilation,
                                     const ObjectUpdateOptions& options = {});
network::lludp::Packet object_terse_update(const SceneObject& object, std::uint64_t region_handle);
std::vector<network::lludp::Packet> object_terse_updates(
    std::span<const SceneObject* const> objects, std::uint64_t region_handle,
    std::span<const AvatarUpdate> avatars = {});
}
