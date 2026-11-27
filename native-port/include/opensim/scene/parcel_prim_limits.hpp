// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <cstdint>
#include <span>

namespace opensim::scene {
enum class ParcelPrimLimitDenial { None, ParcelFull, UserLimit };
// PrimLimitsModule.DoCommonChecks, with already sampled parcel/count data.
ParcelPrimLimitDenial parcel_prim_limit_check(std::int32_t added, framework::Uuid owner, std::int32_t simulator_prims,
                                              std::int32_t capacity, std::int32_t user_prims,
                                              std::int32_t maximum_per_user, framework::Uuid parcel_owner,
                                              bool group_owned, framework::Uuid estate_owner,
                                              std::span<const framework::Uuid> estate_managers);
} // namespace opensim::scene
