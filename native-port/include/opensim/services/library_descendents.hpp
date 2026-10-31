// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_caps.hpp"
namespace opensim::services {
struct LibraryDescendentsBackend {
 std::function<std::optional<framework::Uuid>()> root_owner;
 std::function<std::optional<InventoryCollection>(framework::Uuid)> folder_content;
 std::function<network::Value(std::string_view)> deserialize_xml=network::parse_llsd_xml;
 std::function<void(std::string)> log;
};
class FetchLibraryDescendentsCore {
public:
 explicit FetchLibraryDescendentsCore(LibraryDescendentsBackend);
 network::HttpResponse handle(const network::HttpRequest&,InventoryBadRequests*,framework::Uuid agent)const;
private:LibraryDescendentsBackend backend_;framework::Uuid initial_owner_;
};
}
