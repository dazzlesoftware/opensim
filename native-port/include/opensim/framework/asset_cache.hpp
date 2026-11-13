// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
namespace opensim::framework {
class IAssetCache {
public:
    virtual ~IAssetCache()=default;
    virtual void cache(const std::shared_ptr<AssetBase>& asset, bool replace = false) = 0;
    virtual void cache_negative(const std::string& id)=0;
    // False denotes a negative-cache hit; the source callers may ignore it.
    virtual bool get(const std::string& id,std::shared_ptr<AssetBase>& asset)=0;
    virtual std::shared_ptr<AssetBase> get_cached(const std::string& id)=0;
    virtual bool get_from_memory(const std::string& id,std::shared_ptr<AssetBase>& asset)=0;
    virtual bool check(const std::string& id)=0;
    virtual void expire(const std::string& id)=0;
    virtual void clear()=0;
};
}
