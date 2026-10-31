// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_store.hpp"
#include "opensim/network/lludp.hpp"
namespace opensim::scene {
// LLClientView.SendInventoryItemCreateUpdate: send on Asset throttle using the
// real captured client/session; this function only constructs the packet.
network::lludp::Packet inventory_item_create(framework::Uuid agent,const services::InventoryItem&,framework::Uuid transaction={},std::uint32_t callback_id=0);
}
