// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/services/flotsam_cache_console.hpp"
namespace opensim::services {
// OpenSim Application selects unchanged invariant culture on its main/default
// threads. Other cultures keep their explicit console parser backend.
// Both callbacks retain the same source-runtime context owner; bind once.
void bind_native_invariant_flotsam_datetime(FlotsamConsoleBackend&,
                                            std::shared_ptr<framework::NativeInvariantDateTimeContext> owner = {});
}
