// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/network/udp_socket.hpp"
#include <mutex>
#include <optional>
#include <vector>

namespace opensim::network {
struct SocketAddressSnapshot {
    std::vector<std::uint8_t> buffer;
    std::size_t size = 0;
};

// A receive socket reuses its SocketAddress object. Cache keys retain this
// object, including subsequent mutations; they are not immutable IP/port pairs.
class MutableSocketAddress {
  public:
    void assign(std::span<const std::uint8_t> buffer, std::size_t size);
    SocketAddressSnapshot snapshot() const;

  private:
    mutable std::mutex mutex_;
    SocketAddressSnapshot value_;
};

// The explicit seed allows comparison with the actual SDK's process seed.
std::uint32_t socket_address_hash(std::span<const std::uint8_t> bytes, std::uint32_t seed);

class UdpEndpointCache {
  public:
    using Address = std::shared_ptr<MutableSocketAddress>;
    using Endpoint = std::shared_ptr<const UdpEndpoint>;
    using Factory = std::function<UdpEndpoint(const SocketAddressSnapshot &)>;

    explicit UdpEndpointCache(framework::ExpiringCacheBackend backend = framework::native_expiring_cache_backend(),
                              std::optional<std::uint32_t> seed = {}, std::uint32_t hash_mask = 0xffffffffu);
    ~UdpEndpointCache();
    UdpEndpointCache(const UdpEndpointCache &) = delete;
    UdpEndpointCache &operator=(const UdpEndpointCache &) = delete;
    Endpoint get(const Address &address, const Factory &create);
    std::size_t count() const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace opensim::network
