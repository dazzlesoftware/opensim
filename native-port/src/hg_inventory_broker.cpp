// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/hg_inventory_broker.hpp"
#include "opensim/framework/expiring_cache.hpp"
#include <map>
#include <mutex>
#include <unordered_map>

namespace opensim::services {
using framework::Uuid;

namespace {
// Preserve the source's integer AddOrUpdate overload: its 60000 argument is
// seconds despite the original comments describing a one-minute duration.
struct SystemFolders {
    std::mutex mutex;
    std::map<int, std::optional<InventoryFolder>> values;
};

struct FolderCaches {
    framework::ExpiringCacheOS<Uuid, std::optional<InventoryFolder>, framework::UuidHash> roots;
    framework::ExpiringCacheOS<Uuid, std::shared_ptr<SystemFolders>, framework::UuidHash> types;
};

FolderCaches &folder_caches() {
    static FolderCaches caches;
    return caches;
}
} // namespace

struct HGInventoryBroker::Impl {
    std::shared_ptr<InventoryService> local;
    HGInventoryDependencies dependencies;
    std::mutex urls_mutex, connectors_mutex, local_requests_mutex;
    std::unordered_map<Uuid, std::string, framework::UuidHash> urls;
    framework::ExpiringCacheOS<std::string, std::shared_ptr<InventoryService>> connectors;

    std::optional<std::string> url(Uuid user) {
        {
            std::lock_guard lock(urls_mutex);
            if (const auto found = urls.find(user); found != urls.end())
                return found->second;
        }
        auto value = dependencies.inventory_url ? dependencies.inventory_url(user) : std::nullopt;
        if (!value || value->empty())
            return {};
        const auto first = value->find_first_not_of('/');
        *value = first == std::string::npos ? "" : value->substr(first, value->find_last_not_of('/') - first + 1);
        std::lock_guard lock(urls_mutex);
        urls[user] = *value;
        return value;
    }

    std::shared_ptr<InventoryService> service(const std::optional<std::string> &url) {
        if (!url)
            return local;
        std::lock_guard lock(connectors_mutex);
        std::shared_ptr<InventoryService> result;
        if (!connectors.try_get_value(*url, result))
            result = dependencies.connector(*url);
        if (result)
            connectors.add_or_update(*url, result, 60000);
        if (!result)
            throw std::runtime_error("Could not create Hypergrid inventory connector");
        return result;
    }

