// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/asset_storage_factory.hpp"
#include <map>
namespace opensim::services {
class AssetXmlError:public std::runtime_error {public:using std::runtime_error::runtime_error;};
using AssetXmlConfig=std::map<std::string,std::string>;
class AssetLoaderFiles {
public:
 virtual ~AssetLoaderFiles()=default;
 virtual framework::Uuid random_uuid(){return framework::Uuid::random();}
 virtual void info(const std::string& message)=0;
 virtual void error(const std::string& message)=0;
 virtual bool exists(const std::string& path);
 virtual std::vector<AssetXmlConfig> configurations(const std::string& path);
 virtual std::string full_path(const std::string& path);
 virtual std::string directory(const std::string& path);
 virtual std::string combine(const std::string& directory,const std::string& path);
 virtual std::int64_t length(const std::string& path);
 virtual framework::AssetBytes read_bytes(const std::string& path,std::int32_t count);
};
class FilesystemAssetLoader:public DefaultAssetLoader {
public:
 explicit FilesystemAssetLoader(std::shared_ptr<AssetLoaderFiles> files);
 void for_each(const std::string& arguments,std::function<void(AssetService::Asset)> action)override;
 AssetService::Asset create_asset(const std::string& id,const std::string& name,const std::optional<std::string>& path,std::int8_t type);
 void load_asset(AssetService::Asset asset,const std::string& path);
 void load_set(const std::string& path,std::vector<AssetService::Asset>& assets);
private:std::shared_ptr<AssetLoaderFiles> files_;
};
}
