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
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
namespace opensim::network {
struct UdpEndpoint {
    std::string address;
    std::uint16_t port = 0;
    std::uint32_t scope_id = 0;
    bool operator==(const UdpEndpoint&) const = default;
    std::string str() const;
};
struct UdpEndpointHash {
    std::size_t operator()(const UdpEndpoint& endpoint) const noexcept;
};
struct UdpSocketOptions {
    std::string address = "127.0.0.1";
    int port = 0;
    int receive_buffer_bytes = 0;
    std::size_t maximum_datagram = 65507;
};
struct UdpSocketStatistics {
    std::uint64_t received = 0;
    std::uint64_t sent = 0;
    std::uint64_t receive_errors = 0;
    std::uint64_t send_errors = 0;
    std::uint64_t handler_errors = 0;
    std::uint64_t oversized = 0;
};
class UdpSocket {
public:
    // The payload span is valid only until this callback returns. Retain a copy
    // when queuing work. Stop/destruction must be performed by the owning thread.
    using Receiver = std::function<void(const UdpEndpoint&, std::span<const std::uint8_t>)>;
    UdpSocket(UdpSocketOptions options, Receiver receiver);
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpEndpoint start();
    void stop();
    bool send_to(const UdpEndpoint& endpoint, std::span<const std::uint8_t> bytes);
    bool is_running() const noexcept;
    UdpSocketStatistics statistics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
