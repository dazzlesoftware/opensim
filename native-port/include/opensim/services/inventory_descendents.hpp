// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_caps.hpp"
#include "opensim/services/inventory_diagnostics.hpp"
namespace opensim::services {
struct InventoryDescendentsBackend {
    using Collections = std::vector<std::optional<InventoryCollection>>;
    std::function<std::optional<Collections>(framework::Uuid, const std::vector<framework::Uuid>&)>
        multiple_content;
    std::function<std::optional<InventoryFolder>(framework::Uuid, framework::Uuid)> folder;
    FetchInventory2Core::GetMultipleItems multiple_items;
    // Captured from original LibraryRootFolder at handler construction.
    std::optional<framework::Uuid> library_owner;
    std::function<std::optional<InventoryCollection>(framework::Uuid)> library_content;
    std::function<network::Value(std::string_view)> deserialize_xml = network::deserialize_sdk_llsd_xml;
    // Legacy category callback; rich diagnostics take precedence when supplied.
    std::function<void(std::string)> log;
    InventoryDiagnostic diagnostic;
};
class FetchInventoryDescendentsCore {
  public:
    explicit FetchInventoryDescendentsCore(InventoryDescendentsBackend);
    // Source FetchInvDescHandler uses XML only, ignores method/content type,
    // returns empty folders on parse errors, and propagates provider failures.
    network::HttpResponse handle(const network::HttpRequest&, InventoryBadRequests*) const;

  private:
    InventoryDescendentsBackend backend_;
};
}
