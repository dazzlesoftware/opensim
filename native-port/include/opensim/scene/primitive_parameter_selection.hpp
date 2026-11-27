// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/world.hpp"
#include "opensim/script/primitive_parameter_query.hpp"

namespace opensim::scene {
// Return sitting avatars in their group's sitting order, not UUID or local-ID
// order. RegionRuntime supplies its live linkset_sitters provider.
using PrimitiveSittingAvatarProvider = std::function<std::vector<framework::Uuid>(framework::Uuid)>;

std::vector<framework::Uuid> select_primitive_parameter_entities(const World &world,
                                                                 const PrimitiveSittingAvatarProvider &sitting_avatars,
                                                                 framework::Uuid host, std::int32_t link,
                                                                 script::PrimitiveQuerySelection selection);
} // namespace opensim::scene
