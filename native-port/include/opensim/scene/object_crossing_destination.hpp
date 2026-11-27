// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/grid_region.hpp"
#include "opensim/framework/vector_text.hpp"
#include <functional>
#include <optional>

namespace opensim::scene {
using ObjectDestinationLookup = std::function<std::optional<services::GridRegion>(std::int32_t, std::int32_t)>;
// EntityTransferModule.GetObjectDestination coordinate and arrival behavior.
std::optional<services::GridRegion> object_crossing_destination(bool scene_available, std::int32_t source_x,
                                                                std::int32_t source_y, framework::Vector3 target,
                                                                const ObjectDestinationLookup &lookup,
                                                                framework::Vector3 &arrival);
} // namespace opensim::scene
