// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_caps.hpp"
namespace opensim::services {
struct InventoryDescendentsBackend {
 using Collections=std::vector<std::optional<InventoryCollection>>;
 std::function<std::optional<Collections>(framework::Uuid,const std::vector<framework::Uuid>&)> multiple_content;
 std::function<std::optional<InventoryFolder>(framework::Uuid,framework::Uuid)> folder;
 FetchInventory2Core::GetMultipleItems multiple_items;
 // Captured from original LibraryRootFolder at handler construction.
 std::optional<framework::Uuid> library_owner;
 std::function<std::optional<InventoryCollection>(framework::Uuid)> library_content;
 std::function<network::Value(std::string_view)> deserialize_xml=network::parse_llsd_xml;
 // Categories only; platform exception text and source log formatting belong
 // to the host logger. No callbacks means source logs are discarded.
 std::function<void(std::string)> log;
};
class FetchInventoryDescendentsCore {
public:
 explicit FetchInventoryDescendentsCore(InventoryDescendentsBackend);
 // Source FetchInvDescHandler uses XML only, ignores method/content type,
 // returns empty folders on parse errors, and propagates provider failures.
 network::HttpResponse handle(const network::HttpRequest&,InventoryBadRequests*)const;
private:InventoryDescendentsBackend backend_;
};
}
