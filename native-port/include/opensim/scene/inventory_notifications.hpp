// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/event_queue.hpp"
#include "opensim/network/lludp.hpp"
namespace opensim::scene {
struct InventoryNotificationBackend {
 // Empty means source missing IEventQueue; no UDP fallback is invented.
 std::function<bool(server::PollBytes,framework::Uuid)> enqueue;
 std::function<void(std::string)> debug;
};
server::PollBytes remove_inventory_items_event(framework::Uuid agent,framework::Uuid session,const std::vector<framework::Uuid>*);
server::PollBytes remove_inventory_folders_event(framework::Uuid agent,framework::Uuid session,const std::vector<framework::Uuid>*);
void send_remove_inventory_items(framework::Uuid agent,framework::Uuid session,const std::vector<framework::Uuid>*,const InventoryNotificationBackend&);
void send_remove_inventory_folders(framework::Uuid agent,framework::Uuid session,const std::vector<framework::Uuid>*,const InventoryNotificationBackend&);
// Source starts-with uses CurrentCulture; default is the native invariant
// request-thread profile. Inject matching prefix collation for another culture.
using AlertPrefix=std::function<bool(std::string_view,std::string_view)>;
network::lludp::Packet agent_alert(framework::Uuid agent,const std::optional<std::string>& message,bool modal,AlertPrefix prefix={});
}
