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
#include "opensim/network/circuit_transport.hpp"
#include "opensim/network/packet_history.hpp"
#include "opensim/network/client_packet_queue.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
namespace opensim::network {
using framework::Uuid;
using lludp::Packet;
namespace {
std::int32_t transport_tick() {
    return static_cast<std::int32_t>(
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count()) &
        0x7fffffffu);
}
double timestamp_ms() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
std::int32_t difference(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
void sequence(std::vector<std::uint8_t>& bytes, std::uint32_t number) {
    for (int i = 0; i < 4; ++i)
        bytes.at(i + 1) = static_cast<std::uint8_t>(number >> ((3 - i) * 8));
}
std::vector<std::uint8_t> encode_outbound(std::vector<std::uint8_t> bytes) {
    constexpr std::uint8_t zerocoded_flag = 0x80;
    if ((bytes.front() & zerocoded_flag) != 0) {
        auto compressed = lludp::zero_encode(bytes);
        if (compressed.size() <= bytes.size())
            return compressed;
        bytes.front() &= static_cast<std::uint8_t>(~zerocoded_flag);
    }
    return bytes;
}
}
struct CircuitTransport::Impl : std::enable_shared_from_this<Impl> {
    struct Client {
        CircuitConnection info;
        bool connected = true, paused = false;
        std::int32_t last_received = 0, last_ack = 0, last_ping = 0, rto = 1000, ping_ms = 20;
        double last_start_ping_ms = 0;
        std::uint32_t next_sequence = 0;
        std::uint8_t next_ping = 0;
        std::size_t bytes_since_ack = 0;
        IncomingPacketHistoryCollection archive{1024};
        std::deque<std::uint32_t> pending_acks;
        std::shared_ptr<UdpClientAccounting> accounting = std::make_shared<UdpClientAccounting>();
        UnackedPacketCollection unacked;
        ClientPacketQueue outgoing;
        HasUpdates has_updates;
        QueueEmpty queue_empty;
        bool queue_empty_running = false;
        double next_queue_empty = 0;
        bool texture_refill_ready = false;
        double next_texture_refill = 0;
        Client(CircuitConnection value, const Clock& clock, const CircuitTransportOptions& options,
               TokenBucket* parent, TokenBucket::Clock bandwidth_clock)
            : info(std::move(value)), rto(options.default_rto_ms), unacked(clock),
              outgoing(options.throttle_rates, parent, std::move(bandwidth_clock)) {
            last_received = last_ack = last_ping = clock() & 0x7fffffff;
        }
    };
    framework::AgentCircuitManager& circuits;
    Sender sender;
    Admission admit;
    PacketHandler handler;
    Disconnect disconnected;
    CircuitTransportOptions options;
    Clock clock;
    std::function<double()> precise_clock;
    Scheduler schedule;
    std::unique_ptr<TokenBucket> region_throttle;
    struct Pending {
        std::int32_t started;
        std::deque<std::vector<std::uint8_t>> packets;
    };
    std::unordered_map<UdpEndpoint, std::shared_ptr<Pending>, UdpEndpointHash> pending;
    mutable std::recursive_mutex mutex;
    bool closed = false;
    std::unordered_map<UdpEndpoint, std::shared_ptr<Client>, UdpEndpointHash> endpoints;
    std::unordered_map<Uuid, std::shared_ptr<Client>, framework::UuidHash> agents;
    CircuitTransportStatistics stats;
    Impl(framework::AgentCircuitManager& manager, Sender send, Admission admission, PacketHandler receive,
         Disconnect disconnect, CircuitTransportOptions settings, Clock time, Scheduler executor)
        : circuits(manager), sender(std::move(send)), admit(std::move(admission)),
          handler(std::move(receive)), disconnected(std::move(disconnect)), options(settings),
          clock(time ? time : Clock{transport_tick}),
          precise_clock(time ? std::function<double()>{[time] { return static_cast<double>(time()); }}
                             : std::function<double()>{timestamp_ms}),
          schedule(std::move(executor)) {
        const auto rate = static_cast<float>(options.scene_throttle_max_bps);
        region_throttle = std::make_unique<TokenBucket>(nullptr, rate, rate * .01f,
                                                        [time = precise_clock] { return time() * .001; });
    }
    void begin_queue_empty(std::shared_ptr<Client> client, ThrottleOutPacketTypeFlags categories) {
        if (closed || !client->connected || client->queue_empty_running || !client->has_updates ||
            !client->queue_empty || !schedule)
            return;
        const auto has_updates = client->has_updates;
        bool updates = false;
        try {
            updates = has_updates(client->info, categories);
        } catch (...) {
            ++stats.callback_errors;
            return;
        }
        if (!updates || closed || !client->connected)
            return;
        const auto now = precise_clock();
        if (now < client->next_queue_empty)
            return;
        client->queue_empty_running = true;
        client->next_queue_empty = now + 20;
        std::weak_ptr<Impl> owner = shared_from_this();
        std::weak_ptr<Client> destination = client;
        try {
            schedule([owner, destination, categories] {
                auto self = owner.lock();
                auto client = destination.lock();
                if (!self || !client)
                    return;
                std::unique_lock lock(self->mutex);
                if (self->closed || !client->connected) {
                    client->queue_empty_running = false;
                    return;
                }
                auto callback = client->queue_empty;
                auto info = client->info;
                lock.unlock();
                bool failed = false;
                if (callback)
                    try {
                        callback(info, categories);
                    } catch (...) {
                        failed = true;
                    }
                lock.lock();
                if (failed)
                    ++self->stats.callback_errors;
                client->queue_empty_running = false;
            });
        } catch (...) {
            ++stats.callback_errors;
            client->queue_empty_running = false;
        }
    }
    void receive(const UdpEndpoint&, std::span<const std::uint8_t>);
    void admit_circuit(const UdpEndpoint& source, const Packet& packet,
                       const std::shared_ptr<Pending>& work) {
        std::unique_lock lock(mutex);
        if (closed)
            return;
        const auto queued_work = pending.find(source);
        if (queued_work == pending.end() || queued_work->second != work)
            return;
        auto erase_pending = [&] {
            auto found = pending.find(source);
            if (found != pending.end() && found->second == work)
                pending.erase(found);
        };
        const auto& code = packet.block("CircuitCode");
        const auto number = static_cast<std::uint32_t>(std::get<std::uint64_t>(code.at("Code")));
        const auto agent = std::get<Uuid>(code.at("ID")), session = std::get<Uuid>(code.at("SessionID"));
        auto authentication = circuits.authenticate_session(session, agent, number);
        if (!authentication.authorised || !authentication.login_info) {
            ++stats.rejected_credentials;
            erase_pending();
            return;
        }
        const auto data = circuits.get_agent_circuit_data(number);
        if (!data || agents.contains(agent) || !admit) {
            erase_pending();
            return;
        }
        lock.unlock();
        bool accepted = false;
        bool failed = false;
        try {
            accepted = admit(data, *authentication.login_info, source);
        } catch (...) {
            failed = true;
        }
        lock.lock();
        if (failed)
            ++stats.callback_errors;
        if (!accepted || closed || agents.contains(agent) || endpoints.contains(source)) {
            erase_pending();
            return;
        }
        auto client =
            std::make_shared<Client>(CircuitConnection{agent, session, number, source}, clock, options,
                                     region_throttle.get(), [time = precise_clock] { return time() * .001; });
        std::weak_ptr<Client> weak = client;
        client->accounting->expire_packets = [weak](int count) {
            if (auto value = weak.lock())
                value->outgoing.flow().expire_packets(count);
        };
        client->accounting->acknowledge_packets = [weak](int count) {
            if (auto value = weak.lock())
                value->outgoing.flow().acknowledge_packets(count);
        };
        endpoints[source] = client;
        agents[agent] = client;
        ++stats.admitted;
        auto found = pending.find(source);
        if (found != pending.end() && found->second == work) {
            auto queued = std::move(work->packets);
            pending.erase(found);
            for (const auto& bytes : queued)
                receive(source, bytes);
        }
    }
    bool emit(const UdpEndpoint& target, std::span<const std::uint8_t> bytes) {
        ++stats.sent;
        try {
            if (sender(target, bytes))
                return true;
        } catch (...) {
            ++stats.callback_errors;
        }
        ++stats.send_errors;
        return false;
    }
    void acknowledge_immediate(const UdpEndpoint& target, std::uint32_t number) {
        auto ack = lludp::make_packet("PacketAck");
        ack.reliable = false;
        const auto& block = ack.spec->blocks.front();
        auto value = lludp::make_block(block);
        value["ID"] = static_cast<std::uint64_t>(number);
        ack.blocks["Packets"].push_back(std::move(value));
        emit(target, lludp::encode_datagram(ack));
    }
    void transmit(std::shared_ptr<Client> client, const std::shared_ptr<OutgoingPacket>& frame) {
        if (!client->connected || !frame->buffer)
            return;
        const auto buffer = frame->buffer;
        auto& bytes = buffer->data;
        const bool resend = (bytes[0] & 0x20) != 0;
        if (resend) {
            ++stats.resent;
        } else {
            frame->sequence_number = ++client->next_sequence;
            sequence(bytes, frame->sequence_number);
        }
        emit(client->info.endpoint, std::span(bytes).first(static_cast<std::size_t>(buffer->data_length)));
        if (!client->connected || closed)
            return;
        frame->tick_count = clock() & 0x7fffffff;
        if (frame->unacked_method) {
            if (!resend)
                client->unacked.add(frame);
        } else
            frame->buffer.reset();
    }
    std::shared_ptr<OutgoingPacket>
    make_frame(std::shared_ptr<Client> client, const Packet& packet,
               std::function<void(const std::shared_ptr<OutgoingPacket>&)> on_unacked = {},
               std::vector<std::uint8_t> encoded = {}) {
        auto frame = std::make_shared<OutgoingPacket>();
        frame->client = client->accounting;
        frame->buffer = std::make_shared<PacketBuffer>();
        frame->buffer->data =
            encode_outbound(encoded.empty() ? lludp::encode_packet(packet) : std::move(encoded));
        frame->buffer->data_length = static_cast<std::int32_t>(frame->buffer->data.size());
        if (packet.reliable) {
            if (on_unacked)
                frame->unacked_method = std::move(on_unacked);
            else {
                std::weak_ptr<Impl> owner = shared_from_this();
                std::weak_ptr<Client> destination = client;
                frame->unacked_method = [owner, destination](const auto& expired) {
                    if (auto self = owner.lock())
                        if (auto client = destination.lock()) {
                            std::lock_guard lock(self->mutex);
                            if (!self->closed && client->connected && expired->buffer) {
                                expired->buffer->data[0] |= 0x20;
                                ++expired->resend_count;
                                expired->category = ThrottleOutPacketType::Resend;
                                client->outgoing.enqueue(expired);
                            }
                        }
                };
            }
        }
        return frame;
    }
    std::uint32_t send_packet(std::shared_ptr<Client> client, Packet packet,
                              std::function<void(const std::shared_ptr<OutgoingPacket>&)> on_unacked = {}) {
        auto frame = make_frame(client, packet, std::move(on_unacked));
        transmit(client, frame);
        return frame->sequence_number;
    }
    void send_acks(const std::shared_ptr<Client>& client) {
        while (client->connected && !closed && !client->pending_acks.empty()) {
            auto ack = lludp::make_packet("PacketAck");
            ack.reliable = false;
            const auto& block = ack.spec->blocks.front();
            auto& rows = ack.blocks["Packets"];
            while (!client->pending_acks.empty() && rows.size() < 255) {
                auto value = lludp::make_block(block);
                value["ID"] = static_cast<std::uint64_t>(client->pending_acks.front());
                client->pending_acks.pop_front();
                rows.push_back(std::move(value));
            }
            send_packet(client, std::move(ack));
        }
    }
    void remove(std::shared_ptr<Client> client) {
        if (!client->connected)
            return;
        client->connected = false;
        endpoints.erase(client->info.endpoint);
        agents.erase(client->info.agent);
        client->outgoing.clear();
        client->unacked.clear();
        if (disconnected)
            try {
                disconnected(client->info);
            } catch (...) {
                ++stats.callback_errors;
            }
    }
};
CircuitTransport::CircuitTransport(framework::AgentCircuitManager& manager, Sender send, Admission admission,
                                   PacketHandler receive, Disconnect disconnect,
                                   CircuitTransportOptions options, Clock clock, Scheduler schedule) {
    if (!send || options.mtu < 16 || options.mtu > 65507 || options.ack_interval_ms < 1 ||
        options.ping_interval_ms < 1 || options.minimum_rto_ms < 1)
        throw std::invalid_argument("Invalid circuit transport configuration");
    impl_ = std::make_shared<Impl>(manager, std::move(send), std::move(admission), std::move(receive),
                                   std::move(disconnect), options, std::move(clock), std::move(schedule));
}
CircuitTransport::~CircuitTransport() {
    close();
}
void CircuitTransport::receive(const UdpEndpoint& source, std::span<const std::uint8_t> bytes) {
    impl_->receive(source, bytes);
}
void CircuitTransport::Impl::receive(const UdpEndpoint& source, std::span<const std::uint8_t> bytes) {
    auto self = shared_from_this();
    std::unique_lock lock(self->mutex);
    if (self->closed)
        return;
    ++self->stats.received;
    Packet packet;
    try {
        packet = lludp::decode_datagram(bytes);
    } catch (const std::exception&) {
        ++self->stats.malformed;
        return;
    }
    auto waiting = self->pending.find(source);
    if (waiting != self->pending.end() &&
        difference(self->clock() & 0x7fffffff, waiting->second->started) >= 60000) {
        self->pending.erase(waiting);
        waiting = self->pending.end();
    }
    if (waiting != self->pending.end()) {
        if (packet.spec->name == "UseCircuitCode")
            self->acknowledge_immediate(source, packet.sequence);
        else
            waiting->second->packets.emplace_back(bytes.begin(), bytes.end());
        return;
    }
    auto found = self->endpoints.find(source);
    if (found == self->endpoints.end()) {
        if (packet.spec->name != "UseCircuitCode") {
            ++self->stats.orphaned;
            return;
        }
        // This ACK confirms receipt, not admission. The baseline sends it even
        // when session authentication or scene admission subsequently fails.
        auto work = std::make_shared<Pending>();
        work->started = self->clock() & 0x7fffffff;
        self->pending[source] = work;
        self->acknowledge_immediate(source, packet.sequence);
        std::weak_ptr<Impl> weak = self;
        auto job = [weak, source, packet = std::move(packet), work] {
            if (auto owner = weak.lock())
                owner->admit_circuit(source, packet, work);
        };
        lock.unlock();
        try {
            if (self->schedule)
                self->schedule(std::move(job));
            else
                job();
        } catch (...) {
            lock.lock();
            ++self->stats.callback_errors;
            auto it = self->pending.find(source);
            if (it != self->pending.end() && it->second == work)
                self->pending.erase(it);
        }
        return;
    }
    const auto client = found->second;
    if (!client->connected)
        return;
    const auto now = self->clock() & 0x7fffffff;
    client->last_received = now;
    if (!packet.valid_ids(client->info.session, client->info.agent)) {
        ++self->stats.rejected_ids;
        return;
    }
    if (packet.appended_acks)
        for (auto sequence : packet.acknowledgements)
            client->unacked.acknowledge(sequence, now, packet.resent);
    if (packet.spec->name == "PacketAck") {
        for (const auto& row : packet.blocks.at("Packets"))
            client->unacked.acknowledge(static_cast<std::uint32_t>(std::get<std::uint64_t>(row.at("ID"))),
                                        now, packet.resent);
        return;
    }
    if (packet.reliable) {
        client->pending_acks.push_back(packet.sequence);
        client->bytes_since_ack += bytes.size();
        if (client->bytes_since_ack > static_cast<std::size_t>(self->options.mtu * 2)) {
            client->bytes_since_ack -= self->options.mtu * 2;
            self->send_acks(client);
        }
        if (!client->connected || self->closed)
            return;
        if (!client->archive.try_enqueue(packet.sequence)) {
            ++self->stats.duplicates;
            return;
        }
    }
    if (packet.spec->name == "StartPingCheck") {
        auto reply = lludp::make_packet("CompletePingCheck");
        reply.reliable = false;
        reply.block("PingID")["PingID"] = packet.block("PingID").at("PingID");
        self->send_packet(client, std::move(reply));
        return;
    }
    if (packet.spec->name == "CompletePingCheck") {
        const auto elapsed = self->precise_clock() - client->last_start_ping_ms;
        const auto smoothed = .8 * client->ping_ms + .2 * elapsed;
        client->ping_ms = std::isfinite(smoothed) && smoothed >= -2147483648.0 && smoothed < 2147483648.0
                              ? static_cast<std::int32_t>(smoothed)
                              : std::numeric_limits<std::int32_t>::min();
        const auto scaled = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(client->ping_ms) * 5u);
        if (scaled > self->options.maximum_rto_ms)
            client->rto = self->options.maximum_rto_ms;
        else
            client->rto = std::max(scaled, self->options.minimum_rto_ms);
        return;
    }
    if (packet.spec->name == "UseCircuitCode")
        return;
    if (self->handler)
        try {
            self->handler(client->info, packet);
        } catch (...) {
            ++self->stats.callback_errors;
        }
}
std::optional<std::uint32_t>
CircuitTransport::send(const Uuid& agent, Packet packet,
                       std::function<void(const std::shared_ptr<OutgoingPacket>&)> on_unacked) {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    if (self->closed)
        return std::nullopt;
    const auto found = self->agents.find(agent);
    if (found == self->agents.end())
        return std::nullopt;
    return self->send_packet(found->second, std::move(packet), std::move(on_unacked));
}
void CircuitTransport::poll() {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    if (self->closed)
        return;
    std::vector<std::shared_ptr<Impl::Client>> clients;
    for (const auto& [agent, client] : self->agents)
        clients.push_back(client);
    const auto now = self->clock() & 0x7fffffff;
    std::erase_if(self->pending,
                  [&](const auto& pair) { return difference(now, pair.second->started) >= 60000; });
    for (const auto& client : clients) {
        if (!client->connected)
            continue;
        const auto timeout =
            client->paused ? self->options.paused_ack_timeout_ms : self->options.ack_timeout_ms;
        if (difference(now, client->last_received) > timeout) {
            self->remove(client);
            continue;
        }
        if (difference(now, client->last_ack) >= self->options.ack_interval_ms) {
            client->last_ack = now;
            self->send_acks(client);
        }
        if (!client->connected)
            continue;
        if (auto expired = client->unacked.get_expired_packets(client->rto))
            for (const auto& packet : *expired)
                if (packet->unacked_method)
                    try {
                        packet->unacked_method(packet);
                    } catch (...) {
                        ++self->stats.callback_errors;
                    }
        if (!client->connected)
            continue;
        client->texture_refill_ready = false;
        client->outgoing.drain([&](const auto& frame) { self->transmit(client, frame); },
                               [&] { return client->unacked.count(); },
                               [&](auto categories) {
                                   client->texture_refill_ready =
                                       (static_cast<int>(categories) &
                                        static_cast<int>(ThrottleOutPacketTypeFlags::Texture)) != 0;
                                   self->begin_queue_empty(client, categories);
                               });
        if (!client->connected)
            continue;
        if (difference(now, client->last_ping) >= self->options.ping_interval_ms) {
            auto ping = lludp::make_packet("StartPingCheck");
            ping.reliable = false;
            ping.block("PingID")["PingID"] = static_cast<std::uint64_t>(client->next_ping++);
            ping.block("PingID")["OldestUnacked"] = static_cast<std::uint64_t>(client->unacked.oldest());
            self->send_packet(client, std::move(ping));
            client->last_ping = now;
            client->last_start_ping_ms = self->precise_clock();
        }
    }
}
bool CircuitTransport::enqueue(const Uuid& agent, Packet packet, ThrottleOutPacketType category,
                               std::function<void(const std::shared_ptr<OutgoingPacket>&)> on_unacked,
                               bool allow_splitting) {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    if (self->closed)
        return false;
    const auto found = self->agents.find(agent);
    if (found == self->agents.end())
        return false;
    auto client = found->second;
    std::vector<std::vector<std::uint8_t>> fragments;
    if (allow_splitting && packet.spec && packet.spec->name != "CoarseLocationUpdate" &&
        std::any_of(packet.spec->blocks.begin(), packet.spec->blocks.end(),
                    [](const auto& block) { return block.variable; }) &&
        lludp::packet_length(packet) + 20 > static_cast<std::size_t>(self->options.mtu)) {
        fragments = lludp::encode_packets(packet);
    } else
        fragments.push_back(lludp::encode_packet(packet));
    for (auto& bytes : fragments) {
        auto frame = self->make_frame(client, packet, on_unacked, std::move(bytes));
        frame->category = category;
        if (!client->outgoing.enqueue(frame))
            self->transmit(client, frame);
        if (!client->connected || self->closed)
            break;
    }
    return true;
}
std::optional<CircuitTransferMetrics> CircuitTransport::transfer_metrics(const Uuid& agent,
                                                                         const Uuid& session) const {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto it = self->agents.find(agent);
    if (self->closed || it == self->agents.end() || it->second->info.session != session)
        return {};
    return CircuitTransferMetrics{
        it->second->ping_ms, it->second->outgoing.setting(static_cast<int>(ThrottleOutPacketType::Task))};
}
void CircuitTransport::set_throttles(const Uuid& agent, std::span<const std::uint8_t> data, float factor) {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto found = self->agents.find(agent);
    if (found != self->agents.end())
        found->second->outgoing.set_throttles(data, factor);
}
bool CircuitTransport::enqueue_if_session(const Uuid& agent, const Uuid& session, Packet packet,
                                          ThrottleOutPacketType category, bool allow_splitting) {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto found = self->agents.find(agent);
    if (self->closed || found == self->agents.end() || found->second->info.session != session)
        return false;
    return enqueue(agent, std::move(packet), category, {}, allow_splitting);
}
std::optional<std::vector<std::uint8_t>> CircuitTransport::packed_throttles(const Uuid& agent,
                                                                            float multiplier) const {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto found = self->agents.find(agent);
    if (found == self->agents.end())
        return std::nullopt;
    return found->second->outgoing.packed_throttles(multiplier);
}
void CircuitTransport::set_update_callbacks(const Uuid& agent, HasUpdates has_updates,
                                            QueueEmpty queue_empty) {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    if ((has_updates || queue_empty) && !self->schedule)
        throw std::logic_error("Viewer update callbacks require a host executor");
    auto found = self->agents.find(agent);
    if (found != self->agents.end()) {
        found->second->has_updates = std::move(has_updates);
        found->second->queue_empty = std::move(queue_empty);
    }
}
bool CircuitTransport::take_texture_refill(const Uuid& agent, const Uuid& session) {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    const auto found = self->agents.find(agent);
    if (found == self->agents.end())
        return false;
    auto& client = *found->second;
    const auto now = self->precise_clock();
    if (client.info.session != session || !client.texture_refill_ready || now < client.next_texture_refill)
        return false;
    client.texture_refill_ready = false;
    client.next_texture_refill = now + 20.0;
    return true;
}
std::optional<int> CircuitTransport::bytes_can_send(const Uuid& agent, ThrottleOutPacketType category,
                                                    int time_ms) const {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto found = self->agents.find(agent);
    if (found == self->agents.end())
        return std::nullopt;
    return found->second->outgoing.bytes_can_send(category, time_ms);
}
void CircuitTransport::set_paused(const Uuid& agent, bool paused) {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto found = self->agents.find(agent);
    if (found != self->agents.end())
        found->second->paused = paused;
}
void CircuitTransport::cancel_reliable(const Uuid& agent, std::uint32_t number) {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto found = self->agents.find(agent);
    if (found != self->agents.end())
        found->second->unacked.remove(number);
}
void CircuitTransport::remove_client(const Uuid& agent) {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto found = self->agents.find(agent);
    if (found != self->agents.end())
        self->remove(found->second);
}
bool CircuitTransport::remove_client_if_session(const Uuid& agent, const Uuid& session) {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto found = self->agents.find(agent);
    if (found == self->agents.end() || found->second->info.session != session)
        return false;
    self->remove(found->second);
    return true;
}
void CircuitTransport::close() {
    auto self = impl_;
    if (!self)
        return;
    std::lock_guard lock(self->mutex);
    if (self->closed)
        return;
    self->closed = true;
    self->pending.clear();
    std::vector<std::shared_ptr<Impl::Client>> clients;
    for (const auto& [agent, client] : self->agents)
        clients.push_back(client);
    for (const auto& client : clients)
        self->remove(client);
}
std::optional<CircuitConnection> CircuitTransport::connection(const Uuid& agent) const {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto found = self->agents.find(agent);
    if (found == self->agents.end())
        return std::nullopt;
    return found->second->info;
}
CircuitTransportStatistics CircuitTransport::statistics() const {
    auto self = impl_;
    std::lock_guard lock(self->mutex);
    auto result = self->stats;
    result.clients = self->agents.size();
    for (const auto& [agent, client] : self->agents)
        result.unacked_bytes +=
            static_cast<std::uint64_t>(std::max(0, client->accounting->unacked_bytes.load()));
    return result;
}
}
