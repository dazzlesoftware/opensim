// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/sdk_uuid.hpp"
#include <stdexcept>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
namespace opensim::framework {
bool sse42() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int info[4]{};
    __cpuid(info, 1);
    return (info[2] & (1 << 20)) != 0;
#elif defined(__x86_64__) || defined(__i386__)
    unsigned a = 0, b = 0, c = 0, d = 0;
    return __get_cpuid(1, &a, &b, &c, &d) && (c & bit_SSE4_2) != 0;
#else
    return false;
#endif
}
// SDK UUID(string) consumes a prefix, unlike TryParse. The hyphenated SSE path
// reads UTF-16 low bytes and uses signed byte comparisons; retain both quirks.
Uuid construct_sdk_uuid(std::u16string_view text) {
    while (text.starts_with(u' '))
        text.remove_prefix(1);
    auto invalid = []() -> void { throw std::invalid_argument("Invalid UUID"); };
    if (text.size() < 32)
        invalid();
    bool hyphen = text[8] == u'-';
    if (hyphen && (text.size() < 36 || text[13] != u'-' || text[18] != u'-' || text[23] != u'-'))
        invalid();
    std::array<std::uint8_t, 16> high{}, low{}, bytes{};
    std::size_t pos = 0;
    bool simd = hyphen && sse42();
    auto nibble = [&](char16_t character) -> std::uint8_t {
        if (!simd) {
            if (character >= u'0' && character <= u'9')
                return static_cast<std::uint8_t>(character - u'0');
            if (character >= u'a' && character <= u'f')
                return static_cast<std::uint8_t>(character - u'a' + 10);
            if (character >= u'A' && character <= u'F')
                return static_cast<std::uint8_t>(character - u'A' + 10);
            invalid();
        }
        auto b = static_cast<std::uint8_t>(character);
        if (b > 57 && b < 128) {
            auto folded = static_cast<std::uint8_t>((b | 32) - 39);
            auto bounds = static_cast<std::uint8_t>(static_cast<std::uint8_t>(63 - folded) |
                                                    static_cast<std::uint8_t>(folded - 58));
            if (bounds & 128)
                invalid();
            b = folded;
        }
        b = static_cast<std::uint8_t>(b - 48);
        if (b & 128)
            invalid();
        return b;
    };
    for (std::size_t i = 0; i < 16; ++i) {
        if (hyphen && (pos == 8 || pos == 13 || pos == 18 || pos == 23))
            ++pos;
        high[i] = nibble(text[pos++]);
        low[i] = nibble(text[pos++]);
    }
    for (std::size_t i = 0; i < 16; ++i)
        bytes[i] = static_cast<std::uint8_t>(low[i] | static_cast<std::uint8_t>(high[i] << 4) |
                                             (simd && i % 2 ? high[i - 1] >> 4 : 0));
    return Uuid::from_bytes(bytes);
}
bool try_parse_sdk_uuid(std::u16string_view text, Uuid& result) {
    result = {};
    while (text.starts_with(u' '))
        text.remove_prefix(1);
    while (text.ends_with(u' '))
        text.remove_suffix(1);
    if (text.size() != 32 && text.size() != 36)
        return false;
    try {
        result = construct_sdk_uuid(text);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}
}
