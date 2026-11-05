// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_cache.hpp"
#include "opensim/network/rpc.hpp"
namespace opensim::framework {
struct WearableCacheItem;
using WearableCacheItems = std::vector<std::shared_ptr<WearableCacheItem>>;
struct WearableCacheItem {
    std::uint32_t texture_index = 0;
    Uuid cache_id, texture_id;
    std::shared_ptr<AssetBase> texture_asset;
    static WearableCacheItems defaults();
    static WearableCacheItems from_osd(const network::Value& input, IAssetCache* cache = nullptr);
    static WearableCacheItems from_osd(const network::Value* input, IAssetCache* cache = nullptr);
    static network::Value to_osd(const WearableCacheItems& items, IAssetCache* cache = nullptr) {
        return to_osd(&items, cache);
    }
    static network::Value to_osd(const WearableCacheItems* items, IAssetCache* cache = nullptr);
    static std::optional<network::Value> baked_to_osd(const WearableCacheItems& items, int start, int end) {
        return baked_to_osd(&items, start, end);
    }
    static std::optional<network::Value> baked_to_osd(const WearableCacheItems* items, int start, int end);
    static WearableCacheItems baked_from_osd(const network::Value& input) {
        return baked_from_osd(&input);
    }
    static WearableCacheItems baked_from_osd(const network::Value* input);
    static std::shared_ptr<WearableCacheItem> search_index(std::uint32_t index,
                                                           const WearableCacheItems* items);
    static std::shared_ptr<WearableCacheItem> search_cache(const Uuid& cache,
                                                           const WearableCacheItems* items);
    static std::shared_ptr<WearableCacheItem> search_texture(const Uuid& texture,
                                                             const WearableCacheItems* items);
};
}
