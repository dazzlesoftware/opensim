// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/vector_text.hpp"
#include <functional>

namespace opensim::scene {
struct GroupPositionState {
    framework::Vector3 position, sandbox_origin;
    bool scene = true, attached = false, loading = false, disable_transfer = false;
    bool in_transit = false, sandboxed = false, duplicating = false, deleted = false, changed_events = false;
};

struct GroupPositionHost {
    std::function<bool(framework::Vector3)> inside;
    std::function<void(framework::Vector3)> schedule_crossing, write_positions;
    std::function<void()> disable_physics, sandbox_debug, move_sitters, changed_position, taint_parcel_counts;
};

// SceneObjectGroup.AbsolutePosition setter. Scene, part/physics, crossing and
// script callbacks are supplied by the owner; this is not a script move command.
void set_group_position(GroupPositionState &, framework::Vector3, const GroupPositionHost &);
} // namespace opensim::scene
