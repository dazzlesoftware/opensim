// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/local_asset_connector.hpp"
#include <algorithm>
namespace opensim::services {
namespace {
template <class T> T &required(const std::shared_ptr<T> &value) {
    if (!value)
        throw std::invalid_argument("Null local asset connector dependency");
    return *value;
}
constexpr auto zero = "00000000-0000-0000-0000-000000000000";
bool is_foreign_asset_id(std::string_view id) {
    return id.starts_with('h') || id.starts_with('H');
}
AssetService::Asset asset_with_payload(AssetService::Asset asset) {
    return asset && asset->data && !asset->data->empty() ? asset : nullptr;
}
}
LocalAssetConnector::LocalAssetConnector(std::shared_ptr<AssetConnectorBackend> backend,
                                         bool service_handles_mutations)
    : backend_(std::move(backend)), service_handles_mutations_(service_handles_mutations) {
    required(backend_);
}
void LocalAssetConnector::initialise(const server::IniConfig *config) {
    if (!config)
        throw std::invalid_argument("Null asset connector configuration");
    if (!config->has_section("Modules"))
        return;
    if (config->get("Modules", "AssetServices", "") != name)
        return;
    if (!config->has_section("AssetService")) {
        backend_->error("[LOCAL ASSET SERVICES CONNECTOR]: AssetService missing from OpenSim.ini");
        return;
    }
    auto module = config->get("AssetService", "LocalServiceModule", "");
    if (module.empty()) {
        backend_->error(
            "[LOCAL ASSET SERVICES CONNECTOR]: No LocalServiceModule named in section AssetService");
        return;
    }
    initialise_service(config, module);
}
void LocalAssetConnector::initialise_service(const server::IniConfig *config, const std::string &module) {
    service_ = backend_->load_service(module, config);
    if (!service_.load()) {
        backend_->error("[LOCAL ASSET SERVICES CONNECTOR]: Fail to load asset service " + module);
        return;
    }
    enabled_ = true;
    backend_->info("[LOCAL ASSET SERVICES CONNECTOR]: Local asset connector enabled");
}
void LocalAssetConnector::add_region(std::shared_ptr<AssetConnectorScene> scene) {
    if (!enabled_)
        return;
    required(scene).register_service(shared_from_this());
}
void LocalAssetConnector::region_loaded(std::shared_ptr<AssetConnectorScene> scene) {
    if (!enabled_)
        return;
    if (!cache_.load()) {
        auto value = required(scene).asset_cache();
        cache_ = value.cache;
        if (!value.shared_module)
            cache_.store(nullptr);
    }
    auto prefix =
        cache_.load()
            ? "[LOCAL ASSET SERVICES CONNECTOR]: Enabled asset connector without caching for region "
            : "[LOCAL ASSET SERVICES CONNECTOR]: Enabled asset connector with caching for region ";
    backend_->debug(prefix + required(scene).region_name().value_or(""));
}
AssetService::Asset LocalAssetConnector::get_asset(const std::string &id) {
    if (foreign_url_fetch_ && is_foreign_asset_id(id))
        return foreign_url_fetch_(id);
    Asset asset;
    auto cache = cache_.load();
    if (cache && !cache->get(id, asset))
        return {};
    if (!asset) {
        asset = required(service_.load()).get_asset(id);
        cache = cache_.load();
        if (cache) {
            if (asset)
                cache->cache(asset);
            else
                cache->cache_negative(id);
        }
    }
    return asset;
}
AssetService::Asset LocalAssetConnector::get_foreign(const std::string &id,
                                                     const std::optional<std::string> &foreign, bool local) {
    return foreign_fetch_ ? foreign_fetch_(id, foreign, local) : nullptr;
}
void LocalAssetConnector::complete_read(const std::string &id, const Asset &asset) {
    std::vector<Complete> waiters;
    {
        std::lock_guard lock(pending_reads_mutex_);
        auto found = pending_reads_.find(id);
        if (found == pending_reads_.end())
            return;
        waiters = std::move(found->second);
        pending_reads_.erase(found);
    }
    deliver_reads(waiters, asset);
}
void LocalAssetConnector::deliver_reads(const std::vector<Complete>& waiters, const Asset& asset) {
    for (const auto& callback : waiters) {
        try {
            callback(asset);
        } catch (const std::exception &error) {
            backend_->error(error.what());
        } catch (...) {
            backend_->error("Asset read callback failed");
        }
    }
}
void LocalAssetConnector::close() {
    decltype(pending_reads_) cancelled;
    {
        std::lock_guard lock(pending_reads_mutex_);
        reads_closed_ = true;
        cancelled.swap(pending_reads_);
    }
    for (const auto& entry : cancelled)
        deliver_reads(entry.second, {});
}
AssetService::Asset LocalAssetConnector::get_cached(const std::string &id) {
    Asset asset;
    auto cache = cache_.load();
    if (cache)
        cache->get(id, asset);
    return asset;
}
std::shared_ptr<framework::AssetMetadata> LocalAssetConnector::get_metadata(const std::string &id) {
    if (foreign_url_fetch_ && is_foreign_asset_id(id)) {
        const auto asset = foreign_url_fetch_(id);
        return asset ? asset->metadata() : nullptr;
    }
    Asset asset;
    auto cache = cache_.load();
    if (cache && !cache->get(id, asset))
        return {};
    if (asset)
        return asset->metadata();
    asset = required(service_.load()).get_asset(id);
    if (asset) {
        cache = cache_.load();
        if (cache)
            cache->cache(asset);
        return asset->metadata();
    }
    return {};
}
framework::AssetBytes LocalAssetConnector::get_data(const std::string &id) {
    if (foreign_url_fetch_ && is_foreign_asset_id(id)) {
        const auto asset = foreign_url_fetch_(id);
        return asset ? asset->data : nullptr;
    }
    Asset asset;
    auto cache = cache_.load();
    if (cache && !cache->get(id, asset))
        return {};
    if (asset)
        return asset->data;
    asset = required(service_.load()).get_asset(id);
    if (asset) {
        cache = cache_.load();
        if (cache)
            cache->cache(asset);
        return asset->data;
    }
    return {};
}
bool LocalAssetConnector::get_async(const std::string &id, std::any sender, Retrieved callback) {
    if (service_handles_mutations_) {
        if (auto cache = cache_.load()) {
            Asset asset;
            if (!cache->get_from_memory(id, asset)) {
                callback(id, std::move(sender), {});
                return false;
            }
            if (asset) {
                callback(id, std::move(sender), asset_with_payload(std::move(asset)));
                return true;
            }
        }
        if (id == zero) {
            callback(id, std::move(sender), {});
            return false;
        }
    }
    if (service_handles_mutations_ || (foreign_url_fetch_ && is_foreign_asset_id(id))) {
        queue_read(id, std::nullopt, false, false,
            [id, sender = std::move(sender), callback = std::move(callback)](Asset asset) {
                callback(id, sender, std::move(asset));
            });
        return true;
    }
    auto self = shared_from_this();
    auto cache = cache_.load();
    if (cache && !service_handles_mutations_) {
        Asset asset;
        if (!cache->get(id, asset))
            return false;
        if (asset) {
            backend_->post(
                [id, sender = std::move(sender), asset, callback = std::move(callback)]() mutable {
                    callback(id, std::move(sender), asset);
                },
                "LocalAssetServiceConnector.GotFromCacheCallback");
            return true;
        }
    }
    if (id == zero)
        return false;
    return required(service_.load())
        .get_async(
            id, std::move(sender),
            [self, callback = std::move(callback)](const std::string &key, std::any source, Asset asset) {
                auto cache = self->cache_.load();
                if (cache) {
                    if (!asset)
                        cache->cache_negative(key);
                    else
                        cache->cache(asset);
                }
                self->backend_->post([key, source = std::move(source), asset,
                                      callback]() mutable { callback(key, std::move(source), asset); },
                                     "LocalAssetServiceConnector.GotFromServiceCallback");
            });
}
void LocalAssetConnector::get(const std::string &id, const std::optional<std::string> &foreign, bool local,
                              Complete callback) {
    const bool explicit_foreign = foreign_fetch_ && foreign && !foreign->empty() && id != zero;
    const bool url_foreign = foreign_url_fetch_ && is_foreign_asset_id(id);
    if (service_handles_mutations_ || explicit_foreign || url_foreign) {
        if (auto cache = cache_.load()) {
            Asset asset;
            const bool available = cache->get_from_memory(id, asset);
            if (!available || asset) {
                if (service_handles_mutations_)
                    asset = available ? asset_with_payload(std::move(asset)) : nullptr;
                callback(std::move(asset));
                return;
            }
        }
        if (id == zero && service_handles_mutations_) {
            callback({});
            return;
        }
        queue_read(id, foreign, local, explicit_foreign, std::move(callback));
        return;
    }
    auto self = shared_from_this();
    auto cache = cache_.load();
    if (cache) {
        Asset asset;
        if (!cache->get_from_memory(id, asset)) {
            callback({});
            return;
        }
        if (asset) {
            callback(service_handles_mutations_ ? asset_with_payload(std::move(asset)) : asset);
            return;
        }
    }
    if (id == zero) {
        callback({});
        return;
    }
    required(service_.load())
        .get_async(id, {},
                   [self, callback = std::move(callback)](const std::string &key, std::any, Asset asset) {
                       auto cache = self->cache_.load();
                       if (cache) {
                           if (!asset)
                               cache->cache_negative(key);
                           else
                               cache->cache(asset);
                       }
                       self->backend_->post([asset, callback] { callback(asset); },
                                            "LocalAssetServiceConnector.GotFromServiceCallback");
                   });
}
void LocalAssetConnector::queue_read(const std::string& id, const std::optional<std::string>& foreign,
                                        bool local, bool explicit_foreign, Complete callback) {
    bool closed;
    {
        std::lock_guard lock(pending_reads_mutex_);
        closed = reads_closed_;
        if (!closed) {
            auto& waiters = pending_reads_[id];
            waiters.push_back(std::move(callback));
            if (waiters.size() != 1)
                return;
        }
    }
    if (closed) {
        deliver_reads({std::move(callback)}, {});
        return;
    }
    if (!backend_->queue_asset_read(
            [self = shared_from_this(), id, foreign, local, explicit_foreign] {
                Asset asset;
                try {
                    asset = explicit_foreign ? self->get_foreign(id, foreign, local) : self->get_asset(id);
                } catch (const std::exception&) {
                }
                self->complete_read(id, asset);
            },
            explicit_foreign))
        complete_read(id, {});
}
std::vector<bool> LocalAssetConnector::assets_exist(const std::vector<std::string> &ids) {
    if (service_handles_mutations_ && std::any_of(ids.begin(), ids.end(), [](const auto& id) {
            return id.starts_with('h') || id.starts_with('H');
        }))
        return foreign_exists_ ? foreign_exists_(ids) : std::vector<bool>{};
    return required(service_.load()).assets_exist(ids);
}
std::optional<std::string> LocalAssetConnector::store(Asset asset) {
    auto cache = cache_.load();
    if (service_handles_mutations_ && is_foreign_asset_id(required(asset).id())) {
        if (asset->local() || asset->temporary())
            return std::nullopt;
        if (!foreign_store_)
            return std::string{};
        auto stored = foreign_store_(asset);
        if (!stored || stored->empty() || *stored == framework::Uuid{}.str())
            return std::string{};
        if (cache)
            cache->cache(asset);
        return stored;
    }
    if (cache)
        cache->cache(asset);
    if (service_handles_mutations_) {
        if (cache && (asset->local() || asset->temporary()))
            return asset->id();
        auto stored = required(service_.load()).store(asset);
        if (!stored || *stored == zero)
            return std::string{};
        return stored;
    }
    if (required(asset).local())
        return asset->id();
    return required(service_.load()).store(asset);
}
bool LocalAssetConnector::update_content(const std::string &id, framework::AssetBytes data) {
    // RegionAssetConnector delegates mutations to its local service without touching the region cache.
    if (service_handles_mutations_) {
        if (id.starts_with('h') || id.starts_with('H'))
            return false;
        return required(service_.load()).update_content(id, std::move(data));
    }
    Asset asset;
    auto cache = cache_.load();
    if (cache)
        cache->get(id, asset);
    if (asset) {
        asset->data = data;
        cache = cache_.load();
        if (cache)
            cache->cache(asset);
    }
    return required(service_.load()).update_content(id, std::move(data));
}
bool LocalAssetConnector::erase(const std::string &id) {
    if (service_handles_mutations_) {
        if (id.starts_with('h') || id.starts_with('H'))
            return false;
    } else {
        if (auto cache = cache_.load())
            cache->expire(id);
    }
    return required(service_.load()).erase(id);
}
}
