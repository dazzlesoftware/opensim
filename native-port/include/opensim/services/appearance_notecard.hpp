// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_cache.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include <functional>

namespace opensim::services {
std::string appearance_notecard_xml(framework::AvatarAppearance &appearance, bool no_huds);
void persist_cached_appearance_bake(framework::Uuid texture, const std::shared_ptr<framework::IAssetCache> &cache,
                                    const std::function<void(const std::shared_ptr<framework::AssetBase> &)> &store);
} // namespace opensim::services
