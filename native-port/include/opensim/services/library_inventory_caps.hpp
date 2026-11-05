// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_caps.hpp"
namespace opensim::services {
struct LibraryItemBackend {
    std::optional<framework::Uuid> root_owner;
    std::function<std::optional<FetchInventory2Core::Items>(const std::vector<framework::Uuid>&)> get_items;
};
class FetchLibrary2Core {
public:
    // Absent backend is a missing ILibraryService. A present service must have
    // its actual root, matching the original constructor's root.Owner access.
    FetchLibrary2Core(framework::Uuid agent, std::optional<LibraryItemBackend> library);
    // Original FetchLibSimpleRequest after SimpleOSDMapHandler parsing. Provider
    // failures propagate to that wrapper; source does not catch them here.
    network::HttpResponse handle(const network::Value&, InventoryBadRequests*) const;

private:
    framework::Uuid agent_;
    std::optional<LibraryItemBackend> library_;
};
class FetchLibrary2Route final : public server::CapabilitySimpleHandler {
public:
    using Deserialize = FetchInventory2Route::Deserialize;
    FetchLibrary2Route(std::string path, std::shared_ptr<FetchLibrary2Core>,
                       std::shared_ptr<InventoryBadRequests>, Deserialize = {});
    std::optional<std::string> path() const override {
        return path_;
    }
    network::HttpResponse handle(const network::HttpRequest&) override;
    std::atomic<std::uint32_t> requests_received = 0, requests_handled = 0;

private:
    std::string path_;
    std::shared_ptr<FetchLibrary2Core> core_;
    std::shared_ptr<InventoryBadRequests> bad_;
    Deserialize deserialize_;
};
}