    std::shared_ptr<InventoryService> service(Uuid user) {
        return service(url(user));
    }
};

HGInventoryBroker::HGInventoryBroker(InventoryFolderStore &store, std::unique_ptr<InventoryService> local,
                                     HGInventoryDependencies dependencies)
    : InventoryService(store), impl_(std::make_unique<Impl>()) {
    if (!local || !dependencies.connector)
        throw std::invalid_argument("Hypergrid inventory broker requires local service and connector factory");
    impl_->local = std::move(local);
    impl_->dependencies = std::move(dependencies);
}

HGInventoryBroker::~HGInventoryBroker() = default;

std::optional<std::string> HGInventoryBroker::inventory_url(Uuid user) {
    return impl_->url(user);
}

void HGInventoryBroker::client_closed(Uuid user, bool active_root_remaining) {
    if (active_root_remaining)
        return;
    {
        std::lock_guard lock(impl_->urls_mutex);
        impl_->urls.erase(user);
    }
    folder_caches().roots.remove(user);
    folder_caches().types.remove(user);
}

bool HGInventoryBroker::create_user_inventory(const Uuid &user) {
    return impl_->local->create_user_inventory(user);
}

std::optional<std::vector<InventoryFolder>> HGInventoryBroker::skeleton(const Uuid &user) {
    const auto url = impl_->url(user);
    const auto service = impl_->service(url);
    if (!url) {
        std::lock_guard lock(impl_->local_requests_mutex);
        return service->skeleton(user);
    }
    return service->skeleton(user);
}

std::optional<InventoryFolder> HGInventoryBroker::root(const Uuid &user) {
    std::optional<InventoryFolder> result;
    if (folder_caches().roots.try_get_value(user, result) && result)
        return result;
    const auto url = impl_->url(user);
    result = impl_->service(url)->root(user);
    if (url)
        folder_caches().roots.add_or_update(user, result, 60000);
    return result;
}

std::optional<InventoryFolder> HGInventoryBroker::for_type(const Uuid &user, int type) {
    std::shared_ptr<SystemFolders> folders;
    if (folder_caches().types.try_get_value(user, folders)) {
        std::lock_guard lock(folders->mutex);
        if (const auto found = folders->values.find(type); found != folders->values.end() && found->second)
            return found->second;
    }
    const auto url = impl_->url(user);
    auto result = impl_->service(url)->for_type(user, type);
    if (url) {
        if (!folder_caches().types.try_get_value(user, folders)) {
            folders = std::make_shared<SystemFolders>();
            folder_caches().types.add(user, folders, 60000);
        }
        std::lock_guard lock(folders->mutex);
        folders->values.try_emplace(type, result);
    }
    return result;
}

std::optional<InventoryFolder> HGInventoryBroker::folder(const Uuid &user, const Uuid &id) {
    return impl_->service(user)->folder(user, id);
}

std::optional<InventoryItem> HGInventoryBroker::item(const Uuid &user, const Uuid &id) {
    return impl_->service(user)->item(user, id);
}

std::optional<InventoryCollection> HGInventoryBroker::content(const Uuid &user, const Uuid &id) {
    return impl_->service(user)->content(user, id);
}

InventoryCollections HGInventoryBroker::multiple_content(const Uuid &user, const std::vector<Uuid> &ids) {
    if (!impl_->url(user))
        return impl_->local->multiple_content(user, ids);
    std::vector<std::optional<InventoryCollection>> result;
    for (const auto &id : ids)
        result.push_back(content(user, id));
    return result;
}

InventoryItemResults HGInventoryBroker::multiple_items(const Uuid &user, const std::vector<Uuid> &ids) {
    return ids.empty() ? InventoryItemResults(std::vector<std::optional<InventoryItem>>{})
                       : impl_->service(user)->multiple_items(user, ids);
}

std::optional<std::vector<InventoryItem>> HGInventoryBroker::folder_items(const Uuid &user, const Uuid &id) {
    return impl_->service(user)->folder_items(user, id);
}

bool HGInventoryBroker::add_folder(InventoryFolder folder) {
    return impl_->service(folder.owner)->add_folder(std::move(folder));
}

bool HGInventoryBroker::update_folder(InventoryFolder folder) {
    return impl_->service(folder.owner)->update_folder(std::move(folder));
}

bool HGInventoryBroker::move_folder(const InventoryFolder &folder) {
    return impl_->service(folder.owner)->move_folder(folder);
}

bool HGInventoryBroker::add_item(const InventoryItem &item) {
    return impl_->service(item.owner)->add_item(item);
}

bool HGInventoryBroker::update_item(InventoryItem &item) {
    return impl_->service(item.owner)->update_item(item);
}

bool HGInventoryBroker::move_items(const Uuid &user, const std::vector<InventoryItem> &items) {
    return items.empty() || impl_->service(user)->move_items(user, items);
}

bool HGInventoryBroker::delete_items(const Uuid &user, const std::vector<Uuid> &ids) {
    return ids.empty() || impl_->service(user)->delete_items(user, ids);
}

bool HGInventoryBroker::delete_folders(const Uuid &user, const std::vector<Uuid> &ids, bool trash) {
    return !ids.empty() && impl_->service(user)->delete_folders(user, ids, trash);
}

bool HGInventoryBroker::purge_folder(const Uuid &id, bool trash) {
    // Ownerless legacy callers only address the local grid. Viewer operations
    // use the folder overload so a visitor's purge reaches its home inventory.
    return impl_->local->purge_folder(id, trash);
}

bool HGInventoryBroker::purge_folder(const InventoryFolder &folder, bool trash) {
    return impl_->service(folder.owner)->purge_folder(folder, trash);
}

std::optional<std::vector<InventoryItem>> HGInventoryBroker::active_gestures(const Uuid &) {
    return std::vector<InventoryItem>{};
}

int HGInventoryBroker::asset_permissions(const Uuid &user, const Uuid &asset) {
    const auto url = impl_->url(user);
    const auto service = impl_->service(url);
    if (!url) {
        std::lock_guard lock(impl_->local_requests_mutex);
        return service->asset_permissions(user, asset);
    }
    return service->asset_permissions(user, asset);
}
} // namespace opensim::services
