// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/services/event_queue.hpp"
#include "opensim/network/lludp.hpp"

namespace opensim::scene {

struct InventoryNotificationBackend {
    // An empty callback represents a missing IEventQueue in the original implementation.
    std::function<bool(server::PollBytes, framework::Uuid)> enqueue;
    std::function<void(std::string)> debug;
};

server::PollBytes remove_inventory_items_event(framework::Uuid agent,
                                               framework::Uuid session,
                                               const std::vector<framework::Uuid>* items);
server::PollBytes remove_inventory_folders_event(framework::Uuid agent,
                                                 framework::Uuid session,
                                                 const std::vector<framework::Uuid>* folders);
void send_remove_inventory_items(framework::Uuid agent,
                                 framework::Uuid session,
                                 const std::vector<framework::Uuid>* items,
                                 const InventoryNotificationBackend& backend);
void send_remove_inventory_folders(framework::Uuid agent,
                                   framework::Uuid session,
                                   const std::vector<framework::Uuid>* folders,
                                   const InventoryNotificationBackend& backend);

// The original prefix check uses CurrentCulture. The native request thread
// defaults to invariant collation. Supply a callback to match another culture.
using AlertPrefix = std::function<bool(std::string_view, std::string_view)>;

network::lludp::Packet agent_alert(framework::Uuid agent,
                                   const std::optional<std::string>& message,
                                   bool modal,
                                   AlertPrefix prefix = {});

} // namespace opensim::scene
