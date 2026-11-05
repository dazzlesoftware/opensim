// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/host_resolver.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#endif
namespace opensim::framework {
namespace {
std::int32_t integer(double n) {
    return !std::isfinite(n) || n < -2147483648. || n >= 2147483648. ? INT32_MIN
                                                                     : static_cast<std::int32_t>(n);
}
std::int32_t expiry(std::int32_t now) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(now) + 300000u);
}
bool blank(const std::optional<std::string>& s) {
    if (!s)
        return true;
    for (auto c : utf16_replacing(*s))
        if (!unicode_whitespace(c))
            return false;
    return true;
}
std::optional<std::vector<HostResolver::Address>> lookup(const std::string& host) {
    if (host.find('\0') != std::string::npos)
        return {};
#ifdef _WIN32
    struct Runtime {
        Runtime() {
            WSADATA data;
            if (WSAStartup(MAKEWORD(2, 2), &data))
                throw std::runtime_error("Winsock startup failed");
        }
        ~Runtime() {
            WSACleanup();
        }
    };
    static Runtime runtime;
    ADDRINFOW hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    ADDRINFOW* found = nullptr;
    auto u16 = utf16_replacing(host);
    std::wstring wide(u16.begin(), u16.end());
    if (GetAddrInfoW(wide.c_str(), nullptr, &hints, &found))
        return {};
    std::unique_ptr<ADDRINFOW, decltype(&FreeAddrInfoW)> guard(found, FreeAddrInfoW);
#else
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &found))
        return {};
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> guard(found, freeaddrinfo);
#endif
    std::vector<HostResolver::Address> result;
    for (auto* row = found; row; row = row->ai_next) {
        std::array<std::uint8_t, 16> bytes{};
        bool v6 = false;
        std::uint32_t scope = 0;
        if (row->ai_family == AF_INET) {
            auto* p = reinterpret_cast<const sockaddr_in*>(row->ai_addr);
            std::copy_n(reinterpret_cast<const std::uint8_t*>(&p->sin_addr), 4, bytes.begin());
        } else if (row->ai_family == AF_INET6) {
            auto* p = reinterpret_cast<const sockaddr_in6*>(row->ai_addr);
            std::copy_n(reinterpret_cast<const std::uint8_t*>(&p->sin6_addr), 16, bytes.begin());
            scope = p->sin6_scope_id;
            v6 = true;
        } else
            continue;
        result.push_back(std::make_shared<IpAddress>(IpAddress::from_bytes(bytes, v6, scope)));
    }
    return result;
}
}
struct HostResolver::State {
    struct Entry {
        Address address;
        std::int32_t expires;
    };
    Lookup lookup;
    Clock clock;
    double start;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::map<std::string, Entry> entries;
    bool active = false, stop = false;
    std::chrono::steady_clock::time_point due;
    std::thread worker;
    State(Lookup l, Clock c, bool automatic) : lookup(std::move(l)), clock(std::move(c)), start(clock()) {
        if (automatic)
            worker = std::thread([this] {
                std::unique_lock lock(mutex);
                while (!stop) {
                    changed.wait(lock, [&] { return stop || active; });
                    if (stop)
                        break;
                    const auto deadline = due;
                    if (changed.wait_until(lock, deadline,
                                           [&] { return stop || !active || due != deadline; }))
                        continue;
                    purge_locked();
                    if (active)
                        due = std::chrono::steady_clock::now() + std::chrono::milliseconds(30000);
                }
            });
    }
    ~State() {
        {
            std::lock_guard lock(mutex);
            stop = true;
            changed.notify_all();
        }
        if (worker.joinable())
            worker.join();
    }
    std::int32_t now() {
        return integer(clock() - start);
    }
    void purge_locked() {
        if (entries.empty()) {
            active = false;
            return;
        }
        const auto tick = now();
        for (auto i = entries.begin(); i != entries.end();)
            if (i->second.expires > 0 && i->second.expires < tick)
                i = entries.erase(i);
            else
                ++i;
        if (entries.empty())
            active = false;
    }
    void timer() {
        if (!active) {
            active = true;
            due = std::chrono::steady_clock::now() + std::chrono::milliseconds(30000);
            changed.notify_all();
        }
    }
};
HostResolver::HostResolver(Lookup function, Clock clock, bool automatic) {
    if (!function)
        function = system_lookup;
    if (!clock)
        clock = []() {
            return std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        };
    state_ = std::make_unique<State>(std::move(function), std::move(clock), automatic);
}
HostResolver::~HostResolver() = default;
std::optional<std::vector<HostResolver::Address>> HostResolver::system_lookup(const std::string& host) {
    return lookup(host);
}
HostResolver::Address HostResolver::host_from_dns(const std::optional<std::string>& host) {
    if (blank(host))
        return {};
    const auto key = *host;
    {
        std::lock_guard lock(state_->mutex);
        auto i = state_->entries.find(key);
        if (i != state_->entries.end()) {
            i->second.expires = expiry(state_->now());
            if (i->second.address)
                return i->second.address;
        }
    }
    Address address;
    if (auto numeric = IpAddress::parse(host)) {
        if (numeric->unspecified())
            return {};
        address = std::make_shared<IpAddress>(*numeric);
    } else {
        std::optional<std::vector<Address>> found;
        try {
            found = state_->lookup(key);
        } catch (const std::exception&) {
            return {};
        }
        if (!found || found->empty())
            return {};
        for (const auto& item : *found) {
            if (!address)
                address = item;
            if (!item)
                throw std::invalid_argument("Null DNS address");
            if (!item->v6()) {
                address = item;
                break;
            }
        }
    }
    if (address) {
        std::lock_guard lock(state_->mutex);
        state_->entries[key] = {address, expiry(state_->now())};
        state_->timer();
    }
    return address;
}
HostResolver::Address HostResolver::host_from_url(const std::optional<std::string>& url) {
    if (!url)
        throw std::invalid_argument("Null URL");
    std::size_t start = 0;
    for (int part = 0; part < 3; ++part) {
        const auto end = url->find_first_of("/:", start);
        if (end == std::string::npos)
            throw std::out_of_range("URL has no host field");
        start = end + 1;
    }
    const auto end = url->find_first_of("/:", start);
    return host_from_dns(url->substr(start, end == std::string::npos ? end : end - start));
}
std::shared_ptr<IpEndpoint> HostResolver::endpoint(const std::optional<std::string>& host,
                                                   std::int32_t port) {
    return endpoint(host_from_dns(host), port);
}
std::shared_ptr<IpEndpoint> HostResolver::endpoint(const Address& address, std::int32_t port) {
    if (!address || port < 0 || port > 65535)
        return {};
    return std::make_shared<IpEndpoint>(IpEndpoint{address, static_cast<std::uint16_t>(port)});
}
void HostResolver::purge() {
    std::lock_guard lock(state_->mutex);
    if (state_->active) {
        state_->purge_locked();
        if (state_->active)
            state_->due = std::chrono::steady_clock::now() + std::chrono::milliseconds(30000);
        state_->changed.notify_all();
    }
}
void HostResolver::clear() {
    std::lock_guard lock(state_->mutex);
    state_->entries.clear();
    state_->active = false;
    state_->changed.notify_all();
}
std::size_t HostResolver::count() const {
    std::lock_guard lock(state_->mutex);
    return state_->entries.size();
}
HostResolver& host_resolver() {
    static HostResolver value;
    return value;
}
}
