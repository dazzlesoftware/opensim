// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/asset_service.hpp"
#include "opensim/framework/asset_cache.hpp"
#include "opensim/server/config.hpp"
namespace opensim::services {
struct AssetConnectorCache {std::shared_ptr<framework::IAssetCache> cache;bool shared_module=false;};
class AssetConnectorScene {
public:
 virtual ~AssetConnectorScene()=default;
 virtual void register_service(std::shared_ptr<AssetService> service)=0;
 virtual AssetConnectorCache asset_cache()=0;
 virtual std::optional<std::string> region_name()=0;
};
class AssetConnectorBackend {
public:
 virtual ~AssetConnectorBackend()=default;
 virtual std::shared_ptr<AssetService> load_service(const std::string& module,const server::IniConfig* config)=0;
 virtual void error(const std::string& message)=0;
 virtual void info(const std::string& message)=0;
 virtual void debug(const std::string& message)=0;
 virtual void post(std::function<void()> callback,const std::string& name)=0;
};
class LocalAssetConnector:public AssetService,public std::enable_shared_from_this<LocalAssetConnector> {
public:
 explicit LocalAssetConnector(std::shared_ptr<AssetConnectorBackend> backend);
 static constexpr std::string_view name="LocalAssetServicesConnector";
 std::nullptr_t replaceable_interface()const{return nullptr;}
 void initialise(const server::IniConfig* config);
 void post_initialise(){}void close(){}void remove_region(std::shared_ptr<AssetConnectorScene>){}
 void add_region(std::shared_ptr<AssetConnectorScene> scene);
 void region_loaded(std::shared_ptr<AssetConnectorScene> scene);
 Asset get_asset(const std::string& id)override;
 Asset get_foreign(const std::string&,const std::optional<std::string>&,bool)override{return {};}
 Asset get_cached(const std::string& id)override;
 std::shared_ptr<framework::AssetMetadata> get_metadata(const std::string& id)override;
 framework::AssetBytes get_data(const std::string& id)override;
 bool get_async(const std::string& id,std::any sender,Retrieved callback)override;
 void get(const std::string& id,const std::optional<std::string>& foreign,bool local,Complete callback)override;
 std::vector<bool> assets_exist(const std::vector<std::string>& ids)override;
 std::optional<std::string> store(Asset asset)override;
 bool update_content(const std::string& id,framework::AssetBytes data)override;
 bool erase(const std::string& id)override;
private:
 friend struct LocalAssetConnectorProbe;
 std::shared_ptr<AssetConnectorBackend> backend_;
 std::atomic<std::shared_ptr<AssetService>> service_;
 std::atomic<std::shared_ptr<framework::IAssetCache>> cache_;
 bool enabled_=false;
};
}
