// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/ip_address.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <net/if.h>
#endif
namespace opensim::framework {
namespace {
int nibble(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
bool ipv4(std::string_view text, std::uint32_t& value) {
    std::uint32_t parts[4]{};
    std::size_t count = 0, start = 0;
    while (true) {
        if (count == 4)
            return false;
        auto end = text.find('.', start);
        auto term = text.substr(start, end == std::string_view::npos ? end : end - start);
        if (term.empty())
            return false;
        int base = 10;
        std::size_t i = 0;
        bool digit = false;
        if (term.front() == '0') {
            base = 8;
            digit = true;
            i = 1;
            if (term.size() > 1 && (term[1] == 'x' || term[1] == 'X')) {
                base = 16;
                i = 2;
                digit = false;
            }
        }
        std::uint64_t n = 0;
        for (; i < term.size(); ++i) {
            auto d = nibble(term[i]);
            if (d < 0 || d >= base)
                return false;
            n = n * base + static_cast<unsigned>(d);
            if (n > UINT32_MAX)
                return false;
            digit = true;
        }
        if (!digit)
            return false;
        parts[count++] = static_cast<std::uint32_t>(n);
        if (end == std::string_view::npos)
            break;
        if (n > 255)
            return false;
        start = end + 1;
    }
    const auto last = parts[count - 1];
    const std::uint64_t limit = std::uint64_t{1} << (8 * (5 - count));
    if (last >= limit)
        return false;
    value = last;
    for (std::size_t i = 0; i + 1 < count; ++i)
        value |= parts[i] << (24 - i * 8);
    return true;
}
std::string dotted(std::uint32_t value) {
    return std::to_string(value >> 24) + "." + std::to_string((value >> 16) & 255) + "." +
           std::to_string((value >> 8) & 255) + "." + std::to_string(value & 255);
}
bool embedded_ipv4(std::string& value) {
    if (value.find('.') == std::string::npos)
        return true;
    const auto colon = value.rfind(':');
    if (colon == std::string::npos)
        return false;
    auto tail = std::string_view(value).substr(colon + 1);
    std::string normalized;
    std::size_t start = 0;
    for (int part = 0; part < 4; ++part) {
        auto end = tail.find('.', start);
        if ((part < 3) == (end == std::string_view::npos))
            return false;
        auto term = tail.substr(start, end == std::string_view::npos ? end : end - start);
        if (term.empty() || (term.size() > 1 && term[0] == '0' && (part < 3 || term[1] == '0')))
            return false;
        unsigned n = 0;
        for (char c : term) {
            if (c < '0' || c > '9')
                return false;
            n = n * 10 + (c - '0');
            if (n > 255)
                return false;
        }
        if (part)
            normalized += '.';
        normalized += std::to_string(n);
        start = end + 1;
    }
    value.resize(colon + 1);
    value += normalized;
    return true;
}
std::string groups(const std::array<std::uint16_t, 8>& numbers, std::size_t count) {
    std::size_t best = count, best_length = 1;
    for (std::size_t i = 0; i < count;) {
        if (numbers[i]) {
            ++i;
            continue;
        }
        const auto start = i;
        while (i < count && !numbers[i])
            ++i;
        if (i - start > best_length) {
            best = start;
            best_length = i - start;
        }
    }
    std::string result;
    for (std::size_t i = 0; i < count;) {
        if (i == best) {
            result += "::";
            i += best_length;
            continue;
        }
        if (!result.empty() && result.back() != ':')
            result += ':';
        char buffer[4];
        auto end = std::to_chars(buffer, buffer + 4, numbers[i++], 16);
        result.append(buffer, end.ptr);
    }
    return result;
}
}
IpAddress IpAddress::from_bytes(const std::array<std::uint8_t, 16>& bytes, bool v6, std::uint32_t scope) {
    IpAddress result;
    result.v6_ = v6;
    result.bytes_ = bytes;
    result.number_.store(v6 ? scope
                            : std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
                                  (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24));
    return result;
}
std::optional<IpAddress> IpAddress::parse(const std::optional<std::string>& value) {
    if (!value)
        return {};
    auto text = std::string_view(*value);
    std::array<std::uint8_t, 16> bytes{};
    if (text.find(':') == std::string_view::npos) {
        std::uint32_t address;
        if (!ipv4(text, address))
            return {};
        for (int i = 0; i < 4; ++i)
            bytes[i] = static_cast<std::uint8_t>(address >> (24 - i * 8));
        return from_bytes(bytes, false);
    }
    if (text.starts_with('[')) {
        auto end = text.find(']');
        if (end == std::string_view::npos)
            return {};
        auto suffix = text.substr(end + 1);
        if (!suffix.empty()) {
            if (suffix.front() != ':')
                return {};
            suffix.remove_prefix(1);
            bool hex = suffix.starts_with("0x");
            if (hex)
                suffix.remove_prefix(2);
            for (char c : suffix)
                if (hex ? nibble(c) < 0 : (c < '0' || c > '9'))
                    return {};
        }
        text = text.substr(1, end - 1);
    } else if (text.find(']') != std::string_view::npos)
        return {};
    if (text.find('/') != std::string_view::npos)
        return {};
    std::uint32_t scope = 0;
    auto percent = text.find('%');
    if (percent != std::string_view::npos) {
        const auto zone = text.substr(percent + 1);
        auto parsed = std::from_chars(zone.data(), zone.data() + zone.size(), scope);
        if (zone.empty() || parsed.ec != std::errc{} ||
            !std::all_of(parsed.ptr, zone.data() + zone.size(), [](char c) { return c == '\0'; })) {
            auto name = std::string(text.substr(percent));
            scope = ::if_nametoindex(name.c_str());
        }
        text = text.substr(0, percent);
    }
    std::string numeric(text);
    if (numeric.find('\0') != std::string::npos || !embedded_ipv4(numeric) ||
        inet_pton(AF_INET6, numeric.c_str(), bytes.data()) != 1)
        return {};
    return from_bytes(bytes, true, scope);
}
std::array<std::uint8_t, 16> IpAddress::bytes() const {
    if (v6_)
        return bytes_;
    auto value = number_.load();
    std::array<std::uint8_t, 16> result{};
    for (int i = 0; i < 4; ++i)
        result[i] = static_cast<std::uint8_t>(value >> (i * 8));
    return result;
}
std::uint32_t IpAddress::scope_id() const {
    if (!v6_)
        throw std::invalid_argument("IPv4 address has no scope");
    return number_.load();
}
void IpAddress::scope_id(std::int64_t value) {
    if (!v6_)
        throw std::invalid_argument("IPv4 address has no scope");
    if (value < 0 || value > UINT32_MAX)
        throw std::out_of_range("Invalid IPv6 scope");
    number_.store(static_cast<std::uint32_t>(value));
}
std::uint32_t IpAddress::ipv4_address() const {
    if (v6_)
        throw std::invalid_argument("IPv6 address has no IPv4 value");
    return number_.load();
}
void IpAddress::ipv4_address(std::int64_t value) {
    if (v6_)
        throw std::invalid_argument("IPv6 address has no IPv4 value");
    number_.store(static_cast<std::uint32_t>(value));
}
bool IpAddress::unspecified() const {
    if (!v6_)
        return number_.load() == 0;
    return number_.load() == 0 && std::all_of(bytes_.begin(), bytes_.end(), [](auto c) { return c == 0; });
}
std::string IpAddress::str() const {
    if (!v6_) {
        const auto b = bytes();
        return dotted((std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) |
                      b[3]);
    }
    std::array<std::uint16_t, 8> n{};
    for (int i = 0; i < 8; ++i)
        n[i] = static_cast<std::uint16_t>((bytes_[i * 2] << 8) | bytes_[i * 2 + 1]);
    bool embedded = (!n[0] && !n[1] && !n[2] && !n[3] && n[6] &&
                     ((!n[4] && (!n[5] || n[5] == 65535)) || (n[4] == 65535 && !n[5]))) ||
                    (!n[4] && n[5] == 0x5efe);
    auto result = groups(n, embedded ? 6 : 8);
    if (embedded) {
        if (result.empty() || result.back() != ':')
            result += ':';
        result += dotted((std::uint32_t(n[6]) << 16) | n[7]);
    }
    const auto scope = number_.load();
    if (scope)
        result += '%' + std::to_string(scope);
    return result;
}
std::string IpEndpoint::str() const {
    if (!address)
        throw std::invalid_argument("Null endpoint address");
    return (address->v6() ? "[" + address->str() + "]" : address->str()) + ":" + std::to_string(port);
}
}
