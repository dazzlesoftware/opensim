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
#include "opensim/network/client_packet_queue.hpp"
#include <bit>
#include <iostream>
using namespace opensim::network;
namespace {
int failures = 0;
void check(bool value, const char* name) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
auto packet(int category, int length = 100) {
    auto p = std::make_shared<OutgoingPacket>();
    p->category = static_cast<ThrottleOutPacketType>(category);
    p->buffer = std::make_shared<PacketBuffer>();
    p->buffer->data_length = length;
    return p;
}
void tests() {
    double now = 0;
    ClientPacketQueue queues(ThrottleRates{}, nullptr, [&] { return now; });
    std::vector<int> sent;
    int empty = 0, unacked = 0;
    auto drain = [&] {
        return queues.drain([&](const auto& p) { sent.push_back(static_cast<int>(p->category)); },
                            [&] { return unacked; }, [&](auto flags) { empty = static_cast<int>(flags); });
    };
    check(!queues.enqueue(packet(-1)) && !queues.enqueue(packet(7)),
          "unknown categories bypass throttle queues");
    for (int i = 6; i >= 0; --i)
        queues.enqueue(packet(i));
    check(!drain() && sent.empty() && queues.queued() == 0,
          "initial token timestamp preserves source wait and held packets excluded from queue count");
    now = .1;
    check(drain() && sent == std::vector<int>({0, 1, 2, 3, 4, 5, 6}),
          "one packet per category serviced in ascending order");
    check(empty == 63, "all ordinary categories report low-water mark");
    queues.enqueue(packet(0));
    queues.enqueue(packet(4));
    now += 1;
    unacked = 51;
    const auto count = sent.size();
    check(drain() && sent.size() == count + 1 && sent.back() == 0 &&
              queues.queued(ThrottleOutPacketType::Task) == 1,
          "resend serviced before ACK stall");
    unacked = 50;
    check(drain() && sent.back() == 4, "normal traffic resumes at fifty unacked packets");
    auto canceled = packet(1);
    queues.enqueue(canceled);
    canceled->buffer.reset();
    now += 1;
    const auto previous = sent.size();
    drain();
    check(sent.size() == previous, "canceled packets discarded");
    queues.enqueue(packet(1, 20000));
    queues.enqueue(packet(1));
    now += 1;
    drain();
    const auto first = sent.size();
    drain();
    check(sent.size() == first, "packet debt delays next packet");
    now += 4;
    drain();
    check(sent.size() == first + 1, "token refill releases held packet");
    auto bytes = queues.packed_throttles();
    check(bytes.size() == 28, "viewer throttle format length");
    queues.set_throttles(bytes);
    check(queues.packed_throttles() == bytes, "viewer throttles roundtrip bits to bytes");
    queues.force_setting(1, 1);
    check(queues.setting(1) == 1200 && queues.setting(0) == 0, "forced setting minimum and resend exclusion");
    check(queues.bytes_can_send(ThrottleOutPacketType::Resend, 1000) == 0, "category budget excludes resend");
    bool rejected = false;
    try {
        queues.set_throttles(std::span(bytes).first(27));
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    check(rejected, "truncated throttle data rejected");
    for (std::size_t i = 0; i < 7; ++i) {
        auto bits = std::bit_cast<std::uint32_t>(8000000.f);
        for (int b = 0; b < 4; ++b)
            bytes[i * 4 + b] = static_cast<std::uint8_t>(bits >> (8 * b));
    }
    queues.set_throttles(bytes);
    const auto packed = queues.packed_throttles();
    float total = 0;
    for (std::size_t i = 0; i < 7; ++i) {
        std::uint32_t bits = 0;
        for (int b = 0; b < 4; ++b)
            bits |= static_cast<std::uint32_t>(packed[i * 4 + b]) << (8 * b);
        total += std::bit_cast<float>(bits) * .125f;
    }
    check(total <= 640001 && total >= 639999, "viewer total bandwidth limited to client maximum");
    queues.clear();
    check(!queues.enqueue(packet(4)) && !drain(), "shutdown releases queues and prevents sends");
}
}
int main() {
    tests();
    return failures ? 1 : 0;
}
