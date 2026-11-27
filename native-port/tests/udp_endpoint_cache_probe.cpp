// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/udp_endpoint_cache.hpp"
#include "opensim/framework/ip_address.hpp"
#include "pending_cache_clock.hpp"
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <thread>

using namespace opensim::network;

namespace {
std::vector<std::uint8_t> decode(std::string_view hex) {
    if (hex.size() % 2)
        throw std::invalid_argument("Odd hex address");
    std::vector<std::uint8_t> bytes;
    for (std::size_t index = 0; index < hex.size(); index += 2)
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(std::string(hex.substr(index, 2)), nullptr, 16)));
    return bytes;
}

UdpEndpoint endpoint(const SocketAddressSnapshot &value) {
    const auto &buffer = value.buffer;
    const bool v6 = buffer[0] == 23 || buffer[0] == 10 || (buffer[0] == 28 && buffer[1] == 30);
    std::array<std::uint8_t, 16> ip{};
    std::copy_n(buffer.begin() + (v6 ? 8 : 4), v6 ? 16 : 4, ip.begin());
    std::uint32_t scope = 0;
    if (v6)
        for (unsigned index = 0; index < 4; ++index)
            scope |= std::uint32_t(buffer[24 + index]) << (index * 8);
    if (ip[0] != 0xfe || (ip[1] & 0xc0) != 0x80)
        scope = 0;
    return {opensim::framework::IpAddress::from_bytes(ip, v6).str(),
            static_cast<std::uint16_t>((std::uint16_t(buffer[2]) << 8) | buffer[3]), scope};
}

void observe(std::string_view line) {
    std::istringstream input{std::string(line)};
    std::uint32_t seed, mask;
    input >> seed >> mask;
    PendingCacheClock clock;
    clock.interval = 300000;
    UdpEndpointCache cache(*clock.backend(), seed, mask);
    std::unordered_map<int, UdpEndpointCache::Address> slots;
    std::vector<UdpEndpointCache::Endpoint> identities;
    std::string command;
    bool first = true;
    while (input >> command) {
        UdpEndpointCache::Address key;
        bool query = false;
        if (command.starts_with('g') || command.starts_with('a')) {
            const auto comma = command.find(',');
            const auto last = command.rfind(',');
            const int slot = std::stoi(command.substr(1, comma - 1));
            auto &address = slots[slot];
            if (!address)
                address = std::make_shared<MutableSocketAddress>();
            address->assign(decode(command.substr(comma + 1, last - comma - 1)), std::stoul(command.substr(last + 1)));
            key = address;
            query = command[0] == 'g';
        } else if (command.starts_with('q')) {
            key = slots.at(std::stoi(command.substr(1)));
            query = true;
        } else if (command.starts_with('t')) {
            clock.now = std::stod(command.substr(1));
        } else if (command == "f") {
            clock.fire();
        } else {
            throw std::invalid_argument("Unknown endpoint cache command");
        }
        if (!first)
            std::cout << ';';
        first = false;
        if (query) {
            const auto value = key->snapshot();
            const auto hash = socket_address_hash(std::span(value.buffer).first(value.size), seed) & mask;
            const auto result = cache.get(key, endpoint);
            const auto found = std::find(identities.begin(), identities.end(), result);
            const auto id =
                found == identities.end() ? identities.size() + 1 : std::size_t(found - identities.begin()) + 1;
            if (found == identities.end())
                identities.push_back(result);
            std::cout << id << '|' << result->str() << '|' << hash;
        } else {
            std::cout << "-|-|-";
        }
        std::cout << '|' << cache.count() << '|' << clock.active();
    }
    std::cout << '\n';
}
} // namespace

void self_test() {
    PendingCacheClock clock;
    clock.interval = 300000;
    UdpEndpointCache cache(*clock.backend(), 1234);
    auto key = std::make_shared<MutableSocketAddress>();
    const auto raw = decode("020003e87f0000010000000000000000");
    key->assign(raw, raw.size());
    const auto first = cache.get(key, endpoint);
    auto equal = std::make_shared<MutableSocketAddress>();
    equal->assign(raw, raw.size());
    if (cache.get(equal, endpoint) != first || cache.count() != 1)
        throw std::runtime_error("Equal socket addresses must share cached endpoint identity");
    clock.now = 300000;
    clock.fire();
    if (cache.count() != 1)
        throw std::runtime_error("Endpoint entry must survive the exact expiry timestamp");
    clock.now = 300001;
    if (cache.get(equal, endpoint) != first)
        throw std::runtime_error("Lookup must slide the endpoint lifetime before purge");
    clock.fire();
    if (cache.count() != 1)
        throw std::runtime_error("Endpoint lookup must renew expiry");
    clock.now = 600002;
    clock.fire();
    if (cache.count() != 0 || cache.get(key, endpoint) == first)
        throw std::runtime_error("Purged endpoint must be recreated with a new identity");
    PendingCacheClock collision_clock;
    collision_clock.interval = 300000;
    UdpEndpointCache collisions(*collision_clock.backend(), 1234, 0);
    const auto frozen = collisions.get(key, endpoint);
    auto changed = raw;
    changed[3] = 0xe9;
    key->assign(changed, changed.size());
    if (collisions.get(key, endpoint) != frozen || frozen->port != 1000)
        throw std::runtime_error("Mutating a colliding cache key must retain the source's frozen endpoint value");
    PendingCacheClock concurrent_clock;
    concurrent_clock.interval = 300000;
    UdpEndpointCache concurrent(*concurrent_clock.backend(), 1234);
    const auto shared_address = std::make_shared<MutableSocketAddress>();
    shared_address->assign(raw, raw.size());
    const auto original_value = concurrent.get(shared_address, endpoint);
    shared_address->assign(changed, changed.size());
    const auto changed_value = concurrent.get(shared_address, endpoint);
    std::jthread writer([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            shared_address->assign(raw, raw.size());
            shared_address->assign(changed, changed.size());
        }
    });
    bool known_identity = true;
    for (int iteration = 0; iteration < 2000; ++iteration) {
        const auto value = concurrent.get(shared_address, endpoint);
        known_identity = known_identity && (value == original_value || value == changed_value);
    }
    writer.request_stop();
    writer.join();
    if (!known_identity || concurrent.count() != 2)
        throw std::runtime_error("Concurrent mutation must not break equality of the same address object");
    std::cout << "PASS: endpoint identity, sliding expiry, strict purge and mutable-key collisions\n";
}

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
            self_test();
            return 0;
        }
        std::string line;
        while (std::getline(std::cin, line))
            observe(line);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
