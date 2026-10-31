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
#include "opensim/network/throttle_rates.hpp"
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
namespace opensim::network {
struct UdpClientAccounting {
    std::atomic<std::int32_t> unacked_bytes{0};
    std::function<void(std::int32_t)> expire_packets,acknowledge_packets;
};
struct PacketBuffer {
    std::vector<std::uint8_t> data;
    std::int32_t data_length=0;
};
struct OutgoingPacket {
    std::shared_ptr<UdpClientAccounting> client;
    std::shared_ptr<PacketBuffer> buffer;
    std::uint32_t sequence_number=0;
    std::int32_t resend_count=0;
    std::atomic<std::int32_t> tick_count{0};
    ThrottleOutPacketType category=ThrottleOutPacketType::Unknown;
    std::function<void(const std::shared_ptr<OutgoingPacket>&)> unacked_method;
};
class UnackedPacketCollection {
public:
    using Packet=std::shared_ptr<OutgoingPacket>;
    using Clock=std::function<std::int32_t()>;
    using FreeBuffer=std::function<void(const std::shared_ptr<PacketBuffer>&)>;
    explicit UnackedPacketCollection(Clock clock={},FreeBuffer free_buffer={});
    void clear();
    std::int32_t count()const;
    std::uint32_t oldest()const noexcept{return oldest_.load();}
    void add(const Packet& packet);
    void acknowledge(std::uint32_t sequence,std::int32_t time,bool from_resend);
    void remove(std::uint32_t sequence);
    std::optional<std::vector<Packet>> get_expired_packets(std::int32_t timeout_ms);
private:
    struct PendingAck{std::uint32_t sequence;std::int32_t time;bool from_resend;};
    void process_queues();
    void free(const Packet& packet);
    void require_open()const;
    Clock clock_;
    FreeBuffer free_buffer_;
    mutable std::mutex consumer_mutex_,pending_mutex_;
    std::map<std::uint32_t,Packet> packets_;
    std::deque<Packet> pending_adds_;
    std::deque<PendingAck> pending_acks_;
    std::deque<std::uint32_t> pending_removes_;
    std::atomic<std::uint32_t> oldest_{0};
    bool closed_=false;
};
}
