// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/sha256.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace opensim::framework {
std::string sha256_hex(std::string_view text, bool sha224) {
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
        0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
        0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
        0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
        0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
        0xc67178f2u,
    };
    std::array<std::uint32_t, 8> state{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                       0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    if (sha224)
        state = {0xc1059ed8u, 0x367cd507u, 0x3070dd17u, 0xf70e5939u,
                 0xffc00b31u, 0x68581511u, 0x64f98fa7u, 0xbefa4fa4u};
    const auto compress = [&](std::string_view block) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i)
            for (std::size_t byte = 0; byte < 4; ++byte)
                words[i] = (words[i] << 8) | static_cast<unsigned char>(block[4 * i + byte]);
        for (std::size_t i = 16; i < words.size(); ++i) {
            const auto x = words[i - 15], y = words[i - 2];
            const auto low = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
            const auto high = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10);
            words[i] = words[i - 16] + low + words[i - 7] + high;
        }
        auto [a, b, c, d, e, f, g, h] = state;
        for (std::size_t i = 0; i < words.size(); ++i) {
            const auto high = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto choice = (e & f) ^ (~e & g);
            const auto t1 = h + high + choice + constants[i] + words[i];
            const auto low = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + low + majority;
        }
        const std::array working{a, b, c, d, e, f, g, h};
        for (std::size_t i = 0; i < state.size(); ++i)
            state[i] += working[i];
    };
    const auto bits = static_cast<std::uint64_t>(text.size()) * 8;
    while (text.size() >= 64) {
        compress(text.substr(0, 64));
        text.remove_prefix(64);
    }
    std::array<char, 128> tail{};
    std::copy(text.begin(), text.end(), tail.begin());
    tail[text.size()] = static_cast<char>(0x80);
    const std::size_t size = text.size() < 56 ? 64 : 128;
    for (std::size_t i = 0; i < 8; ++i)
        tail[size - 1 - i] = static_cast<char>((bits >> (8 * i)) & 0xff);
    for (std::size_t i = 0; i < size; i += 64)
        compress(std::string_view(tail.data() + i, 64));
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (auto word : state)
        for (int shift = 28; shift >= 0; shift -= 4)
            result += digits[(word >> shift) & 15];
    if (sha224)
        result.resize(56);
    return result;
}
}
