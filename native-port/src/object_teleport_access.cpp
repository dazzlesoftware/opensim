// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_teleport_access.hpp"

namespace opensim::scene {
bool object_teleport_source_land_access(framework::Uuid host_owner, framework::Uuid host_group,
                                        const std::optional<ObjectTeleportSourceLand> &land,
                                        const std::function<bool(framework::Uuid)> &estate_manager_or_owner) {
    if (!land || land->owner == host_owner)
        return true;
    if (estate_manager_or_owner && estate_manager_or_owner(host_owner))
        return true;
    return land->group_owned && !land->group.is_zero() && land->group == host_group;
}
} // namespace opensim::scene
