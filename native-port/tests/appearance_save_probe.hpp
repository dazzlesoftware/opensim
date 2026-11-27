// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/region_runtime.hpp"

void verify_script_appearance_saves(opensim::scene::RegionRuntime &runtime,
                                    opensim::services::IPresenceService &presence,
                                    const opensim::scene::RegionPolicies &policies,
                                    const std::shared_ptr<opensim::framework::AgentCircuitData> &viewer,
                                    const std::vector<opensim::framework::Uuid> &persisted_bakes);
