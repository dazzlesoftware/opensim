// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/asset_store.hpp"
namespace opensim::services {
namespace {
template <class T> T &required(const std::shared_ptr<T> &value) {
    if (!value)
        throw std::invalid_argument("Null asset store reference");
    return *value;
}
}
StoredAssetService::StoredAssetService(std::shared_ptr<AssetStore> store, Diagnostics diagnostics)
    : store_(std::move(store)), diagnostics_(std::move(diagnostics)) {}
AssetService::Asset StoredAssetService::get_asset(const std::string &id) {
    framework::Uuid parsed;
    if (!framework::try_parse_sdk_uuid(id, parsed)) {
        if (diagnostics_.warn)
            diagnostics_.warn("[ASSET SERVICE]: Could not parse requested asset id " + id);
        return {};
    }
    try {
        return required(store_).get(parsed);
    } catch (const std::exception &error) {
        if (diagnostics_.error)
            diagnostics_.error("[ASSET SERVICE]: Exception getting asset " + parsed.str() + " ", error);
        return {};
    }
}
std::shared_ptr<framework::AssetMetadata> StoredAssetService::get_metadata(const std::string &id) {
    auto asset = get_asset(id);
    return asset ? asset->metadata() : nullptr;
}
framework::AssetBytes StoredAssetService::get_data(const std::string &id) {
    auto asset = get_asset(id);
    return asset ? asset->data : nullptr;
}
bool StoredAssetService::get_async(const std::string &id, std::any sender, Retrieved callback) {
    auto asset = get_asset(id);
    callback(id, std::move(sender), asset);
    return true;
}
std::vector<bool> StoredAssetService::assets_exist(const std::vector<std::string> &ids) {
    try {
        std::vector<framework::Uuid> values;
        values.reserve(ids.size());
        for (auto &id : ids) {
            values.push_back(framework::parse_sdk_uuid(id));
        }
        return required(store_).exists(values);
    } catch (const std::exception &error) {
        if (diagnostics_.error)
            diagnostics_.error("[ASSET SERVICE]: Exception getting assets ", error);
        return std::vector<bool>(ids.size(), false);
    }
}
std::optional<std::string> StoredAssetService::store(Asset asset) {
    auto found = required(store_).exists({required(asset).full_id()});
    if (found.empty() || !found[0])
        if (!required(store_).store(asset))
            return framework::Uuid{}.str();
    return asset->id();
}
bool StoredAssetService::erase(const std::string &id) {
    framework::Uuid ignored;
    if (framework::try_parse_sdk_uuid(id, ignored))
        return required(store_).erase(id);
    return false;
}
}
