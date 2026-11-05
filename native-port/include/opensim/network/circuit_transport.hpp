/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#include "opensim/framework/agent_circuit.hpp"
#include "opensim/network/lludp.hpp"
#include "opensim/network/udp_socket.hpp"
#include "opensim/network/unacked_packets.hpp"
#include <functional>
#include <memory>
#include <optional>
namespace opensim::network {
struct CircuitConnection {
    framework::Uuid agent, session;
    std::uint32_t circuit_code = 0;
    UdpEndpoint endpoint;
};
struct CircuitTransportOptions {
    int mtu = 1400, ack_interval_ms = 100, ping_interval_ms = 5000;
    int default_rto_ms = 1000, minimum_rto_ms = 250, maximum_rto_ms = 3000;
    int ack_timeout_ms = 60000, paused_ack_timeout_ms = 300000;
    ThrottleRates throttle_rates{};
    int scene_throttle_max_bps = 0;
};
struct CircuitTransferMetrics {
    int ping_ms = 0, task_throttle = 0;
};
struct CircuitTransportStatistics {
    std::uint64_t received = 0, sent = 0, resent = 0, malformed = 0, orphaned = 0, rejected_credentials = 0,
                  rejected_ids = 0, duplicates = 0, admitted = 0, callback_errors = 0, send_errors = 0;
    std::size_t clients = 0;
    std::uint64_t unacked_bytes = 0;
};
class CircuitTransport {
  public:
    using Clock = std::function<std::int32_t()>;
    using Sender = std::function<bool(const UdpEndpoint&, std::span<const std::uint8_t>)>;
    // Returning true means the owning scene actually admitted this agent.
    // Missing admission handlers reject all circuits, even when credentials match.
    using Admission = std::function<bool(const std::shared_ptr<framework::AgentCircuitData>&,
                                         const framework::Login&, const UdpEndpoint&)>;
    using PacketHandler = std::function<void(const CircuitConnection&, const lludp::Packet&)>;
    using Disconnect = std::function<void(const CircuitConnection&)>;
    // A host executor may run admission on its scene/worker thread. It must
    // eventually execute or release each job; jobs hold only weak owner references.
    using Scheduler = std::function<void(std::function<void()>)>;
    using HasUpdates = std::function<bool(const CircuitConnection&, ThrottleOutPacketTypeFlags)>;
    using QueueEmpty = std::function<void(const CircuitConnection&, ThrottleOutPacketTypeFlags)>;
    CircuitTransport(framework::AgentCircuitManager& circuits, Sender send, Admission admit,
                     PacketHandler receive = {}, Disconnect disconnect = {},
                     CircuitTransportOptions options = {}, Clock clock = {}, Scheduler schedule = {});
    ~CircuitTransport();
    CircuitTransport(const CircuitTransport&) = delete;
    CircuitTransport& operator=(const CircuitTransport&) = delete;
    void receive(const UdpEndpoint& source, std::span<const std::uint8_t> bytes);
    std::optional<std::uint32_t>
    send(const framework::Uuid& agent, lludp::Packet packet,
         std::function<void(const std::shared_ptr<OutgoingPacket>&)> on_unacked = {});
    bool enqueue(const framework::Uuid& agent, lludp::Packet packet, ThrottleOutPacketType category,
                 std::function<void(const std::shared_ptr<OutgoingPacket>&)> on_unacked = {},
                 bool allow_splitting = true);
    // Reject stale producer work after the same agent opens a new session.
    bool enqueue_if_session(const framework::Uuid& agent, const framework::Uuid& session,
                            lludp::Packet packet, ThrottleOutPacketType category,
                            bool allow_splitting = true);
    std::optional<CircuitTransferMetrics> transfer_metrics(const framework::Uuid& agent,
                                                           const framework::Uuid& session) const;
    void set_throttles(const framework::Uuid& agent, std::span<const std::uint8_t> data, float factor = 1);
    std::optional<std::vector<std::uint8_t>> packed_throttles(const framework::Uuid& agent,
                                                              float multiplier = 1) const;
    // Update production runs through the configured host executor, at most one
    // callback per client at a time and no more often than every 20 ms.
    void set_update_callbacks(const framework::Uuid& agent, HasUpdates has_updates, QueueEmpty queue_empty);
    bool take_texture_refill(const framework::Uuid& agent, const framework::Uuid& session);
    std::optional<int> bytes_can_send(const framework::Uuid& agent, ThrottleOutPacketType category,
                                      int time_ms) const;
    void poll();
    void set_paused(const framework::Uuid& agent, bool paused);
    void cancel_reliable(const framework::Uuid& agent, std::uint32_t sequence);
    void remove_client(const framework::Uuid& agent);
    bool remove_client_if_session(const framework::Uuid& agent, const framework::Uuid& session);
    void close();
    std::optional<CircuitConnection> connection(const framework::Uuid& agent) const;
    CircuitTransportStatistics statistics() const;

  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
