// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
namespace opensim::framework {
class IpAddress {
  public:
    IpAddress() = default;
    IpAddress(const IpAddress& value) : v6_(value.v6_), bytes_(value.bytes_), number_(value.number_.load()) {}
    IpAddress& operator=(const IpAddress& value) {
        v6_ = value.v6_;
        bytes_ = value.bytes_;
        number_.store(value.number_.load());
        return *this;
    }
    static IpAddress from_bytes(const std::array<std::uint8_t, 16>& bytes, bool v6, std::uint32_t scope = 0);
    static std::optional<IpAddress> parse(const std::optional<std::string>& value);
    bool v6() const {
        return v6_;
    }
    std::array<std::uint8_t, 16> bytes() const;
    std::uint32_t scope_id() const;
    void scope_id(std::int64_t value);
    std::uint32_t ipv4_address() const;
    void ipv4_address(std::int64_t value);
    bool unspecified() const;
    std::string str() const;

  private:
    bool v6_ = false;
    std::array<std::uint8_t, 16> bytes_{};
    std::atomic<std::uint32_t> number_{0};
};
struct IpEndpoint {
    std::shared_ptr<IpAddress> address;
    std::uint16_t port = 0;
    std::string str() const;
};
}
