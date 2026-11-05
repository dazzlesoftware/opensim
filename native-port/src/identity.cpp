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
#include "opensim/framework/identity.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <span>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#else
#include <sys/random.h>
#include <cerrno>
#endif

namespace opensim::framework {
namespace {
constexpr char hex[] = "0123456789abcdef";
void secure_random(std::span<std::uint8_t> bytes) {
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("Operating system random generator failed");
#elif defined(__APPLE__)
    arc4random_buf(bytes.data(), bytes.size());
#else
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            throw std::runtime_error("Operating system random generator failed");
        offset += static_cast<std::size_t>(count);
    }
#endif
}
}
std::string normalize_uuid(std::string_view text) {
    if (text.size() == 38 && text.front() == '{' && text.back() == '}')
        text = text.substr(1, 36);
    std::string digits;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (text.size() == 36 && (i == 8 || i == 13 || i == 18 || i == 23)) {
            if (c != '-')
                throw std::invalid_argument("Invalid UUID");
            continue;
        }
        if (c >= 'A' && c <= 'F')
            c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            throw std::invalid_argument("Invalid UUID");
        digits += c;
    }
    if (digits.size() != 32 || (text.size() != 32 && text.size() != 36))
        throw std::invalid_argument("Invalid UUID");
    return digits.substr(0, 8) + '-' + digits.substr(8, 4) + '-' + digits.substr(12, 4) + '-' +
           digits.substr(16, 4) + '-' + digits.substr(20);
}
std::string random_uuid() {
    std::array<std::uint8_t, 16> bytes{};
    secure_random(bytes);
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 15) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 63) | 0x80);
    std::string digits;
    for (auto byte : bytes) {
        digits += hex[byte >> 4];
        digits += hex[byte & 15];
    }
    return normalize_uuid(digits);
}
bool constant_time_equal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    volatile std::uint8_t difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        difference = static_cast<std::uint8_t>(difference | static_cast<std::uint8_t>(a[i] ^ b[i]));
    return difference == 0;
}
std::string md5(std::string_view text) {
    constexpr std::uint32_t k[] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    constexpr int shifts[] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8;
    bytes.push_back(0x80);
    while (bytes.size() % 64 != 56)
        bytes.push_back(0);
    for (int i = 0; i < 8; ++i)
        bytes.push_back(static_cast<std::uint8_t>(bit_length >> (i * 8)));
    std::uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
        std::array<std::uint32_t, 16> words{};
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 4; ++j)
                words[i] |= static_cast<std::uint32_t>(bytes[offset + i * 4 + j]) << (j * 8);
        auto a = a0, b = b0, c = c0, d = d0;
        for (int i = 0; i < 64; ++i) {
            std::uint32_t f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            const auto next = b + std::rotl(a + f + k[i] + words[g], shifts[(i / 16) * 4 + i % 4]);
            a = d;
            d = c;
            c = b;
            b = next;
        }
        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }
    std::string output;
    for (auto word : {a0, b0, c0, d0})
        for (int i = 0; i < 4; ++i) {
            auto byte = static_cast<std::uint8_t>(word >> (8 * i));
            output += hex[byte >> 4];
            output += hex[byte & 15];
        }
    return output;
}
}
