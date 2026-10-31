// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/asset_service.hpp"
namespace opensim::services {
class MigrationResources;
struct MigrationLog;
class AssetStore {
public:
 virtual ~AssetStore()=default;
 virtual AssetService::Asset get(framework::Uuid id)=0;
 virtual bool store(AssetService::Asset asset)=0;
 virtual std::vector<bool> exists(const std::vector<framework::Uuid>& ids)=0;
 virtual std::vector<std::shared_ptr<framework::AssetMetadata>> metadata_set(std::int32_t start,std::int32_t count)=0;
 virtual bool erase(const std::string& id)=0;
};
class SqliteAssetStore:public AssetStore {
public:
 using Warn=std::function<void(const std::string&)>;
 explicit SqliteAssetStore(Warn warn={});
 explicit SqliteAssetStore(const std::string& path,bool initialize=false,Warn warn={});
 ~SqliteAssetStore()override;
 AssetService::Asset get(framework::Uuid id)override;
 bool store(AssetService::Asset asset)override;
 std::vector<bool> exists(const std::vector<framework::Uuid>& ids)override;
 std::vector<std::shared_ptr<framework::AssetMetadata>> metadata_set(std::int32_t start,std::int32_t count)override;
 bool erase(const std::string& id)override;
 bool erase_asset(framework::Uuid id);
 void dispose();
 void open(const std::string& path,bool create=true);
 void migrate(std::shared_ptr<MigrationResources> resources,const MigrationLog& log);
 static constexpr std::string_view name="SQLite Asset storage engine";
private:
 struct State;std::unique_ptr<State> state_;Warn warn_;
};
class StoredAssetService:public AssetService {
public:
 struct Diagnostics {
  std::function<void(const std::string&)> warn;
  std::function<void(const std::string&,const std::exception&)> error;
 };
 explicit StoredAssetService(std::shared_ptr<AssetStore> store,Diagnostics diagnostics={});
 Asset get_asset(const std::string& id)override;
 Asset get_foreign(const std::string&,const std::optional<std::string>&,bool)override{return {};}
 Asset get_cached(const std::string& id)override{return get_asset(id);}
 std::shared_ptr<framework::AssetMetadata> get_metadata(const std::string& id)override;
 framework::AssetBytes get_data(const std::string& id)override;
 bool get_async(const std::string& id,std::any sender,Retrieved callback)override;
 // This overload has an empty body in the original storage service. The
 // local connector deliberately uses the three-argument callback overload.
 void get(const std::string&,const std::optional<std::string>&,bool,Complete)override{}
 std::vector<bool> assets_exist(const std::vector<std::string>& ids)override;
 std::optional<std::string> store(Asset asset)override;
 bool update_content(const std::string&,framework::AssetBytes)override{return false;}
 bool erase(const std::string& id)override;
private:
 std::shared_ptr<AssetStore> store_;Diagnostics diagnostics_;
};
}
