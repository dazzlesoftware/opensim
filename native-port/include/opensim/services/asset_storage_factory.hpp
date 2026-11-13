// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/atomic_shared_ptr.hpp"
#include "opensim/services/asset_store.hpp"
#include "opensim/server/config.hpp"
namespace opensim::services {
class InitializableAssetStore : public AssetStore {
public:
    virtual void initialise(const std::string& connection) = 0;
    virtual void dispose() = 0;
};
class DefaultAssetLoader {
public:
    virtual ~DefaultAssetLoader() = default;
    virtual void for_each(const std::string& arguments, std::function<void(AssetService::Asset)> action) = 0;
};
class AssetStorageBackend {
public:
    virtual ~AssetStorageBackend() = default;
    virtual std::shared_ptr<InitializableAssetStore> load_store(const std::string& module) = 0;
    virtual std::shared_ptr<DefaultAssetLoader> load_loader(const std::string& module) = 0;
    virtual void debug(const std::string& message) = 0;
    virtual void warn(const std::string& message) = 0;
    virtual void error(const std::string& message, const std::exception& exception) = 0;
    virtual std::string hash(const framework::AssetBytes& data);
};
class ConfiguredAssetService : public StoredAssetService {
public:
    ConfiguredAssetService(std::shared_ptr<InitializableAssetStore> data,
                           std::shared_ptr<DefaultAssetLoader> defaults, std::optional<std::string> section,
                           std::shared_ptr<AssetStorageBackend> backend);
    std::shared_ptr<InitializableAssetStore> database;
    std::shared_ptr<DefaultAssetLoader> loader;
    std::optional<std::string> config_name;
};
struct AssetStorageState {
    opensim::framework::AtomicSharedPtr<ConfiguredAssetService> root;
};
class AssetStorageFactory {
public:
    explicit AssetStorageFactory(std::shared_ptr<AssetStorageBackend> backend,
                                 std::shared_ptr<AssetStorageState> state = {});
    std::shared_ptr<ConfiguredAssetService>
    create(const server::IniConfig* config,
           const std::optional<std::string>& name = std::string("AssetService"));

private:
    std::shared_ptr<AssetStorageBackend> backend_;
    std::shared_ptr<AssetStorageState> state_;
};
}
