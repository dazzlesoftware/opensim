// Copyright (c) .NET Foundation and Contributors.
// Port of .NET8.0.31 RegexCharClass matching methods; MIT third_party/dotnet_collation/LICENSE.
#include "opensim/framework/dotnet_regex_char_class.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <span>
namespace opensim::framework {
namespace {
#include "dotnet_regex_category_data.inc"
constexpr std::uint8_t word_ascii[] = {0, 0, 0, 0, 0, 0, 255, 3, 254, 255, 255, 135, 254, 255, 255, 7};
[[noreturn]] void index_error() {
    throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                "Index was outside the bounds of the array.");
}
char16_t at(std::u16string_view set, std::size_t index) {
    if (index >= set.size())
        index_error();
    return set[index];
}
int category_value(std::u16string_view set, std::size_t index) {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(at(set, index)));
}
bool category_group(int value, std::u16string_view categories, std::size_t &index) {
    auto n = index + 1;
    int category = category_value(categories, n);
    bool result = category > 0 ? false : true;
    if (category > 0) {
        do {
            result |= value == category - 1;
            category = category_value(categories, ++n);
        } while (category != 0);
    } else {
        do {
            result &= value != -1 - category;
            category = category_value(categories, ++n);
        } while (category != 0);
    }
    index = n;
    return result;
}
bool in_category(char16_t c, std::u16string_view categories) {
    const auto value = dotnet_regex_unicode_category(c);
    for (std::size_t i = 0; i < categories.size(); ++i) {
        int category = category_value(categories, i);
        if (category == 0) {
            if (category_group(value, categories, i))
                return true;
        } else if (category > 0) {
            if (category == 100) {
                if (unicode_whitespace(c))
                    return true;
            } else if (value == category - 1)
                return true;
        } else if (category == -100) {
            if (!unicode_whitespace(c))
                return true;
        } else if (value != -1 - category)
            return true;
    }
    return false;
}
bool in_class_recursive(char16_t c, std::u16string_view set, std::size_t start) {
    const auto range_length = at(set, start + 1), category_length = at(set, start + 2);
    const std::size_t end = start + 3 + range_length + category_length;
    auto low = start + 3, high = low + range_length;
    while (low != high) {
        auto mid = (low + high) / 2;
        if (c < at(set, mid))
            high = mid;
        else
            low = mid + 1;
    }
    bool result = (low & 1) == (start & 1);
    if (!result && category_length) {
        auto offset = start + 3 + range_length;
        if (offset > set.size() || category_length > set.size() - offset)
            throw DotnetRegexClassError(
                "System.ArgumentOutOfRangeException", 0x80131502u,
                "Specified argument was out of the range of valid values. (Parameter 'start')");
        result = in_category(c, set.substr(offset, category_length));
    }
    if (at(set, start) == 1)
        result = !result;
    if (result && set.size() > end)
        result = !in_class_recursive(c, set, end);
    return result;
}
}
std::uint8_t dotnet_regex_unicode_category(char16_t c) {
    auto end = std::upper_bound(std::begin(regex_category_starts), std::end(regex_category_starts),
                                static_cast<std::uint16_t>(c));
    return regex_category_values[(end - std::begin(regex_category_starts)) - 1];
}
bool dotnet_regex_ecma_word_char(char16_t c) {
    return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'_' ||
           c == u'\u0130';
}
bool dotnet_regex_word_char(char16_t c) {
    if (c < 128)
        return (word_ascii[c >> 3] & (1u << (c & 7))) != 0;
    return (0x4013fu & (1u << dotnet_regex_unicode_category(c))) != 0;
}
bool dotnet_regex_boundary_word_char(char16_t c) {
    return dotnet_regex_word_char(c) || c == u'\u200c' || c == u'\u200d';
}
bool dotnet_regex_char_in_class(char16_t c, std::u16string_view set) { return in_class_recursive(c, set, 0); }
bool dotnet_regex_char_in_category(char16_t c, std::u16string_view categories) {
    return in_category(c, categories);
}
bool dotnet_regex_char_in_class(char16_t c, std::u16string_view set, DotnetRegexAsciiCacheSlot &slot) {
    auto cached = slot.load();
    const auto index = static_cast<std::size_t>(static_cast<std::uint16_t>(c) >> 4);
    const std::uint32_t valid = std::uint32_t(1) << ((c & 15) << 1);
    if (cached && index < cached->size()) {
        auto bits = (*cached)[index].load();
        if (bits & valid)
            return (bits & (valid << 1)) != 0;
    }
    if (c >= 128)
        return dotnet_regex_char_in_class(c, set);
    const bool result = dotnet_regex_char_in_class(c, set);
    auto bits = valid;
    if (result)
        bits |= bits << 1;
    auto selected = slot.load();
    if (!selected) {
        auto candidate = std::make_shared<DotnetRegexAsciiCache>(8);
        for (auto &word : *candidate)
            word.store(0);
        std::shared_ptr<DotnetRegexAsciiCache> expected;
        if (slot.compare_exchange_strong(expected, candidate))
            selected = slot.load();
        else
            selected = std::move(expected);
    }
    if (!selected)
        throw DotnetRegexClassError("System.NullReferenceException", 0x80004003u,
                                    "Object reference not set to an instance of an object.");
    if (index >= selected->size())
        index_error();
    (*selected)[index].fetch_or(bits);
    return result;
}
}
