// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/native_asset_storage.hpp"

namespace opensim::services {
std::shared_ptr<InitializableAssetStore> make_mysql_asset_store(NativeAssetStorageBackend::Log log);
}
