// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/region_runtime.hpp"
void verify_object_details_text_color(opensim::scene::RegionRuntime &runtime,
                                      const std::function<void(std::shared_ptr<opensim::framework::AssetBase>)> &store);
