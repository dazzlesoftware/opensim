// Copyright (c) .NET Foundation and Contributors.
// Port of .NET8.0.31 RegexCharClass analysis methods; MIT third_party/dotnet_collation/LICENSE.
#include "opensim/framework/dotnet_regex_class_analysis.hpp"
#include <map>
#include <mutex>
#include <algorithm>
#include <array>
#include <bit>
namespace opensim::framework {
std::shared_ptr<const std::u16string> dotnet_regex_class_literal(std::u16string_view value) {
    static std::mutex mutex;
    static std::map<std::u16string, std::shared_ptr<const std::u16string>> literals;
    std::lock_guard lock(mutex);
    auto [entry, inserted] = literals.try_emplace(std::u16string(value));
    if (inserted)
        entry->second = std::make_shared<const std::u16string>(value);
    return entry->second;
}

namespace {
using namespace std::literals;
std::u16string_view required(DotnetRegexClassText text) {
    if (!text)
        throw DotnetRegexClassError("System.NullReferenceException", 0x80004003u,
                                    "Object reference not set to an instance of an object.");
    return *text;
}
char16_t at(std::u16string_view text, std::size_t index) {
    if (index >= text.size())
        throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                    "Index was outside the bounds of the array.");
    return text[index];
}
int signed_at(std::u16string_view text, std::size_t index) {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(at(text, index)));
}
bool easy(std::u16string_view set, bool& subtraction) {
    subtraction = dotnet_regex_class_subtraction(set);
    return set.size() > 3 && at(set, 1) > 0 && at(set, 1) % 2 == 0 && at(set, 2) == 0;
}
bool easy(std::u16string_view set) {
    bool subtraction = false;
    return easy(set, subtraction) && !subtraction;
}
bool distinct(std::u16string_view first, std::u16string_view second) {
    if (first == u"\0\0\u0001d"sv || first == u"\0\u0004\0\t\u000e !"sv)
        return second == u"\0\0\u0001\t"sv ||
               second == u"\0\0\n\0\u0002\u0004\u0005\u0003\u0001\u0006\t\u0013\0"sv ||
               second == u"\0\u0002\0"
                         u"0:"sv ||
               second == u"\0\n\0"
                         u"0:A[_`a{\u0130\u0131"sv;
    return false;
}
bool overlap_enumerated(std::u16string_view target, std::u16string_view enumerated) {
    for (int i = 3; i < 3 + at(enumerated, 1); i += 2) {
        int end = at(enumerated, i + 1);
        for (int c = at(enumerated, i); c < end; ++c)
            if (dotnet_regex_char_in_class(static_cast<char16_t>(c), target))
                return true;
    }
    return false;
}
}
char16_t dotnet_regex_singleton_char(DotnetRegexClassText text) {
    return at(required(text), 3);
}
bool dotnet_regex_class_negated(DotnetRegexClassText text, int offset) {
    return at(required(text), static_cast<std::size_t>(offset)) == 1;
}
bool dotnet_regex_class_subtraction(DotnetRegexClassText text) {
    auto set = required(text);
    return set.size() > std::size_t(3) + at(set, 2) + at(set, 1);
}
bool dotnet_regex_class_mergeable(DotnetRegexClassText text) {
    return text && !dotnet_regex_class_negated(text) && !dotnet_regex_class_subtraction(text);
}
bool dotnet_regex_class_empty(DotnetRegexClassText text) {
    auto set = required(text);
    return at(set, 2) == 0 && at(set, 1) == 0 && !dotnet_regex_class_negated(set) &&
           !dotnet_regex_class_subtraction(set);
}
bool dotnet_regex_class_singleton(DotnetRegexClassText text) {
    auto set = required(text);
    return at(set, 2) == 0 && at(set, 1) == 2 && !dotnet_regex_class_negated(set) &&
           !dotnet_regex_class_subtraction(set) &&
           (at(set, 3) == 65535 || static_cast<unsigned>(at(set, 3)) + 1 == at(set, 4));
}
bool dotnet_regex_class_singleton_inverse(DotnetRegexClassText text) {
    auto set = required(text);
    return at(set, 2) == 0 && at(set, 1) == 2 && dotnet_regex_class_negated(set) &&
           !dotnet_regex_class_subtraction(set) &&
           (at(set, 3) == 65535 || static_cast<unsigned>(at(set, 3)) + 1 == at(set, 4));
}
bool dotnet_regex_only_categories(DotnetRegexClassText text, std::span<std::int32_t> output, int& count,
                                  bool& negated) {
    negated = false;
    count = 0;
    bool seen = false;
    auto set = required(text);
    if (at(set, 2) == 0 || at(set, 1) != 0 || dotnet_regex_class_subtraction(set))
        return false;
    int end = 3 + at(set, 2);
    for (int i = 3; i < end; ++i) {
        int c = signed_at(set, i);
        if (c > 0) {
            if ((seen && negated) || c == 100 || static_cast<std::size_t>(count) == output.size())
                return false;
            seen = true;
            output[count++] = c - 1;
            continue;
        }
        if (c < 0) {
            if ((seen && !negated) || c == -100 || static_cast<std::size_t>(count) == output.size())
                return false;
            seen = true;
            negated = true;
            output[count++] = -1 - c;
            continue;
        }
        c = signed_at(set, ++i);
        if (c > 0) {
            if (seen && negated)
                return false;
            seen = true;
            do {
                if (static_cast<std::size_t>(count) == output.size())
                    return false;
                output[count++] = c - 1;
                c = signed_at(set, ++i);
            } while (c != 0);
            continue;
        }
        if (seen && !negated)
            return false;
        negated = true;
        seen = true;
        do {
            if (static_cast<std::size_t>(count) == output.size())
                return false;
            output[count++] = -1 - c;
            c = signed_at(set, ++i);
        } while (c != 0);
    }
    negated ^= dotnet_regex_class_negated(set);
    return true;
}
bool dotnet_regex_single_range(DotnetRegexClassText text, char16_t& low, char16_t& high) {
    auto set = required(text);
    if (at(set, 2) == 0 && set.size() == std::size_t(3) + at(set, 1)) {
        switch (at(set, 1)) {
        case 1:
            low = at(set, 3);
            high = 65535;
            return true;
        case 2:
            low = at(set, 3);
            high = static_cast<char16_t>(at(set, 4) - 1);
            return true;
        }
    }
    low = high = 0;
    return false;
}
bool dotnet_regex_double_range(DotnetRegexClassText text, DotnetRegexRange& first, DotnetRegexRange& second) {
    auto set = required(text);
    if (at(set, 2) == 0 && set.size() == std::size_t(3) + at(set, 1)) {
        auto count = at(set, 1);
        if (count == 3 || count == 4) {
            first = {at(set, 3), static_cast<char16_t>(at(set, 4) - 1)};
            second = {at(set, 5), count == 3 ? char16_t(65535) : static_cast<char16_t>(at(set, 6) - 1)};
            return true;
        }
    }
    first = second = {};
    return false;
}
int dotnet_regex_set_chars(DotnetRegexClassText text, std::span<char16_t> chars) {
    auto set = required(text);
    if (!easy(set))
        return 0;
    int count = 0;
    for (int i = 3; i < 3 + at(set, 1); i += 2) {
        int end = at(set, i + 1);
        for (int c = at(set, i); c < end; ++c) {
            if (static_cast<std::size_t>(count) >= chars.size())
                return 0;
            chars[count++] = static_cast<char16_t>(c);
        }
    }
    return count;
}
std::optional<std::u16string> dotnet_regex_ascii_set_chars(DotnetRegexClassText text) {
    std::array<char16_t, 128> buffer{};
    auto count = dotnet_regex_set_chars(text, buffer);
    if (!count || !dotnet_regex_is_ascii(std::span<const char16_t>(buffer).first(count)))
        return {};
    return std::u16string(buffer.data(), count);
}
bool dotnet_regex_may_overlap(DotnetRegexClassText first, DotnetRegexClassText second) {
    if (first == second)
        return true;
    constexpr auto any = u"\0\u0001\0\0"sv;
    if ((first && *first == any) || (second && *second == any))
        return true;
    bool first_negated = dotnet_regex_class_negated(first),
         second_negated = dotnet_regex_class_negated(second);
    auto a = required(first), b = required(second);
    if (first_negated != second_negated)
        return a.substr(1) != b.substr(1);
    if (first_negated)
        return true;
    if (distinct(a, b) || distinct(b, a))
        return false;
    if (easy(b))
        return overlap_enumerated(a, b);
    if (easy(a))
        return overlap_enumerated(b, a);
    return true;
}
bool dotnet_regex_participates_in_case_conversion(std::int32_t value) {
    switch (dotnet_regex_unicode_category(static_cast<char16_t>(value))) {
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
        return false;
    default:
        return true;
    }
}
bool dotnet_regex_participates_in_case_conversion(std::span<const char16_t> text) {
    return std::any_of(text.begin(), text.end(), [](char16_t c) {
        return dotnet_regex_participates_in_case_conversion(static_cast<std::int32_t>(c));
    });
}
bool dotnet_regex_is_ascii(std::span<const char16_t> text) {
    return std::all_of(text.begin(), text.end(), [](char16_t c) { return c < 128; });
}
bool dotnet_regex_differ_by_one_bit(char16_t first, char16_t second, int& mask) {
    mask = first ^ second;
    return std::has_single_bit(static_cast<unsigned>(mask));
}
DotnetRegexClassAnalysis dotnet_regex_analyze_class(DotnetRegexClassText text) {
    bool negated = dotnet_regex_class_negated(text), subtraction = false;
    auto set = required(text);
    if (!easy(set, subtraction) || (negated && subtraction))
        return {};
    auto first = at(set, 3), last = at(set, 3 + at(set, 1) - 1);
    DotnetRegexClassAnalysis result;
    result.only_ranges = true;
    result.lower_bound = first;
    result.upper_bound = last;
    if (negated) {
        result.all_non_ascii_contained = last <= 128;
        result.all_ascii_contained = first >= 128;
        result.contains_no_ascii = first == 0 && at(set, 4) >= 128;
    } else {
        result.all_ascii_contained = first == 0 && at(set, 4) >= 128 && !subtraction;
        result.contains_only_ascii = last <= 128;
        result.contains_no_ascii = first >= 128;
    }
    return result;
}
std::optional<std::vector<DotnetRegexRange>> dotnet_regex_compute_ranges(std::span<const char16_t> input) {
    std::u16string_view set(input.data(), input.size());
    auto count = at(set, 1);
    if (!count)
        return {};
    std::vector<DotnetRegexRange> ranges;
    for (int i = 3; i < 3 + count; i += 2)
        ranges.push_back(
            {at(set, i), i + 1 < 3 + count ? static_cast<char16_t>(at(set, i + 1) - 1) : char16_t(65535)});
    return ranges;
}
std::shared_ptr<const std::u16string> dotnet_regex_chars_to_string_class(std::span<const char16_t> chars) {
    static const auto empty = std::make_shared<const std::u16string>(std::u16string{0, 0, 0});
    static std::array<std::atomic<std::shared_ptr<const std::u16string>>, 128> ascii;
    static std::array<std::atomic<std::shared_ptr<const std::u16string>>, 26> letters;
    if (chars.empty())
        return empty;
    std::atomic<std::shared_ptr<const std::u16string>>* cache = nullptr;
    std::u16string text;
    if (chars.size() == 1 && chars[0] < 128) {
        cache = &ascii[chars[0]];
        text = {0, 2, 0, chars[0], static_cast<char16_t>(chars[0] + 1)};
    } else if (chars.size() == 2) {
        unsigned lower = chars[0] | 32u;
        if (lower >= 97 && lower <= 122 && lower == (chars[1] | 32u)) {
            cache = &letters[lower - 97];
            auto upper = static_cast<char16_t>(lower & ~32u);
            text = {0,
                    4,
                    0,
                    upper,
                    static_cast<char16_t>(upper + 1),
                    static_cast<char16_t>(lower),
                    static_cast<char16_t>(lower + 1)};
        }
    }
    if (cache) {
        if (auto value = cache->load())
            return value;
        auto value = std::make_shared<const std::u16string>(std::move(text));
        cache->store(value);
        return value;
    }
    auto length = chars.size() * 2 - (chars.back() == 65535 ? 1 : 0);
    text.resize(3 + length);
    text[0] = 0;
    text[1] = static_cast<char16_t>(length);
    text[2] = 0;
    std::size_t index = 3;
    for (auto c : chars) {
        text[index++] = c;
        if (c != 65535)
            text[index++] = static_cast<char16_t>(c + 1);
    }
    return std::make_shared<const std::u16string>(std::move(text));
}
std::shared_ptr<const std::u16string> dotnet_regex_one_to_string_class(char16_t c) {
    return dotnet_regex_chars_to_string_class(std::span<const char16_t>(&c, 1));
}
}
