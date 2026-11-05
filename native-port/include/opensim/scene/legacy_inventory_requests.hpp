// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/inventory_packets.hpp"
namespace opensim::scene {
struct LegacyItemRequest {framework::Uuid owner,item;};
struct LegacyFolderRequest {
    framework::Uuid folder;
    bool fetch_folders = false, fetch_items = false;
};
struct LegacyInventoryBackend {
 std::function<std::optional<services::InventoryItem>(framework::Uuid,framework::Uuid)> item;
 std::function<services::InventoryCollection(framework::Uuid,framework::Uuid)> content;
 std::function<std::optional<services::InventoryFolder>(framework::Uuid,framework::Uuid)> folder;
 std::function<std::optional<services::InventoryCollection>(framework::Uuid)> library_folder;
};
enum class LegacyFolderDisposition {Ignored,Answered,Queued};
class LegacyInventoryRequests {
public:
 explicit LegacyInventoryRequests(LegacyInventoryBackend);
 bool process_items(framework::Uuid agent,std::span<const LegacyItemRequest>,const std::function<bool()>& is_active,const InventoryPacketSink&)const;
 LegacyFolderDisposition receive_folder(framework::Uuid agent,const LegacyFolderRequest&,const InventoryPacketSink&)const;
 // true means the original ordinary-folder worker reaches its Sleep(50).
 // Exceptions propagate before that gate; the host owns queue-level catches.
 bool process_folder(framework::Uuid agent,const LegacyFolderRequest&,const std::function<bool()>& is_active,const InventoryPacketSink&)const;
private:
 void send_folder(framework::Uuid,const LegacyFolderRequest&,const services::InventoryCollection&,int version,int descendants,const InventoryPacketSink&)const;
 LegacyInventoryBackend backend_;
};
}
