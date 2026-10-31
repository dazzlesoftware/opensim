// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/native_asset_storage.hpp"
#include "opensim/services/sqlite_migration.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/services/sqlite_connection.hpp"
#include "opensim/services/user_account.hpp"
namespace opensim::services {
namespace {
template<class T>T& required(const std::shared_ptr<T>& value){if(!value)throw std::invalid_argument("Asset storage plugin is not initialized");return *value;}

}
void SqliteAssetPlugin::initialise(const std::string& connection){
 auto value=std::make_shared<SqliteAssetStore>(warn_);store_=value;
 auto values=parse_sqlite_connection(connection.empty()?"URI=file:Asset.db":connection);
 for(auto& [key,value]:values){
  if(framework::ordinal_equals_ignore_case(key,"URI")||framework::ordinal_equals_ignore_case(key,"Data Source")||framework::ordinal_equals_ignore_case(key,"DataSource")||framework::ordinal_equals_ignore_case(key,"Data_Source"))continue;
  if(framework::ordinal_equals_ignore_case(key,"Version")){int32_t version;if(!parse_account_integer(value,version)||version!=3)throw std::invalid_argument("Unsupported SQLite version");}
  else if(framework::ordinal_equals_ignore_case(key,"Pooling")){if(sqlite_boolean(value))throw std::invalid_argument("SQLite asset connection pooling is not implemented");}
  else throw std::invalid_argument("SQLite asset connection option is not implemented: "+key);
 }
 auto path=sqlite_connection_path(values);value->open(path,true);value->migrate(migrations(),log_);
}
void SqliteAssetPlugin::dispose(){auto store=store_.load();if(store)store->dispose();}
std::shared_ptr<MigrationResources> SqliteAssetPlugin::migrations(){return asset_store_migrations();}
AssetService::Asset SqliteAssetPlugin::get(framework::Uuid id){auto store=store_.load();return required(store).get(id);}
bool SqliteAssetPlugin::store(AssetService::Asset asset){auto store=store_.load();return required(store).store(std::move(asset));}
std::vector<bool> SqliteAssetPlugin::exists(const std::vector<framework::Uuid>& ids){auto store=store_.load();return required(store).exists(ids);}
std::vector<std::shared_ptr<framework::AssetMetadata>> SqliteAssetPlugin::metadata_set(std::int32_t start,std::int32_t count){auto store=store_.load();return required(store).metadata_set(start,count);}
bool SqliteAssetPlugin::erase(const std::string& id){auto store=store_.load();return required(store).erase(id);}
void NativeAssetStorageBackend::register_store(std::string module,StoreFactory factory){stores_[std::move(module)]=std::move(factory);}
void NativeAssetStorageBackend::register_loader(std::string module,LoaderFactory factory){loaders_[std::move(module)]=std::move(factory);}
std::shared_ptr<InitializableAssetStore> NativeAssetStorageBackend::load_store(const std::string& module){if(auto entry=stores_.find(module);entry!=stores_.end())return entry->second();if(module=="OpenSim.Data.SQLite.dll"||module=="OpenSim.Data.SQLite.dll:SQLiteAssetData"){auto self=shared_from_this();return std::make_shared<SqliteAssetPlugin>([self](const auto& message){self->warn(message);},MigrationLog{[self](const auto& message){self->info(message);},[self](const auto& message){self->debug(message);},[self](const auto& message){self->error(message);}});}return {};}
std::shared_ptr<DefaultAssetLoader> NativeAssetStorageBackend::load_loader(const std::string& module){if(auto entry=loaders_.find(module);entry!=loaders_.end())return entry->second();if(module=="OpenSim.Framework.AssetLoader.Filesystem.dll"||module=="OpenSim.Framework.AssetLoader.Filesystem.dll:AssetLoaderFileSystem")return std::make_shared<FilesystemAssetLoader>(std::static_pointer_cast<AssetLoaderFiles>(shared_from_this()));return {};}
void NativeAssetStorageBackend::debug(const std::string& message){if(log_)log_("debug",message);}
void NativeAssetStorageBackend::info(const std::string& message){if(log_)log_("info",message);}
void NativeAssetStorageBackend::warn(const std::string& message){if(log_)log_("warn",message);}
void NativeAssetStorageBackend::error(const std::string& message){if(log_)log_("error",message);}
void NativeAssetStorageBackend::error(const std::string& message,const std::exception& exception){if(log_)log_("error",message+exception.what());}
}
