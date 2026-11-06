// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/native_asset_storage.hpp"

namespace opensim::framework {
class ConsoleCommandResolver;
}

namespace opensim::services {
inline constexpr auto fs_asset_module = "OpenSim.Services.FSAssetService.dll:FSAssetConnector";
std::shared_ptr<AssetService> make_fs_asset_service(
    const server::IniConfig& config, std::shared_ptr<NativeAssetStorageBackend> backend);
void register_fs_asset_console(const std::shared_ptr<AssetService>& service,
                               framework::ConsoleCommandResolver& commands,
                               const std::function<void(const std::string&)>& output);
}
