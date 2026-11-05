// Copyright (c) .NET Foundation and Contributors.
// .NET8.0.31 RegexFindOptimizations; MIT third_party/dotnet_collation/LICENSE.
#include "opensim/framework/dotnet_regex_find.hpp"
#include "opensim/framework/text.hpp"
#include "dotnet_regex_sort.hpp"
#include <array>
#include <bit>
namespace opensim::framework {
namespace {
using K = DotnetRegexNodeKind;
using M = DotnetRegexFindMode;
using Sets = std::vector<DotnetRegexFixedDistanceSet>;
[[noreturn]] void slice_error() {
    throw DotnetRegexClassError("System.ArgumentOutOfRangeException", 0x80131502u,
                                "Specified argument was out of the range of valid values.");
}
[[noreturn]] void index_error() {
    throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                "Index was outside the bounds of the array.");
}
int wrap(std::int64_t value) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}
std::u16string_view slice(std::u16string_view value, int start) {
    if (start < 0 || static_cast<std::size_t>(start) > value.size())
        slice_error();
    return value.substr(start);
}
std::u16string_view prefix(std::u16string_view value, int count) {
    if (count < 0 || static_cast<std::size_t>(count) > value.size())
        slice_error();
    return value.substr(0, count);
}
char16_t at(std::u16string_view value, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= value.size())
        index_error();
    return value[index];
}
std::u16string_view str(const DotnetRegexNodeText& value) {
    if (!value)
        throw DotnetRegexClassError("System.NullReferenceException", 0x80004003u,
                                    "Object reference not set to an instance of an object.");
    return *value;
}
int index(std::u16string_view value, std::u16string_view target) {
    auto found = value.find(target);
    return found == std::u16string_view::npos ? -1 : static_cast<int>(found);
}
int index(std::u16string_view value, char16_t target) {
    auto found = value.find(target);
    return found == std::u16string_view::npos ? -1 : static_cast<int>(found);
}
int any(std::u16string_view value, const std::vector<char16_t>& chars, bool negate = false) {
    for (std::size_t i = 0; i < value.size(); ++i) {
        bool contained = std::find(chars.begin(), chars.end(), value[i]) != chars.end();
        if (contained != negate)
            return static_cast<int>(i);
    }
    return -1;
}
int ordinal_ignore_case_index(std::u16string_view value, std::u16string_view ascii) {
    // The source prefix extractor accepts ASCII characters and ASCII case pairs
    // only. Non-ASCII input cannot equal an ASCII character ordinal-ignore-case.
    if (ascii.size() > value.size())
        return -1;
    for (std::size_t i = 0; i <= value.size() - ascii.size(); ++i) {
        std::size_t j = 0;
        for (; j < ascii.size(); ++j) {
            auto a = value[i + j], b = ascii[j];
            if (a == b)
                continue;
            if (a >= 128 || b >= 128)
                break;
            auto upper = [](char16_t c) {
                return c >= u'a' && c <= u'z' ? static_cast<char16_t>(c - 32) : c;
            };
            if (upper(a) != upper(b))
                break;
        }
        if (j == ascii.size())
            return static_cast<int>(i);
    }
    return -1;
}
std::optional<std::pair<DotnetRegexNodeText, int>> fixed_string(Sets& sets) {
    std::optional<std::pair<DotnetRegexNodeText, int>> result;
    if (sets.size() < 2)
        return result;
    regex_detail::sort(sets, [](auto& a, auto& b) {
        return a.distance < b.distance ? -1 : a.distance > b.distance ? 1 : 0;
    });
    std::u16string pending;
    int start = -1;
    for (int i = 0; i < static_cast<int>(sets.size()) + 1; ++i) {
        auto chars = i < static_cast<int>(sets.size()) ? sets[i].chars : nullptr;
        bool stop = !chars || chars->size() != 1 || sets[i].negated;
        if (stop ||
            (i > 0 && sets[i].distance != wrap(static_cast<std::int64_t>(sets[i - 1].distance) + 1))) {
            if (start != -1 && i - start >= (result ? static_cast<int>(result->first->size()) : 2))
                result = std::pair{std::make_shared<const std::u16string>(std::move(pending)),
                                   sets[start].distance};
            pending.clear();
            start = -1;
            if (stop)
                continue;
        }
        if (start == -1)
            start = i;
        pending.push_back((*chars)[0]);
    }
    return result;
}
}
void DotnetRegexFindOptimizations::caches(std::size_t count) {
    ascii_ = std::make_unique<DotnetRegexAsciiCacheSlot[]>(count);
    ascii_count_ = count;
}
const DotnetRegexAsciiCacheSlot& DotnetRegexFindOptimizations::ascii_lookup(std::size_t index) const {
    if (index >= ascii_count_)
        index_error();
    return ascii_[index];
}
DotnetRegexFindOptimizations::DotnetRegexFindOptimizations(const DotnetRegexParsedPattern& tree,
                                                           const DotnetRegexExecutionStack& stack)
    : owner_(tree.arena) {
    auto root = tree.root;
    if (!root)
        throw DotnetRegexClassError("System.NullReferenceException", 0x80004003u,
                                    "Object reference not set to an instance of an object.");
    auto& d = data_;
    bool rtl = (tree.options & 64) != 0;
    d.leading_prefix = runtime_empty_text();
    d.minimum_length = root->compute_min_length(stack);
    d.leading_anchor = dotnet_regex_find_leading_anchor(root, stack);
    if (rtl && d.leading_anchor == K::Bol)
        d.leading_anchor = K::Unknown;
    if (d.leading_anchor >= K::Beginning && d.leading_anchor <= K::End) {
        int mode = d.leading_anchor == K::Beginning ? 0
                   : d.leading_anchor == K::Start   ? 1
                   : d.leading_anchor == K::End     ? 3
                                                    : 2;
        d.mode = static_cast<M>(mode + (rtl ? 4 : 0));
        return;
    }
    if (!rtl) {
        d.trailing_anchor = dotnet_regex_find_trailing_anchor(root, stack);
        if (d.trailing_anchor == K::EndZ || d.trailing_anchor == K::End) {
            auto maximum = root->compute_max_length(stack);
            if (maximum) {
                d.maximum_length = maximum;
                if (d.minimum_length == *maximum) {
                    d.mode = d.trailing_anchor == K::End ? M::TrailingAnchor_FixedLength_LeftToRight_End
                                                         : M::TrailingAnchor_FixedLength_LeftToRight_EndZ;
                    return;
                }
            }
        }
    }
    auto literal = dotnet_regex_find_prefix(root, stack);
    if (literal->size() > 1) {
        d.leading_prefix = literal;
        d.mode = rtl ? M::LeadingString_RightToLeft : M::LeadingString_LeftToRight;
        return;
    }
    bool non_backtracking = (tree.options & 1024) != 0,
         compiled = (tree.options & 8) != 0 && !non_backtracking,
         interpreted = !compiled && !non_backtracking;
    if (rtl) {
        auto first = dotnet_regex_find_first_char_class(root, stack);
        if (first) {
            std::array<char16_t, 5> chars;
            std::shared_ptr<const std::vector<char16_t>> values;
            int count = 0;
            if (!dotnet_regex_class_negated(std::u16string_view(*first)) &&
                (count = dotnet_regex_set_chars(std::u16string_view(*first), chars)) > 0)
                values = std::make_shared<const std::vector<char16_t>>(chars.begin(), chars.begin() + count);
            if (!compiled && values && values->size() == 1) {
                d.fixed_literal.character = (*values)[0];
                d.mode = M::LeadingChar_RightToLeft;
                return;
            }
            d.fixed_sets = Sets{{values, first, 0}};
            d.mode = M::LeadingSet_RightToLeft;
            caches(1);
        }
        return;
    }
    literal = dotnet_regex_find_ordinal_case_insensitive_prefix(root);
    if (literal && literal->size() > 1) {
        d.leading_prefix = literal;
        d.mode = M::LeadingString_OrdinalIgnoreCase_LeftToRight;
        return;
    }
    auto sets = dotnet_regex_find_fixed_distance_sets(root, !interpreted, stack);
    if (sets) {
        auto string = fixed_string(*sets);
        if (string && string->first) {
            d.mode = M::FixedDistanceString_LeftToRight;
            d.fixed_literal.string = string->first;
            d.fixed_literal.distance = string->second;
            return;
        }
    }
    auto after = dotnet_regex_find_literal_after_loop(root);
    if (sets) {
        dotnet_regex_sort_fixed_distance_sets(*sets);
        if (!after || ((*sets)[0].chars && !(*sets)[0].negated)) {
            if (!compiled && sets->size() == 1) {
                auto chars = (*sets)[0].chars;
                if (chars && chars->size() == 1 && !(*sets)[0].negated) {
                    d.fixed_literal.character = (*chars)[0];
                    d.fixed_literal.distance = (*sets)[0].distance;
                    d.mode = M::FixedDistanceChar_LeftToRight;
                    return;
                }
            }
            if (sets->size() > 3)
                sets->erase(sets->begin() + 3, sets->end());
            d.fixed_sets = std::move(sets);
            d.mode = d.fixed_sets->size() == 1 && (*d.fixed_sets)[0].distance == 0
                         ? M::LeadingSet_LeftToRight
                         : M::FixedDistanceSets_LeftToRight;
            caches(d.fixed_sets->size());
            return;
        }
    }
    if (after) {
        d.mode = M::LiteralAfterLoop_LeftToRight;
        d.literal_after_loop = after;
        caches(1);
    }
}
bool DotnetRegexFindOptimizations::try_find_right_to_left(std::u16string_view value, int& pos, int start) {
    auto& d = data_;
    int length = static_cast<int>(value.size());
    if (pos < d.minimum_length) {
        pos = 0;
        return false;
    }
    switch (d.mode) {
    case M::LeadingAnchor_RightToLeft_Beginning:
        if (pos != 0)
            pos = 0;
        return true;
    case M::LeadingAnchor_RightToLeft_Start:
        if (pos != start) {
            pos = 0;
            return false;
        }
        return true;
    case M::LeadingAnchor_RightToLeft_EndZ:
        if (pos < length - 1 || (static_cast<std::uint32_t>(pos) < value.size() && at(value, pos) != u'\n')) {
            pos = 0;
            return false;
        }
        return true;
    case M::LeadingAnchor_RightToLeft_End:
        if (pos < length) {
            pos = 0;
            return false;
        }
        return true;
    case M::LeadingString_RightToLeft: {
        auto found = prefix(value, pos).rfind(str(d.leading_prefix));
        if (found != std::u16string_view::npos) {
            pos = static_cast<int>(found + d.leading_prefix->size());
            return true;
        }
        pos = 0;
        return false;
    }
    case M::LeadingChar_RightToLeft: {
        auto found = prefix(value, pos).rfind(d.fixed_literal.character);
        if (found != std::u16string_view::npos) {
            pos = static_cast<int>(found) + 1;
            return true;
        }
        pos = 0;
        return false;
    }
    case M::LeadingSet_RightToLeft: {
        auto text = prefix(value, pos);
        auto set = str((*d.fixed_sets)[0].set);
        for (int i = static_cast<int>(text.size()) - 1; i >= 0; --i)
            if (dotnet_regex_char_in_class(text[i], set, ascii_[0])) {
                pos = i + 1;
                return true;
            }
        pos = 0;
        return false;
    }
    default:
        return true;
    }
}
bool DotnetRegexFindOptimizations::try_find_left_to_right(std::u16string_view value, int& pos, int start) {
    auto& d = data_;
    int length = static_cast<int>(value.size());
    if (pos > wrap(static_cast<std::int64_t>(length) - d.minimum_length)) {
        pos = length;
        return false;
    }
    if (d.leading_anchor == K::Bol) {
        int previous = wrap(static_cast<std::int64_t>(pos) - 1);
        if (static_cast<std::uint32_t>(previous) < value.size() && at(value, previous) != u'\n') {
            int next = index(slice(value, pos), u'\n');
            if (static_cast<std::int64_t>(static_cast<std::uint32_t>(next)) >
                wrap(static_cast<std::int64_t>(length) - 1 - pos)) {
                pos = length;
                return false;
            }
            pos = wrap(static_cast<std::int64_t>(next) + 1 + pos);
            if (pos > wrap(static_cast<std::int64_t>(length) - d.minimum_length)) {
                pos = length;
                return false;
            }
        }
    }
    switch (d.mode) {
    case M::LeadingAnchor_LeftToRight_Beginning:
        if (pos == 0)
            return true;
        pos = length;
        return false;
    case M::LeadingAnchor_LeftToRight_Start:
        if (pos == start)
            return true;
        pos = length;
        return false;
    case M::LeadingAnchor_LeftToRight_EndZ:
        if (pos < length - 1)
            pos = length - 1;
        return true;
    case M::LeadingAnchor_LeftToRight_End:
        if (pos < length)
            pos = length;
        return true;
    case M::TrailingAnchor_FixedLength_LeftToRight_EndZ:
        if (pos < wrap(static_cast<std::int64_t>(length) - d.minimum_length - 1))
            pos = wrap(static_cast<std::int64_t>(length) - d.minimum_length - 1);
        return true;
    case M::TrailingAnchor_FixedLength_LeftToRight_End:
        if (pos < wrap(static_cast<std::int64_t>(length) - d.minimum_length))
            pos = wrap(static_cast<std::int64_t>(length) - d.minimum_length);
        return true;
    case M::LeadingString_LeftToRight:
    case M::LeadingString_OrdinalIgnoreCase_LeftToRight: {
        auto tail = slice(value, pos);
        int found = d.mode == M::LeadingString_LeftToRight
                        ? index(tail, str(d.leading_prefix))
                        : ordinal_ignore_case_index(tail, str(d.leading_prefix));
        if (found >= 0) {
            pos = wrap(static_cast<std::int64_t>(pos) + found);
            return true;
        }
        pos = length;
        return false;
    }
    case M::LeadingSet_LeftToRight: {
        auto& set = (*d.fixed_sets)[0];
        auto tail = slice(value, pos);
        if (set.chars && set.chars->size() <= 5) {
            int found = any(tail, *set.chars, set.negated);
            if (found >= 0) {
                pos = wrap(static_cast<std::int64_t>(pos) + found);
                return true;
            }
        } else
            for (std::size_t i = 0; i < tail.size(); ++i)
                if (dotnet_regex_char_in_class(tail[i], str(set.set), ascii_[0])) {
                    pos = wrap(static_cast<std::int64_t>(pos) + i);
                    return true;
                }
        pos = length;
        return false;
    }
    case M::FixedDistanceChar_LeftToRight:
    case M::FixedDistanceString_LeftToRight: {
        auto tail = slice(value, wrap(static_cast<std::int64_t>(pos) + d.fixed_literal.distance));
        int found = d.mode == M::FixedDistanceChar_LeftToRight ? index(tail, d.fixed_literal.character)
                                                               : index(tail, str(d.fixed_literal.string));
        if (found >= 0) {
            pos = wrap(static_cast<std::int64_t>(pos) + found);
            return true;
        }
        pos = length;
        return false;
    }
    case M::FixedDistanceSets_LeftToRight: {
        auto& sets = *d.fixed_sets;
        auto& first = sets[0];
        int limit = wrap(static_cast<std::int64_t>(length) - std::max(1, d.minimum_length));
        bool small = first.chars && first.chars->size() <= 5;
        for (int candidate = pos; candidate <= limit; ++candidate) {
            if (small) {
                int offset = wrap(static_cast<std::int64_t>(candidate) + first.distance);
                int found = any(slice(value, offset), *first.chars, first.negated);
                if (found < 0)
                    break;
                found = wrap(static_cast<std::int64_t>(found) + offset);
                candidate = wrap(static_cast<std::int64_t>(found) - first.distance);
                if (candidate > limit)
                    break;
            } else if (!dotnet_regex_char_in_class(
                           at(value, wrap(static_cast<std::int64_t>(candidate) + first.distance)),
                           str(first.set), ascii_[0]))
                continue;
            std::size_t i = 1;
            for (; i < sets.size(); ++i)
                if (!dotnet_regex_char_in_class(
                        at(value, wrap(static_cast<std::int64_t>(candidate) + sets[i].distance)),
                        str(sets[i].set), ascii_[i]))
                    break;
            if (i == sets.size()) {
                pos = candidate;
                return true;
            }
        }
        pos = length;
        return false;
    }
    case M::LiteralAfterLoop_LeftToRight: {
        auto& literal = *d.literal_after_loop;
        auto loop = literal.loop;
        int search = pos;
        for (;;) {
            auto tail = slice(value, search);
            int found = literal.string  ? index(tail, str(literal.string))
                        : literal.chars ? any(tail, *literal.chars)
                                        : index(tail, literal.character);
            if (found < 0)
                break;
            int beginning = found;
            while (static_cast<std::uint32_t>(--beginning) < tail.size() &&
                   dotnet_regex_char_in_class(tail[beginning], str(loop->text()), ascii_[0])) {
            }
            if (found - beginning - 1 < loop->minimum()) {
                search = wrap(static_cast<std::int64_t>(search) + found + 1);
                continue;
            }
            pos = wrap(static_cast<std::int64_t>(search) + beginning + 1);
            return true;
        }
        pos = length;
        return false;
    }
    default:
        return true;
    }
}
}
