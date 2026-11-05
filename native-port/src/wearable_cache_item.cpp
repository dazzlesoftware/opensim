// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/wearable_cache_item.hpp"
#include "opensim/framework/avatar_appearance_constants.hpp"
#include "opensim/network/osd_value.hpp"
#include <stdexcept>
namespace opensim::framework {
namespace {
using network::Value;
Value uuid(const Uuid& id) {
    Value value(id.str());
    value.kind = Value::Kind::Uuid;
    return value;
}
WearableCacheItem& required(const std::shared_ptr<WearableCacheItem>& item) {
    if (!item)
        throw std::invalid_argument("Null wearable cache entry");
    return *item;
}
const WearableCacheItems& required(const WearableCacheItems* items) {
    if (!items)
        throw std::invalid_argument("Null wearable cache array");
    return *items;
}
Value packed(const WearableCacheItem& item) {
    auto result = Value::object({});
    result.set("textureindex", network::osd_from_unsigned(item.texture_index));
    result.set("cacheid", uuid(item.cache_id));
    result.set("textureid", uuid(item.texture_id));
    return result;
}
template <class Match>
std::shared_ptr<WearableCacheItem> search(const WearableCacheItems* items, Match match) {
    for (const auto& item : required(items))
        if (match(required(item)))
            return item;
    return {};
}
}
WearableCacheItems WearableCacheItem::defaults() {
    WearableCacheItems result;
    for (int i = 0; i < avatar_appearance::texture_count; ++i) {
        auto item = std::make_shared<WearableCacheItem>();
        item->texture_index = static_cast<std::uint32_t>(i);
        result.push_back(std::move(item));
    }
    return result;
}
WearableCacheItems WearableCacheItem::from_osd(const network::Value& input, IAssetCache* cache) {
    return from_osd(&input, cache);
}
WearableCacheItems WearableCacheItem::from_osd(const network::Value* input, IAssetCache* cache) {
    if (!input)
        throw std::invalid_argument("Null wearable cache OSD");
    WearableCacheItems result;
    const auto append = [&](const Value& row, bool array) {
        if (row.kind != Value::Kind::Map)
            throw std::invalid_argument("Wearable cache requires map entries");
        auto item = std::make_shared<WearableCacheItem>();
        item->texture_index = network::osd_unsigned(row.get("textureindex"));
        item->cache_id = network::osd_uuid(row.get("cacheid"));
        item->texture_id = network::osd_uuid(row.get("textureid"));
        result.push_back(item);
        if (cache && row.contains("assetdata")) {
            auto asset = std::make_shared<AssetBase>(
                network::osd_uuid(row.get("textureid")),
                array ? "BakedTexture" : network::osd_string(row.get("assetname")), std::int8_t{0},
                array ? Uuid{}.str() : network::osd_string(row.get("assetcreator")));
            asset->temporary(true);
            asset->data = network::osd_binary(row.get("assetdata"));
            cache->cache(asset);
        }
    };
    if (input->kind == Value::Kind::Array)
        for (const auto& row : input->array)
            append(row, true);
    else if (input->kind == Value::Kind::Map)
        append(*input, false);
    return result;
}
network::Value WearableCacheItem::to_osd(const WearableCacheItems* items, IAssetCache* cache) {
    Value result;
    result.kind = Value::Kind::Array;
    for (const auto& pointer : required(items)) {
        const auto& item = required(pointer);
        auto row = packed(item);
        if (cache && cache->check(item.texture_id.str())) {
            std::shared_ptr<AssetBase> asset;
            cache->get(item.texture_id.str(), asset);
            if (asset) {
                row.set("assetdata", network::osd_from_binary(asset->data));
                row.set("assetcreator", Value(asset->creator_id().value_or("")));
                row.set("assetname", Value(asset->name().value_or("")));
            }
        }
        result.array.push_back(std::move(row));
    }
    return result;
}
std::optional<network::Value> WearableCacheItem::baked_to_osd(const WearableCacheItems* items, int start,
                                                              int end) {
    Value result;
    result.kind = Value::Kind::Array;
    if (start < 0)
        start = 0;
    if (end < 0 || end > static_cast<int>(avatar_appearance::bake_indices.size()))
        end = static_cast<int>(avatar_appearance::bake_indices.size());
    if (start > end)
        return std::nullopt;
    for (int i = start; i < end; ++i) {
        const auto index =
            static_cast<std::size_t>(avatar_appearance::bake_indices[static_cast<std::size_t>(i)]);
        if (index >= required(items).size())
            continue;
        result.array.push_back(packed(required((*items)[index])));
    }
    return result;
}
WearableCacheItems WearableCacheItem::baked_from_osd(const network::Value* input) {
    auto result = defaults();
    if (!input)
        throw std::invalid_argument("Null wearable cache OSD");
    if (input->kind == Value::Kind::Array) {
        for (const auto& row : input->array) {
            if (row.kind != Value::Kind::Map)
                throw std::invalid_argument("Wearable cache requires map entries");
            const auto index = network::osd_integer(row.get("textureindex"));
            if (index < 0 || static_cast<std::size_t>(index) >= result.size())
                continue;
            auto& item = *result[static_cast<std::size_t>(index)];
            item.cache_id = network::osd_uuid(row.get("cacheid"));
            item.texture_id = network::osd_uuid(row.get("textureid"));
            item.texture_asset.reset();
        }
    }
    return result;
}
std::shared_ptr<WearableCacheItem> WearableCacheItem::search_index(std::uint32_t index,
                                                                   const WearableCacheItems* items) {
    return search(items, [&](const auto& item) { return item.texture_index == index; });
}
std::shared_ptr<WearableCacheItem> WearableCacheItem::search_cache(const Uuid& cache,
                                                                   const WearableCacheItems* items) {
    return search(items, [&](const auto& item) { return item.cache_id == cache; });
}
std::shared_ptr<WearableCacheItem> WearableCacheItem::search_texture(const Uuid& texture,
                                                                     const WearableCacheItems* items) {
    return search(items, [&](const auto& item) { return item.texture_id == texture; });
}
}
