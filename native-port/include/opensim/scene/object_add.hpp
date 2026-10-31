// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/world.hpp"
#include "opensim/network/lludp.hpp"
namespace opensim::scene {
struct ObjectAddRequest {
 SceneObject object;
 framework::Vector3 ray_start,ray_end;
 framework::Uuid ray_target;
 std::uint8_t bypass_raycast=0,ray_end_is_intersection=0;
 std::uint32_t add_flags=0;
};
// LLClientView.GetShapeFromAddPacket and HandleObjectAdd projection.
ObjectAddRequest object_add_request(const network::lludp::Packet& packet);
}
