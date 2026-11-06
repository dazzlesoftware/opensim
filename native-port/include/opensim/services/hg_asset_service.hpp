// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/server/config.hpp"
#include "opensim/services/asset_service.hpp"
#include <bitset>

namespace opensim::services {
framework::AssetBytes sanitize_object_asset(const framework::AssetBytes& data);

class AssetExchangePermissions {
public:
    using Log = std::function<void(const std::string&, const std::string&)>;
    AssetExchangePermissions() = default;
    AssetExchangePermissions(const server::IniConfig& config, const std::string& section, Log log = {});
    bool allows_import(std::int8_t type) const;
    bool allows_export(std::int8_t type) const;

private:
    std::bitset<256> blocked_import_, blocked_export_;
};

class HgAssetService final : public AssetService {
public:
    using AccountName = std::function<std::optional<std::string>(framework::Uuid)>;
    HgAssetService(std::shared_ptr<AssetService> backing, std::string home, AccountName account_name,
                   AssetExchangePermissions permissions);
    Asset get_asset(const std::string& id) override;
    Asset get_cached(const std::string& id) override;
    std::shared_ptr<framework::AssetMetadata> get_metadata(const std::string& id) override;
    framework::AssetBytes get_data(const std::string& id) override;
    bool get_async(const std::string& id, std::any sender, Retrieved callback) override;
    std::vector<bool> assets_exist(const std::vector<std::string>& ids) override;
    std::optional<std::string> store(Asset asset) override;
    bool update_content(const std::string&, framework::AssetBytes) override { return false; }
    bool erase(const std::string&) override { return false; }
    Asset get_foreign(const std::string&, const std::optional<std::string>&, bool) override { return {}; }
    void get(const std::string&, const std::optional<std::string>&, bool, Complete) override {}

private:
    Asset prepare_export(Asset asset);
    void adjust_creator(framework::AssetMetadata& metadata);
    std::optional<std::string> creator_name(std::string_view id);
    framework::AssetBytes rewrite_object(const framework::AssetBytes& data);

    std::shared_ptr<AssetService> backing_;
    std::string home_;
    AccountName account_name_;
    AssetExchangePermissions permissions_;
    framework::ExpiringCacheOS<framework::Uuid, std::optional<std::string>, framework::UuidHash> names_{5000};
};
}
