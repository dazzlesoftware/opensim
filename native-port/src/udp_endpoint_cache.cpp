// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// Socket hashing follows the .NET HashCode/xxHash algorithm. Notices:
// native-port/third_party/dotnet_hashcode/LICENSE.txt.
#include "opensim/network/udp_endpoint_cache.hpp"
#include "opensim/framework/uuid.hpp"
#include <bit>
#include <cstring>
#include <unordered_map>

namespace opensim::network {
void MutableSocketAddress::assign(std::span<const std::uint8_t> buffer, std::size_t size) {
    if (size > buffer.size())
        throw std::invalid_argument("Socket address size exceeds its buffer");
    std::lock_guard lock(mutex_);
    value_ = {std::vector<std::uint8_t>(buffer.begin(), buffer.end()), size};
}

SocketAddressSnapshot MutableSocketAddress::snapshot() const {
    std::lock_guard lock(mutex_);
    return value_;
}

std::uint32_t socket_address_hash(std::span<const std::uint8_t> bytes, std::uint32_t seed) {
    constexpr std::uint32_t prime1 = 2654435761u, prime2 = 2246822519u, prime3 = 3266489917u, prime4 = 668265263u,
                            prime5 = 374761393u;
    std::vector<std::uint32_t> words;
    words.reserve((bytes.size() + 3) / 4 + 3);
    while (bytes.size() >= 4) {
        std::uint32_t word;
        std::memcpy(&word, bytes.data(), sizeof(word));
        words.push_back(word);
        bytes = bytes.subspan(4);
    }
    for (auto byte : bytes)
        words.push_back(byte);
    std::uint32_t v1 = seed + prime1 + prime2, v2 = seed + prime2, v3 = seed, v4 = seed - prime1;
    const auto round = [](std::uint32_t hash, std::uint32_t value) {
        return std::rotl(hash + value * prime2, 13) * prime1;
    };
    std::size_t index = 0;
    for (; index + 4 <= words.size(); index += 4) {
        v1 = round(v1, words[index]);
        v2 = round(v2, words[index + 1]);
        v3 = round(v3, words[index + 2]);
        v4 = round(v4, words[index + 3]);
    }
    auto hash =
        words.size() < 4 ? seed + prime5 : std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
    hash += static_cast<std::uint32_t>(words.size()) * 4u;
    for (; index < words.size(); ++index)
        hash = std::rotl(hash + words[index] * prime3, 17) * prime4;
    hash ^= hash >> 15;
    hash *= prime2;
    hash ^= hash >> 13;
    hash *= prime3;
    return hash ^ (hash >> 16);
}

namespace {
using Address = UdpEndpointCache::Address;
using Endpoint = UdpEndpointCache::Endpoint;
using Mode = framework::ExpiringCacheLock::Mode;

class CacheGuard {
  public:
    CacheGuard(framework::ExpiringCacheLock &lock, Mode mode) : lock_(lock), mode_(mode) {
        lock_.enter(mode_);
    }

    ~CacheGuard() {
        lock_.exit(mode_);
    }

  private:
    framework::ExpiringCacheLock &lock_;
    Mode mode_;
};

// Dictionary entries retain their insertion hash even when their key mutates.
// Index by stored hash and retain the CLR's slot/free-list and resize ordering.
// A normal unordered_map keyed by mutable address bytes cannot preserve this.
template <class Value> class AddressDictionary {
    struct Entry {
        std::uint32_t hash;
        Address key;
        Value value;
        std::optional<std::size_t> next;
    };

    std::vector<std::optional<Entry>> slots_;
    std::vector<std::size_t> free_;
    std::unordered_map<std::uint32_t, std::optional<std::size_t>> heads_;
    std::size_t capacity_ = 0;

    std::optional<std::size_t> find(const Address &key, std::uint32_t hash) const {
        const auto head = heads_.find(hash);
        if (head == heads_.end())
            return {};
        std::optional<std::vector<std::uint8_t>> bytes;
        for (auto index = head->second; index; index = slots_[*index]->next) {
            // SocketAddress.Equals compares the same backing span equal even
            // while a receiver changes it. Two snapshots could disagree.
            if (slots_[*index]->key == key)
                return index;
            if (!bytes)
                bytes = key->snapshot().buffer;
            if (slots_[*index]->key->snapshot().buffer == *bytes)
                return index;
        }
        return {};
    }

    void grow() {
        constexpr std::size_t primes[] = {3,        7,        17,        37,        89,        197,       431,
                                          919,      1931,     4049,      8419,      17519,     36353,     75431,
                                          156437,   324449,   672827,    1395263,   2893249,   5999471,   12413977,
                                          25708489, 53352417, 110373619, 228896869, 474980747, 984939527, 2038479629};
        const auto minimum = capacity_ ? capacity_ * 2 : 3;
        const auto next =
            std::find_if(std::begin(primes), std::end(primes), [&](auto prime) { return prime >= minimum; });
        if (next == std::end(primes))
            throw std::length_error("Endpoint cache dictionary capacity exceeded");
        capacity_ = *next;
        heads_.clear();
        for (std::size_t index = 0; index < slots_.size(); ++index)
            if (slots_[index]) {
                auto &head = heads_[slots_[index]->hash];
                slots_[index]->next = head;
                head = index;
            }
    }

  public:
    Value *get(const Address &key, std::uint32_t hash) {
        const auto index = find(key, hash);
        return index ? &slots_[*index]->value : nullptr;
    }

    void set(const Address &key, std::uint32_t hash, Value value) {
        if (const auto index = find(key, hash)) {
            slots_[*index]->value = std::move(value);
            return;
        }
        if (free_.empty() && slots_.size() == capacity_)
            grow();
        const auto index = free_.empty() ? slots_.size() : free_.back();
        auto &head = heads_[hash];
        Entry entry{hash, key, std::move(value), head};
        if (free_.empty())
            slots_.emplace_back(std::move(entry));
        else {
            slots_[index] = std::move(entry);
            free_.pop_back();
        }
        head = index;
    }

    void remove(const Address &key, std::uint32_t hash) {
        const auto index = find(key, hash);
        if (!index)
            return;
        auto *link = &heads_[hash];
        while (*link != index)
            link = &slots_[**link]->next;
        *link = slots_[*index]->next;
        slots_[*index].reset();
        free_.push_back(*index);
    }

    std::size_t count() const {
        return slots_.size() - free_.size();
    }

    template <class Visitor> void each(Visitor visit) const {
        for (const auto &slot : slots_)
            if (slot)
                visit(slot->key, slot->value);
    }
};

std::int32_t integer(double value) {
    return !std::isfinite(value) || value < -2147483648.0 || value >= 2147483648.0
               ? std::numeric_limits<std::int32_t>::min()
               : static_cast<std::int32_t>(value);
}
} // namespace

struct UdpEndpointCache::State : std::enable_shared_from_this<State> {
    framework::ExpiringCacheBackend backend;
    framework::ExpiringCacheLock lock;
    framework::AtomicSharedPtr<framework::ExpiringCacheTimer> timer;
    AddressDictionary<std::int32_t> expires;
    AddressDictionary<Endpoint> values;
    std::atomic<std::size_t> entries = 0;
    double start;
    std::uint32_t seed, mask;

