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
#include "opensim/network/token_bucket.hpp"
#include "opensim/network/unacked_packets.hpp"
#include <array>
#include <deque>
#include <span>
namespace opensim::network {
// Like LLUDPClient.DequeueOutgoing, calls are serialized by the owning transport.
class ClientPacketQueue {
  public:
    using Packet = std::shared_ptr<OutgoingPacket>;
    using Send = std::function<void(const Packet&)>;
    using Empty = std::function<void(ThrottleOutPacketTypeFlags)>;
    explicit ClientPacketQueue(const ThrottleRates& rates = ThrottleRates{}, TokenBucket* parent = nullptr,
                               TokenBucket::Clock clock = {});
    bool enqueue(const Packet& packet);
    bool drain(const Send& send, const std::function<int()>& unacked_count, const Empty& empty = {});
    void clear();
    std::size_t queued(ThrottleOutPacketType category) const;
    std::size_t queued() const;
    void set_throttles(std::span<const std::uint8_t> data, float factor = 1);
    std::vector<std::uint8_t> packed_throttles(float multiplier = 1) const;
    int bytes_can_send(ThrottleOutPacketType category, int time_ms) const;
    void force_setting(int category, int setting, int mtu = 1200);
    int setting(int category) const;
    AdaptiveTokenBucket& flow() {
        return flow_;
    }

  private:
    float max_rate_, burst_time_;
    AdaptiveTokenBucket flow_;
    std::array<std::unique_ptr<TokenBucket>, 7> buckets_;
    std::array<std::deque<Packet>, 7> queues_;
    std::array<Packet, 7> next_;
    bool closed_ = false;
};
}
