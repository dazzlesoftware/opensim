// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/get_assets.hpp"
#include <any>
namespace opensim::services {
class AssetService:public server::AssetRetrievalService {
public:
 using Asset=std::shared_ptr<framework::AssetBase>;
 using Retrieved=std::function<void(const std::string&,std::any,Asset)>;
 virtual Asset get_asset(const std::string& id)=0;
 virtual Asset get_foreign(const std::string& id,const std::optional<std::string>& foreign,bool local)=0;
 virtual Asset get_cached(const std::string& id)=0;
 virtual std::shared_ptr<framework::AssetMetadata> get_metadata(const std::string& id)=0;
 virtual framework::AssetBytes get_data(const std::string& id)=0;
 virtual bool get_async(const std::string& id,std::any sender,Retrieved callback)=0;
 virtual std::vector<bool> assets_exist(const std::vector<std::string>& ids)=0;
 virtual std::optional<std::string> store(Asset asset)=0;
 virtual bool update_content(const std::string& id,framework::AssetBytes data)=0;
 virtual bool erase(const std::string& id)=0;
};
}