    State(framework::ExpiringCacheBackend clock, std::optional<std::uint32_t> hash_seed, std::uint32_t hash_mask)
        : backend(std::move(clock)), start(backend.timestamp()), seed(hash_seed.value_or(0)), mask(hash_mask) {
        if (!hash_seed) {
            const auto random = framework::Uuid::random();
            std::memcpy(&seed, random.bytes().data(), sizeof(seed));
        }
    }

    std::uint32_t hash(const Address &key) const {
        const auto value = key->snapshot();
        return socket_address_hash(std::span(value.buffer).first(value.size), seed) & mask;
    }

    std::int32_t expiration() const {
        const auto now = std::bit_cast<std::uint32_t>(integer(backend.timestamp() - start));
        return std::bit_cast<std::int32_t>(now + 300000u);
    }

    void stop_timer() {
        if (const auto value = timer.load()) {
            value->dispose();
            timer.store({});
        }
    }

    void check_timer() {
        if (!timer.load()) {
            std::weak_ptr<State> weak = shared_from_this();
            timer.store(backend.timer(
                [weak] {
                    if (const auto state = weak.lock())
                        state->purge();
                },
                300000));
        }
    }

    void purge() {
        CacheGuard upgradeable(lock, Mode::Upgradeable);
        if (expires.count() == 0) {
            stop_timer();
            return;
        }
        const auto now = integer(backend.timestamp() - start);
        std::vector<Address> expired;
        expires.each([&](const auto &key, auto expiry) {
            if (expiry > 0 && expiry < now)
                expired.push_back(key);
        });
        if (!expired.empty()) {
            CacheGuard write(lock, Mode::Write);
            for (const auto &key : expired) {
                expires.remove(key, hash(key));
                values.remove(key, hash(key));
            }
            entries = expires.count();
        }
        if (expires.count() == 0)
            stop_timer();
        else if (const auto active = timer.load())
            active->change(300000);
    }
};

UdpEndpointCache::UdpEndpointCache(framework::ExpiringCacheBackend backend, std::optional<std::uint32_t> seed,
                                   std::uint32_t hash_mask)
    : state_(std::make_shared<State>(std::move(backend), seed, hash_mask)) {}

UdpEndpointCache::~UdpEndpointCache() {
    state_->stop_timer();
}

UdpEndpointCache::Endpoint UdpEndpointCache::get(const Address &address, const Factory &create) {
    if (!address)
        throw std::invalid_argument("Socket address is null");
    const auto state = state_;
    {
        CacheGuard upgradeable(state->lock, Mode::Upgradeable);
        if (const auto found = state->values.get(address, state->hash(address))) {
            const auto result = *found;
            CacheGuard write(state->lock, Mode::Write);
            const auto expiry = state->expiration();
            state->expires.set(address, state->hash(address), expiry);
            state->entries = state->expires.count();
            return result;
        }
    }
    const auto result = std::make_shared<const UdpEndpoint>(create(address->snapshot()));
    {
        const auto expiry = state->expiration();
        CacheGuard write(state->lock, Mode::Write);
        state->expires.set(address, state->hash(address), expiry);
        state->values.set(address, state->hash(address), result);
        state->entries = state->expires.count();
        state->check_timer();
    }
    return result;
}

std::size_t UdpEndpointCache::count() const {
    return state_->entries;
}
} // namespace opensim::network
