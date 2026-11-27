// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_store.hpp"
#include "opensim/network/rest_forms.hpp"
#include "opensim/server/config.hpp"

namespace opensim::framework {
template <class K, class V, class Hash> class ExpiringCacheOS;
}

namespace opensim::services {
using RemoteInventoryItemCache =
    framework::ExpiringCacheOS<framework::Uuid, std::optional<InventoryItem>, framework::UuidHash>;
std::unique_ptr<InventoryService> remote_inventory_service(InventoryFolderStore &, const server::IniConfig &,
                                                           network::RestFormsRequest = {},
                                                           std::shared_ptr<network::WebHttpClients> = {},
                                                           std::shared_ptr<RemoteInventoryItemCache> = {});
} // namespace opensim::services
