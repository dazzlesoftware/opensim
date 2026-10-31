// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/filesystem_asset_loader.hpp"
#include "opensim/services/migration.hpp"
#include <atomic>
namespace opensim::services {
class SqliteAssetPlugin:public InitializableAssetStore {
public:
 explicit SqliteAssetPlugin(SqliteAssetStore::Warn warn={},MigrationLog log={}):warn_(std::move(warn)),log_(std::move(log)),store_(std::make_shared<SqliteAssetStore>(warn_)){}
 void initialise(const std::string& connection)override;
 void dispose()override;
 AssetService::Asset get(framework::Uuid id)override;
 bool store(AssetService::Asset asset)override;
 std::vector<bool> exists(const std::vector<framework::Uuid>& ids)override;
 std::vector<std::shared_ptr<framework::AssetMetadata>> metadata_set(std::int32_t start,std::int32_t count)override;
 bool erase(const std::string& id)override;
protected:virtual std::shared_ptr<MigrationResources> migrations();
private:SqliteAssetStore::Warn warn_;MigrationLog log_;std::atomic<std::shared_ptr<SqliteAssetStore>> store_;
};
class NativeAssetStorageBackend:public AssetStorageBackend,public AssetLoaderFiles,public std::enable_shared_from_this<NativeAssetStorageBackend> {
public:
 using Log=std::function<void(const std::string& level,const std::string& message)>;
 using StoreFactory=std::function<std::shared_ptr<InitializableAssetStore>()>;
 using LoaderFactory=std::function<std::shared_ptr<DefaultAssetLoader>()>;
 explicit NativeAssetStorageBackend(Log log={}):log_(std::move(log)){}
 void register_store(std::string module,StoreFactory factory);
 void register_loader(std::string module,LoaderFactory factory);
 std::shared_ptr<InitializableAssetStore> load_store(const std::string& module)override;
 std::shared_ptr<DefaultAssetLoader> load_loader(const std::string& module)override;
 void debug(const std::string& message)override;
 void info(const std::string& message)override;
 void warn(const std::string& message)override;
 void error(const std::string& message)override;
 void error(const std::string& message,const std::exception& exception)override;
private:Log log_;std::map<std::string,StoreFactory> stores_;std::map<std::string,LoaderFactory> loaders_;
};
}
