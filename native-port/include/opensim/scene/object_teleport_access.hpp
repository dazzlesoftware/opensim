// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <functional>
#include <optional>

namespace opensim::scene {
struct ObjectTeleportSourceLand {
    framework::Uuid owner, group;
    bool group_owned = false;
};

// OSSL checkAllowObjectTPbyLandOwner, for a target belonging to another owner.
// A missing land object or LandData is represented by nullopt. The estate
// predicate is absent when EstateSettings is null and is evaluated lazily.
bool object_teleport_source_land_access(framework::Uuid host_owner, framework::Uuid host_group,
                                        const std::optional<ObjectTeleportSourceLand> &land,
                                        const std::function<bool(framework::Uuid)> &estate_manager_or_owner);
} // namespace opensim::scene
