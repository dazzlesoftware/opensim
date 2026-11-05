// Derived from .NET runtime v8.0.31 System.Xml XmlCharType/ValidateNames.
// Copyright (c) .NET Foundation and Contributors. Licensed under the MIT license.
// License text: native-port/third_party/dotnet_collation/LICENSE.
#include "opensim/framework/nini_xml_character.hpp"
#include "opensim/framework/nini_xml_character_data.hpp"
#include <algorithm>
namespace opensim::framework {
std::uint8_t nini_xml_character_properties(char16_t ch) noexcept {
    const auto& ranges = nini_xml_character_data;
    auto p = std::upper_bound(ranges.begin(), ranges.end(), static_cast<std::uint16_t>(ch),
                              [](std::uint16_t value, const auto& range) { return value < range.first; });
    return (--p)->properties;
}
bool nini_xml_public_id_character(char16_t ch) noexcept {
    constexpr std::u16string_view chars =
        u"\n\r !#$%'()*+,-./0123456789:;=?@ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";
    return chars.find(ch) != std::u16string_view::npos;
}
std::ptrdiff_t nini_xml_invalid_whitespace(std::u16string_view s) noexcept {
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((nini_xml_character_properties(s[i]) & 1) == 0)
            return static_cast<std::ptrdiff_t>(i);
    return -1;
}
std::ptrdiff_t nini_xml_invalid_public_id(std::u16string_view s) noexcept {
    for (std::size_t i = 0; i < s.size(); ++i)
        if (!nini_xml_public_id_character(s[i]))
            return static_cast<std::ptrdiff_t>(i);
    return -1;
}
std::ptrdiff_t nini_xml_invalid_character_data(std::u16string_view s) noexcept {
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((nini_xml_character_properties(s[i]) & 16) == 0) {
            if (i + 1 >= s.size() || s[i] < 0xd800 || s[i] > 0xdbff || s[i + 1] < 0xdc00 || s[i + 1] > 0xdfff)
                return static_cast<std::ptrdiff_t>(i);
            ++i;
        }
    return -1;
}
std::size_t nini_xml_parse_ncname(std::u16string_view s, std::size_t offset) noexcept {
    auto i = offset;
    if (i < s.size()) {
        if ((nini_xml_character_properties(s[i]) & 4) == 0)
            return 0;
        for (++i; i < s.size() && (nini_xml_character_properties(s[i]) & 8) != 0; ++i) {
        }
    }
    return i - offset;
}
std::size_t nini_xml_parse_name(std::u16string_view s, std::size_t offset) noexcept {
    auto i = offset;
    if (i < s.size()) {
        if ((nini_xml_character_properties(s[i]) & 4) == 0 && s[i] != u':')
            return 0;
        for (++i; i < s.size() && ((nini_xml_character_properties(s[i]) & 8) != 0 || s[i] == u':'); ++i) {
        }
    }
    return i - offset;
}
std::size_t nini_xml_parse_nmtoken(std::u16string_view s, std::size_t offset, bool namespaces) noexcept {
    auto i = offset;
    for (; i < s.size() && ((nini_xml_character_properties(s[i]) & 8) != 0 || (!namespaces && s[i] == u':'));
         ++i) {
    }
    return i - offset;
}
std::size_t nini_xml_parse_qname(std::u16string_view s, std::size_t offset, std::size_t& colon) noexcept {
    colon = 0;
    auto n = nini_xml_parse_ncname(s, offset);
    if (n != 0) {
        offset += n;
        if (offset < s.size() && s[offset] == u':') {
            auto second = nini_xml_parse_ncname(s, offset + 1);
            if (second != 0) {
                colon = offset;
                n += second + 1;
            }
        }
    }
    return n;
}
}
