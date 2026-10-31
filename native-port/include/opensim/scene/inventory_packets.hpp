// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_store.hpp"
#include "opensim/network/lludp.hpp"
#include <functional>
namespace opensim::scene {
class InventoryPacketNullError:public std::runtime_error {public:InventoryPacketNullError():std::runtime_error("Original inventory packet has a null item or uninitialized block") {}};
// Emit immediately on the Asset throttle. The bool is original OutPacket's
// automatic-splitting argument: true for FetchInventoryReply, false for folders.
// Callback shape preserves already-emitted packets if a later source batch fails.
using InventoryPacketSink=std::function<void(network::lludp::Packet,bool)>;
network::lludp::Packet task_inventory_reply(framework::Uuid part,std::int16_t serial,std::span<const std::uint8_t> filename);
void inventory_item_details(framework::Uuid agent,std::span<const std::optional<services::InventoryItem>>,const InventoryPacketSink&);
void inventory_folder_details(framework::Uuid agent,framework::Uuid owner,framework::Uuid folder,
 std::span<const std::optional<services::InventoryItem>> items,std::span<const std::optional<services::InventoryFolder>> folders,
 int version,int descendants,bool fetch_folders,bool fetch_items,const InventoryPacketSink&);
}
