// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/parcel_prim_limits.hpp"
#include <algorithm>
#include <bit>

namespace opensim::scene {
ParcelPrimLimitDenial parcel_prim_limit_check(std::int32_t added, framework::Uuid owner, std::int32_t simulator_prims,
                                              std::int32_t capacity, std::int32_t user_prims,
                                              std::int32_t maximum_per_user, framework::Uuid parcel_owner,
                                              bool group_owned, framework::Uuid estate_owner,
                                              std::span<const framework::Uuid> estate_managers) {
    const auto add = [](std::int32_t a, std::int32_t b) {
        return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) + std::bit_cast<std::uint32_t>(b));
    };
    if (add(added, simulator_prims) > capacity)
        return ParcelPrimLimitDenial::ParcelFull;
    if (maximum_per_user >= 0 && (owner != parcel_owner || group_owned) && owner != estate_owner &&
        std::find(estate_managers.begin(), estate_managers.end(), owner) == estate_managers.end() &&
        add(user_prims, added) > maximum_per_user)
        return ParcelPrimLimitDenial::UserLimit;
    return ParcelPrimLimitDenial::None;
}
} // namespace opensim::scene
