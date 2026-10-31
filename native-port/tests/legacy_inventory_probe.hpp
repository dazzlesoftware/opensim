// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/lludp.hpp"
#include <filesystem>
#include <functional>
namespace opensim::tests {
struct LegacyInventoryProbeIo {
 using Packet=network::lludp::Packet;
 std::function<Packet(const char*)> make;
 std::function<void(const Packet&)> send;
 std::function<void(std::function<bool(const std::vector<Packet>&)>)> wait;
 std::function<std::vector<Packet>()> snapshot;
};
// Call after RegionHandshakeReply and before CompleteAgentMovement. Io must
// perform actual production codec/network I/O; the helper fabricates no replies.
// Optional JSON item_name/item_flags default to the connected fixture values.
// Optional verify_partial_batch tests the original 12-item send's valid prefix.
void run_legacy_inventory_probe(framework::Uuid agent,const std::filesystem::path& metadata,const LegacyInventoryProbeIo&);
}
