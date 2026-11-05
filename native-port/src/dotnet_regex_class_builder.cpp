// Copyright (c) .NET Foundation and Contributors.
// .NET8.0.31 RegexCharClass builder and ArraySortHelper comparison sort; MIT third_party/dotnet_collation/LICENSE.
#include "opensim/framework/dotnet_regex_class_builder.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <span>
namespace opensim::framework {
namespace {
#include "dotnet_regex_property_data.inc"
using Range = std::pair<char16_t, char16_t>;
void swap_greater(std::span<Range> values, int i, int j) {
    if (values[i].first > values[j].first)
        std::swap(values[i], values[j]);
}
void down_heap(std::span<Range> values, int i, int n) {
    auto value = values[i - 1];
    while (i <= n / 2) {
        int child = 2 * i;
        if (child < n && values[child - 1].first < values[child].first)
            ++child;
        if (value.first >= values[child - 1].first)
            break;
        values[i - 1] = values[child - 1];
        i = child;
    }
    values[i - 1] = value;
}
void intro_sort(std::span<Range> values, int depth) {
    int n = static_cast<int>(values.size());
    while (n > 1) {
        if (n <= 16) {
            if (n == 2)
                swap_greater(values, 0, 1);
            else if (n == 3) {
                swap_greater(values, 0, 1);
                swap_greater(values, 0, 2);
                swap_greater(values, 1, 2);
            } else
                for (int i = 0; i < n - 1; ++i) {
                    auto value = values[i + 1];
                    int j = i;
                    while (j >= 0 && value.first < values[j].first) {
                        values[j + 1] = values[j];
                        --j;
                    }
                    values[j + 1] = value;
                }
            return;
        }
        if (depth == 0) {
            for (int i = n / 2; i >= 1; --i)
                down_heap(values, i, n);
            for (int i = n; i > 1; --i) {
                std::swap(values[0], values[i - 1]);
                down_heap(values, 1, i - 1);
            }
            return;
        }
        --depth;
        int high = n - 1, middle = high / 2;
        swap_greater(values, 0, middle);
        swap_greater(values, 0, high);
        swap_greater(values, middle, high);
        auto pivot = values[middle];
        std::swap(values[middle], values[high - 1]);
        int left = 0, right = high - 1;
        while (left < right) {
            while (values[++left].first < pivot.first) {
            }
            while (pivot.first < values[--right].first) {
            }
            if (left >= right)
                break;
            std::swap(values[left], values[right]);
        }
        if (left != high - 1)
            std::swap(values[left], values[high - 1]);
        intro_sort(values.subspan(left + 1, n - left - 1), depth);
        n = left;
    }
}
char16_t at(std::u16string_view text, std::size_t index) {
    if (index >= text.size())
        throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                    "Index was outside the bounds of the array.");
    return text[index];
}
std::u16string_view category_named(std::u16string_view name) {
    for (auto& item : regex_defined_categories)
        if (item.name == name)
            return item.value;
    return {};
}
}
DotnetRegexParseException::DotnetRegexParseException(std::u16string text, int position)
    : DotnetRegexClassError("System.Text.RegularExpressions.RegexParseException", 0x80070057u, utf8(text)),
      message(std::move(text)), offset(position) {
}
DotnetRegexParseException::DotnetRegexParseException(int code, std::u16string text, int position)
    : DotnetRegexParseException(std::move(text), position) {
    error = code;
}
std::vector<DotnetRegexClassBuilder::Range>& DotnetRegexClassBuilder::ensure_ranges() {
    if (!ranges_)
        ranges_.emplace();
    return *ranges_;
}
std::u16string& DotnetRegexClassBuilder::ensure_categories() {
    if (!categories_)
        categories_.emplace();
    return *categories_;
}
void DotnetRegexClassBuilder::add_range(char16_t first, char16_t last) {
    ensure_ranges().emplace_back(first, last);
}
void DotnetRegexClassBuilder::add_char(char16_t value) {
    add_range(value, value);
}
void DotnetRegexClassBuilder::add_ranges(std::u16string_view ranges) {
    auto& list = ensure_ranges();
    std::size_t i = 0;
    for (; i + 1 < ranges.size(); i += 2)
        list.emplace_back(ranges[i], static_cast<char16_t>(ranges[i + 1] - 1));
    if (i < ranges.size())
        list.emplace_back(ranges[i], char16_t(65535));
}
void DotnetRegexClassBuilder::add_class(const DotnetRegexClassBuilder& value) {
    if (value.ranges_ && !value.ranges_->empty()) {
        auto copy = *value.ranges_;
        auto& list = ensure_ranges();
        list.insert(list.end(), copy.begin(), copy.end());
    }
    if (value.categories_)
        ensure_categories().append(*value.categories_);
}
bool DotnetRegexClassBuilder::try_add_class(const DotnetRegexClassBuilder& value) {
    if (value.can_merge() && can_merge()) {
        add_class(value);
        return true;
    }
    return false;
}
void DotnetRegexClassBuilder::add_category_from_name(std::u16string_view name, bool invert, bool insensitive,
                                                     std::u16string_view pattern, int position) {
    auto category = category_named(name);
    if (!category.empty() && name != u"__InternalRegexIgnoreCase__") {
        if (insensitive && (name == u"Ll" || name == u"Lu" || name == u"Lt"))
            category = category_named(u"__InternalRegexIgnoreCase__");
        auto& output = ensure_categories();
        for (auto c : category)
            output.push_back(invert ? static_cast<char16_t>(-static_cast<int>(
                                          std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(c))))
                                    : c);
        return;
    }
    std::size_t low = 0, high = std::size(regex_property_ranges);
    while (low != high) {
        auto mid = (low + high) / 2;
        auto comparison = name.compare(regex_property_ranges[mid].name);
        if (comparison < 0)
            high = mid;
        else if (comparison > 0)
            low = mid + 1;
        else {
            auto ranges = regex_property_ranges[mid].value;
            if (invert) {
                if (ranges.front() != 0) {
                    std::u16string copy(1, 0);
                    copy += ranges;
                    add_ranges(copy);
                } else
                    add_ranges(ranges.substr(1));
            } else
                add_ranges(ranges);
            return;
        }
    }
    throw DotnetRegexParseException(u"Invalid pattern '" + std::u16string(pattern) + u"' at offset " +
                                        utf16(std::to_string(position)) + u". Unknown property '" +
                                        std::u16string(name) + u"'.",
                                    position);
}
void DotnetRegexClassBuilder::add_case_equivalences(const DotnetRegexCultureName& culture) {
    if (!ranges_)
        return;
    auto count = ranges_->size();
    for (std::size_t i = 0; i < count; ++i) {
        auto range = (*ranges_)[i];
        for (unsigned c = range.first; c <= range.second; ++c)
            for (auto equivalent :
                 dotnet_regex_case_equivalences(static_cast<char16_t>(c), culture, case_behavior_))
                add_char(equivalent);
    }
}
void DotnetRegexClassBuilder::add_word(bool ecma, bool invert) {
    if (ecma) {
        std::u16string ranges = u"0:A[_`a{\u0130\u0131";
        if (invert)
            ranges.insert(ranges.begin(), 0);
        add_ranges(ranges);
    } else {
        auto& output = ensure_categories();
        for (int c : {0, 2, 4, 5, 3, 1, 6, 9, 19, 0})
            output.push_back(static_cast<char16_t>(invert ? -c : c));
    }
}
void DotnetRegexClassBuilder::add_space(bool ecma, bool invert) {
    if (ecma) {
        std::u16string ranges = {9, 14, 32, 33};
        if (invert)
            ranges.insert(ranges.begin(), 0);
        add_ranges(ranges);
    } else
        ensure_categories().push_back(static_cast<char16_t>(invert ? -100 : 100));
}
void DotnetRegexClassBuilder::add_digit(bool ecma, bool invert, std::u16string_view pattern, int position) {
    if (ecma) {
        std::u16string ranges = u"0:";
        if (invert)
            ranges.insert(ranges.begin(), 0);
        add_ranges(ranges);
    } else
        add_category_from_name(u"Nd", invert, false, pattern, position);
}
void DotnetRegexClassBuilder::canonicalize() {
    if (!ranges_)
        return;
    auto& list = *ranges_;
    if (list.size() > 1) {
        intro_sort(list, 2 * std::bit_width(static_cast<unsigned>(list.size())));
        bool done = false;
        std::size_t n = 0, next = 1;
        for (;;) {
            auto last = list[n].second;
            for (;;) {
                if (next == list.size() || last == 65535) {
                    done = true;
                    break;
                }
                auto range = list[next];
                if (static_cast<unsigned>(range.first) > static_cast<unsigned>(last) + 1)
                    break;
                if (last < range.second)
                    last = range.second;
                ++next;
            }
            list[n].second = last;
            ++n;
            if (done)
                break;
            if (n < next)
                list[n] = list[next];
            ++next;
        }
        list.resize(n);
    }
    if (!negate_ && !subtractor_ && (!categories_ || categories_->empty())) {
        if (list.size() == 2) {
            if (list[0].first == 0 && list[1].second == 65535 &&
                static_cast<int>(list[0].second) < static_cast<int>(list[1].first) - 1) {
                list[0] = {static_cast<char16_t>(list[0].second + 1),
                           static_cast<char16_t>(list[1].first - 1)};
                list.resize(1);
                negate_ = true;
            }
        } else if (list.size() == 1) {
            if (list[0].first == 0) {
                if (list[0].second == 65534) {
                    list[0] = {char16_t(65535), char16_t(65535)};
                    negate_ = true;
                }
            } else if (list[0].first == 1 && list[0].second == 65535) {
                list[0] = {char16_t(0), char16_t(0)};
                negate_ = true;
            }
        }
    }
    if (!negate_ && !subtractor_ && categories_ && !categories_->empty() && list.size() == 1 &&
        list[0].first == 0 && list[0].second == 65535)
        categories_->clear();
    if (negate_ || subtractor_)
        return;
    if (categories_ && !categories_->empty() && list.size() == 2 && list[0].first == 0 &&
        static_cast<unsigned>(list[0].second) + 2 == list[1].first && list[1].second == 65535) {
        auto c = static_cast<char16_t>(list[0].second + 1);
        if (dotnet_regex_char_in_category(c, *categories_))
            list[0] = {char16_t(0), char16_t(65535)};
        else {
            negate_ = true;
            list[0] = {c, c};
        }
        list.resize(1);
        categories_->clear();
    }
}
void DotnetRegexClassBuilder::append_to(std::u16string& output) {
    canonicalize();
    auto start = output.size();
    output.push_back(negate_ ? 1 : 0);
    output.push_back(0);
    output.push_back(static_cast<char16_t>(categories_ ? categories_->size() : 0));
    if (ranges_)
        for (auto range : *ranges_) {
            output.push_back(range.first);
            if (range.second != 65535)
                output.push_back(static_cast<char16_t>(range.second + 1));
        }
    output[start + 1] = static_cast<char16_t>(output.size() - start - 3);
    if (categories_)
        output += *categories_;
    if (subtractor_)
        subtractor_->append_to(output);
}
std::u16string DotnetRegexClassBuilder::to_string_class() {
    std::u16string output;
    append_to(output);
    return output;
}
std::shared_ptr<DotnetRegexClassBuilder> DotnetRegexClassBuilder::parse(std::u16string_view input) {
    auto result = std::make_shared<DotnetRegexClassBuilder>();
    auto range_count = at(input, 1), category_count = at(input, 2);
    auto category_start = std::size_t(3) + range_count;
    auto end = category_start + category_count;
    if (range_count) {
        auto& list = result->ensure_ranges();
        for (std::size_t i = 3; i < std::size_t(3) + range_count; i += 2) {
            auto first = at(input, i);
            auto last = i + 1 < std::size_t(3) + range_count ? static_cast<char16_t>(at(input, i + 1) - 1)
                                                             : char16_t(65535);
            list.emplace_back(first, last);
        }
    }
    if (input.size() > end)
        result->subtractor_ = parse(input.substr(end));
    if (category_count) {
        if (category_start > input.size() || category_count > input.size() - category_start)
            throw DotnetRegexClassError(
                "System.ArgumentOutOfRangeException", 0x80131502u,
                "Specified argument was out of the range of valid values. (Parameter 'start')");
        result->categories_ = std::u16string(input.substr(category_start, category_count));
    }
    result->negate_ = at(input, 0) == 1;
    return result;
}
std::u16string dotnet_regex_convert_old_class(std::u16string_view ranges, std::u16string_view categories) {
    bool invert = ranges.size() >= 2 && ranges[0] == 0 && ranges[1] == 0;
    if (invert)
        ranges.remove_prefix(2);
    std::u16string output = {static_cast<char16_t>(invert ? 1 : 0), static_cast<char16_t>(ranges.size()),
                             static_cast<char16_t>(categories.size())};
    output += ranges;
    output += categories;
    return output;
}
}
