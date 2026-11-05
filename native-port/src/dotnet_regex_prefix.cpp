// Copyright (c) .NET Foundation and Contributors.
// .NET8.0.31 RegexPrefixAnalyzer; MIT third_party/dotnet_collation/LICENSE.
#include "opensim/framework/dotnet_regex_prefix.hpp"
#include "opensim/framework/text.hpp"
#include "dotnet_regex_sort.hpp"
#include <array>
#include <bit>
namespace opensim::framework {
namespace {
using K = DotnetRegexNodeKind;
using Sets = std::vector<DotnetRegexFixedDistanceSet>;
using Builder = std::shared_ptr<DotnetRegexClassBuilder>;
[[noreturn]] void null_reference() {
    throw DotnetRegexClassError("System.NullReferenceException", 0x80004003u,
                                "Object reference not set to an instance of an object.");
}
DotnetRegexNode* required(DotnetRegexNode* node) {
    if (!node)
        null_reference();
    return node;
}
std::u16string_view required(const DotnetRegexNodeText& text) {
    if (!text)
        null_reference();
    return *text;
}
bool available(const DotnetRegexExecutionStack& stack) {
    if (!stack)
        throw std::invalid_argument("Regex prefix analysis requires its execution-stack policy");
    return stack();
}
int wrap(std::int64_t value) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}
DotnetRegexNodeText text(std::u16string value) {
    return value.empty() ? runtime_empty_text() : std::make_shared<const std::u16string>(std::move(value));
}
DotnetRegexClassText class_text(const DotnetRegexNodeText& value) {
    return value ? DotnetRegexClassText(*value) : std::nullopt;
}
bool prefix(DotnetRegexNode* node, std::u16string& output, const DotnetRegexExecutionStack& stack) {
    if (!available(stack))
        return false;
    required(node);
    bool rtl = (node->options() & 64) != 0;
    switch (node->kind()) {
    case K::Concatenate:
        for (int i = 0; i < node->child_count(); ++i)
            if (!prefix(node->child(i), output, stack))
                return false;
        return !rtl;
    case K::Alternate: {
        int count = node->child_count();
        auto start = output.size();
        prefix(node->child(0), output, stack);
        auto length = output.size() - start;
        if (length) {
            std::u16string next;
            for (int i = 1; i < count; ++i) {
                if (length == 0)
                    break;
                next.clear();
                prefix(node->child(i), next, stack);
                std::size_t common = 0;
                while (common < length && common < next.size() && output[start + common] == next[common])
                    ++common;
                length = common;
            }
            output.resize(start + length);
        }
        return false;
    }
    case K::One:
        output.push_back(node->character());
        return !rtl;
    case K::Multi:
        if (node->text())
            output += *node->text();
        return !rtl;
    case K::Oneloop:
    case K::Onelazy:
    case K::Oneloopatomic:
        if (node->minimum() > 0) {
            int count = std::min(node->minimum(), 32);
            output.append(count, node->character());
            return count == node->maximum() && !rtl;
        }
        break;
    case K::Loop:
    case K::Lazyloop:
        if (node->minimum() > 0) {
            int count = std::min(node->minimum(), 4);
            for (int i = 0; i < count; ++i)
                if (!prefix(node->child(0), output, stack))
                    return false;
            return count == node->maximum() && !rtl;
        }
        break;
    case K::Capture:
    case K::Atomic:
        return prefix(node->child(0), output, stack);
    case K::Bol:
    case K::Eol:
    case K::Boundary:
    case K::NonBoundary:
    case K::Beginning:
    case K::Start:
    case K::EndZ:
    case K::End:
    case K::Empty:
    case K::PositiveLookaround:
    case K::NegativeLookaround:
    case K::ECMABoundary:
    case K::NonECMABoundary:
    case K::UpdateBumpalong:
        return true;
    default:
        break;
    }
    return false;
}
std::optional<bool> first_class(DotnetRegexNode* node, Builder& builder,
                                const DotnetRegexExecutionStack& stack) {
    if (!available(stack))
        return false;
    required(node);
    switch (node->kind()) {
    case K::Oneloop:
    case K::Onelazy:
    case K::One:
    case K::Oneloopatomic:
        if (!builder || builder->can_merge()) {
            if (!builder)
                builder = std::make_shared<DotnetRegexClassBuilder>();
            builder->add_char(node->character());
            if (node->kind() != K::One && node->minimum() <= 0)
                return {};
            return true;
        }
        return false;
    case K::Notoneloop:
    case K::Notonelazy:
    case K::Notone:
    case K::Notoneloopatomic:
        if (!builder || builder->can_merge()) {
            if (!builder)
                builder = std::make_shared<DotnetRegexClassBuilder>();
            auto c = node->character();
            if (c > 0)
                builder->add_range(0, c - 1);
            if (c < 65535)
                builder->add_range(c + 1, 65535);
            if (node->kind() != K::Notone && node->minimum() <= 0)
                return {};
            return true;
        }
        return false;
    case K::Setloop:
    case K::Setlazy:
    case K::Set:
    case K::Setloopatomic: {
        bool merged = false;
        if (!builder) {
            builder = DotnetRegexClassBuilder::parse(required(node->text()));
            merged = true;
        } else if (builder->can_merge()) {
            auto next = DotnetRegexClassBuilder::parse(required(node->text()));
            if (next && next->can_merge()) {
                builder->add_class(*next);
                merged = true;
            }
        }
        if (merged) {
            if (node->kind() != K::Set && node->minimum() <= 0)
                return {};
            return true;
        }
        return false;
    }
    case K::Multi:
        if (!builder || builder->can_merge()) {
            if (!builder)
                builder = std::make_shared<DotnetRegexClassBuilder>();
            auto value = required(node->text());
            if (value.empty())
                throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                            "Index was outside the bounds of the array.");
            builder->add_char(value[(node->options() & 64) ? value.size() - 1 : 0]);
            return true;
        }
        return false;
    case K::Bol:
    case K::Eol:
    case K::Boundary:
    case K::NonBoundary:
    case K::Beginning:
    case K::Start:
    case K::EndZ:
    case K::End:
    case K::Nothing:
    case K::Empty:
    case K::PositiveLookaround:
    case K::NegativeLookaround:
    case K::ECMABoundary:
    case K::NonECMABoundary:
    case K::UpdateBumpalong:
        return {};
    case K::Capture:
    case K::Atomic:
        return first_class(node->child(0), builder, stack);
    case K::Loop:
    case K::Lazyloop: {
        auto result = first_class(node->child(0), builder, stack);
        if (!result)
            return {};
        if (!*result)
            return false;
        return node->minimum() == 0 ? std::optional<bool>{} : std::optional<bool>{true};
    }
    case K::Concatenate:
        for (int i = 0; i < node->child_count(); ++i) {
            auto result = first_class(node->child(i), builder, stack);
            if (result.has_value())
                return result;
        }
        return {};
    case K::Alternate: {
        bool nullable = false;
        for (int i = 0; i < node->child_count(); ++i) {
            auto result = first_class(node->child(i), builder, stack);
            if (!result)
                nullable = true;
            else if (!*result)
                return false;
        }
        return nullable ? std::optional<bool>{} : std::optional<bool>{true};
    }
    case K::BackreferenceConditional:
    case K::ExpressionConditional: {
        int start = node->kind() == K::BackreferenceConditional ? 0 : 1;
        auto first = first_class(node->child(start), builder, stack);
        auto second = first_class(node->child(start + 1), builder, stack);
        if (first == true && second == true)
            return true;
        if (first == false || second == false)
            return false;
        return {};
    }
    default:
        return false;
    }
}
bool raw_fixed_sets(DotnetRegexNode* node, Sets& results, int& distance, bool thorough,
                    const DotnetRegexExecutionStack& stack) {
    if (!available(stack))
        return false;
    required(node);
    if (node->options() & 64)
        return false;
    auto append = [&](DotnetRegexNodeText set) {
        results.push_back({{}, std::move(set), distance});
        distance = wrap(static_cast<std::int64_t>(distance) + 1);
    };
    switch (node->kind()) {
    case K::One:
        if (results.size() < 50) {
            append(dotnet_regex_one_to_string_class(node->character()));
            return true;
        }
        return false;
    case K::Oneloop:
    case K::Onelazy:
    case K::Oneloopatomic:
        if (node->minimum() > 0) {
            auto set = dotnet_regex_one_to_string_class(node->character());
            int count = std::min(node->minimum(), 20), i = 0;
            for (; i < count; ++i) {
                if (results.size() >= 50)
                    break;
                append(set);
            }
            return i == node->minimum() && i == node->maximum();
        }
        break;
    case K::Multi: {
        auto value = required(node->text());
        std::size_t i = 0;
        for (; i < value.size(); ++i) {
            if (results.size() >= 50)
                break;
            append(dotnet_regex_one_to_string_class(value[i]));
        }
        return i == value.size();
    }
    case K::Set:
        if (results.size() < 50) {
            append(node->text());
            return true;
        }
        return false;
    case K::Setloop:
    case K::Setlazy:
    case K::Setloopatomic:
        if (node->minimum() > 0) {
            int count = std::min(node->minimum(), 20), i = 0;
            for (; i < count; ++i) {
                if (results.size() >= 50)
                    break;
                append(node->text());
            }
            return i == node->minimum() && i == node->maximum();
        }
        break;
    case K::Notone:
        distance = wrap(static_cast<std::int64_t>(distance) + 1);
        return true;
    case K::Notoneloop:
    case K::Notonelazy:
    case K::Notoneloopatomic:
        if (node->minimum() == node->maximum()) {
            distance = wrap(static_cast<std::int64_t>(distance) + node->minimum());
            return true;
        }
        break;
    case K::Bol:
    case K::Eol:
    case K::Boundary:
    case K::NonBoundary:
    case K::Beginning:
    case K::Start:
    case K::EndZ:
    case K::End:
    case K::Empty:
    case K::PositiveLookaround:
    case K::NegativeLookaround:
    case K::ECMABoundary:
    case K::NonECMABoundary:
    case K::UpdateBumpalong:
        return true;
    case K::Capture:
    case K::Group:
    case K::Atomic:
        return raw_fixed_sets(node->child(0), results, distance, thorough, stack);
    case K::Loop:
    case K::Lazyloop:
        if (node->minimum() > 0) {
            raw_fixed_sets(node->child(0), results, distance, thorough, stack);
            return false;
        }
        break;
    case K::Concatenate:
        for (int i = 0; i < node->child_count(); ++i)
            if (!raw_fixed_sets(node->child(i), results, distance, thorough, stack))
                return false;
        return true;
    case K::Alternate:
        if (thorough) {
            int count = node->child_count();
            bool fixed = true;
            std::optional<int> length;
            // Dictionary enumeration follows first insertion, not numeric key order.
            struct Entry {
                int distance;
                Builder builder;
                int count;
            };
            std::vector<Entry> entries;
            Sets branch;
            for (int i = 0; i < count; ++i) {
                branch.clear();
                int branch_distance = 0;
                bool branch_fixed = raw_fixed_sets(node->child(i), branch, branch_distance, thorough, stack);
                fixed &= branch_fixed;
                if (branch.empty())
                    return false;
                if (fixed) {
                    if (!length)
                        length = branch_distance;
                    else if (*length != branch_distance)
                        fixed = false;
                }
                for (auto& item : branch) {
                    auto found = std::find_if(entries.begin(), entries.end(),
                                              [&](auto& entry) { return entry.distance == item.distance; });
                    if (found != entries.end()) {
                        if (found->builder->try_add_class(
                                *DotnetRegexClassBuilder::parse(required(item.set))))
                            ++found->count;
                    } else
                        entries.push_back(
                            {item.distance, DotnetRegexClassBuilder::parse(required(item.set)), 1});
                }
            }
            for (auto& entry : entries) {
                if (results.size() >= 50) {
                    fixed = false;
                    break;
                }
                if (entry.count == count)
                    results.push_back({{},
                                       text(entry.builder->to_string_class()),
                                       wrap(static_cast<std::int64_t>(entry.distance) + distance)});
            }
            if (fixed) {
                if (!length)
                    throw DotnetRegexClassError("System.InvalidOperationException", 0x80131509u,
                                                "Nullable object must have a value.");
                distance = wrap(static_cast<std::int64_t>(distance) + *length);
                return true;
            }
            return false;
        }
        break;
    default:
        break;
    }
    return false;
}
constexpr float frequency[] = {
    0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.001f, 0.0f,   0.0f,   0.0f,
    0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.0f,   0.003f, 0.0f,   0.0f,   0.0f,   0.0f,   0.004f,
    0.0f,   0.0f,   0.006f, 0.006f, 0.0f,   0.0f,   8.952f, 0.065f, 0.42f,  0.01f,  0.011f, 0.005f, 0.07f,
    0.05f,  3.911f, 3.91f,  0.356f, 2.775f, 1.411f, 0.173f, 2.054f, 0.677f, 1.199f, 0.87f,  0.729f, 0.491f,
    0.335f, 0.269f, 0.435f, 0.24f,  0.234f, 0.196f, 0.144f, 0.983f, 0.357f, 0.661f, 0.371f, 0.088f, 0.007f,
    0.763f, 0.229f, 0.551f, 0.306f, 0.449f, 0.337f, 0.162f, 0.131f, 0.489f, 0.031f, 0.035f, 0.301f, 0.205f,
    0.253f, 0.228f, 0.288f, 0.034f, 0.38f,  0.73f,  0.675f, 0.265f, 0.309f, 0.137f, 0.084f, 0.023f, 0.023f,
    0.591f, 0.085f, 0.59f,  0.013f, 0.797f, 0.001f, 4.596f, 1.296f, 2.081f, 2.005f, 6.903f, 1.494f, 1.019f,
    1.024f, 3.75f,  0.286f, 0.439f, 2.913f, 1.459f, 3.908f, 3.23f,  1.444f, 0.231f, 4.22f,  3.924f, 5.312f,
    2.112f, 0.737f, 0.573f, 0.992f, 1.067f, 0.181f, 0.391f, 0.056f, 0.391f, 0.002f, 0.0f};
int compare_sets(const DotnetRegexFixedDistanceSet& a, const DotnetRegexFixedDistanceSet& b) {
    auto compare = [](auto x, auto y) { return x < y ? -1 : x > y ? 1 : 0; };
    int ac = a.chars ? static_cast<int>(a.chars->size()) : 0,
        bc = b.chars ? static_cast<int>(b.chars->size()) : 0;
    auto range_length = [](const DotnetRegexFixedDistanceSet& item) {
        if (!item.range)
            return 0;
        int size = static_cast<int>(item.range->last) - item.range->first + 1;
        return item.negated ? 65536 - size : size;
    };
    int ar = range_length(a), br = range_length(b);
    if (a.negated != b.negated)
        return b.negated ? -1 : 1;
    if (!a.negated) {
        if (a.chars && b.chars) {
            auto sum = [](auto& chars) {
                float result = 0;
                for (auto c : *chars)
                    if (c < 128)
                        result += frequency[c];
                return result;
            };
            float af = sum(a.chars), bf = sum(b.chars);
            if (af != bf)
                return compare(af, bf);
            if (!dotnet_regex_is_ascii(*a.chars) && !dotnet_regex_is_ascii(*b.chars))
                return compare(ac, bc);
        }
        if ((ac > 0 && br > 0) || (ar > 0 && bc > 0)) {
            int result = compare(std::max(ac, ar), std::max(bc, br));
            if (result)
                return result;
            return ac <= 0 ? 1 : -1;
        }
        if ((ac > 0) != (bc > 0))
            return ac <= 0 ? 1 : -1;
    }
    if ((ar > 0) != (br > 0))
        return ar <= 0 ? 1 : -1;
    return ar > 0 ? compare(ar, br) : compare(a.distance, b.distance);
}
K anchor(DotnetRegexNode* node, bool leading, const DotnetRegexExecutionStack& stack) {
    if (!available(stack))
        return K::Unknown;
    for (;;) {
        required(node);
        switch (node->kind()) {
        case K::Bol:
        case K::Eol:
        case K::Boundary:
        case K::Beginning:
        case K::Start:
        case K::EndZ:
        case K::End:
        case K::ECMABoundary:
            return node->kind();
        case K::Capture:
        case K::Atomic:
            node = node->child(0);
            continue;
        case K::Concatenate: {
            DotnetRegexNode* next = nullptr;
            int count = node->child_count();
            for (int i = 0; i < count; ++i) {
                auto child = required(node->child(leading ? i : count - 1 - i));
                auto kind = child->kind();
                if (kind != K::Empty && kind != K::PositiveLookaround && kind != K::NegativeLookaround) {
                    next = child;
                    break;
                }
            }
            if (next) {
                node = next;
                continue;
            }
            break;
        }
        case K::Alternate: {
            auto result = anchor(node->child(0), leading, stack);
            if (result == K::Unknown)
                return result;
            for (int i = 1; i < node->child_count(); ++i)
                if (anchor(node->child(i), leading, stack) != result)
                    return K::Unknown;
            return result;
        }
        default:
            break;
        }
        return K::Unknown;
    }
}
}
DotnetRegexNodeText dotnet_regex_find_prefix(DotnetRegexNode* node, const DotnetRegexExecutionStack& stack) {
    std::u16string value;
    prefix(node, value, stack);
    return text(std::move(value));
}
DotnetRegexNodeText dotnet_regex_find_ordinal_case_insensitive_prefix(DotnetRegexNode* node) {
    for (;;) {
        required(node);
        auto kind = node->kind();
        if (kind == K::Capture || kind == K::Atomic ||
            ((kind == K::Loop || kind == K::Lazyloop) && node->minimum() > 0)) {
            node = node->child(0);
            continue;
        }
        if (kind == K::Concatenate) {
            int consumed = 0;
            DotnetRegexNodeText result;
            node->try_get_ordinal_case_insensitive_string(0, node->child_count(), consumed, result, true);
            return result;
        }
        return {};
    }
}
DotnetRegexNodeText dotnet_regex_find_first_char_class(DotnetRegexNode* node,
                                                       const DotnetRegexExecutionStack& stack) {
    Builder builder;
    if (first_class(node, builder, stack) != true)
        return {};
    if (!builder)
        null_reference();
    return text(builder->to_string_class());
}
std::optional<Sets> dotnet_regex_find_fixed_distance_sets(DotnetRegexNode* root, bool thorough,
                                                          const DotnetRegexExecutionStack& stack) {
    Sets results;
    int distance = 0;
    raw_fixed_sets(root, results, distance, thorough, stack);
    auto any = dotnet_regex_class_literal(std::u16string{0, 1, 0, 0});
    std::erase_if(results, [&](auto& item) { return item.set && *item.set == *any; });
    if (results.empty()) {
        auto first = dotnet_regex_find_first_char_class(root, stack);
        if (!first || *first == *any)
            return {};
        results.push_back({{}, first, 0});
    }
    std::array<char16_t, 128> buffer;
    for (auto& item : results) {
        auto set = class_text(item.set);
        item.negated = dotnet_regex_class_negated(set);
        int count = dotnet_regex_set_chars(set, buffer);
        if (count > 0)
            item.chars =
                std::make_shared<const std::vector<char16_t>>(buffer.begin(), buffer.begin() + count);
        char16_t low = 0, high = 0;
        if (thorough && (!item.chars || item.chars->size() > 2) &&
            dotnet_regex_single_range(set, low, high)) {
            item.chars.reset();
            item.range = DotnetRegexRange{low, high};
        }
    }
    return results;
}
std::optional<DotnetRegexLiteralAfterLoop> dotnet_regex_find_literal_after_loop(DotnetRegexNode* node) {
    required(node);
    if (node->options() & 64)
        return {};
    while (node->kind() == K::Capture || node->kind() == K::Atomic)
        node = required(node->child(0));
    if (node->kind() != K::Concatenate)
        return {};
    auto loop = required(node->child(0));
    auto kind = loop->kind();
    if ((kind != K::Setloop && kind != K::Setlazy && kind != K::Setloopatomic) ||
        loop->maximum() != 2147483647)
        return {};
    auto next = required(node->child(1));
    if (next->kind() == K::UpdateBumpalong) {
        if (node->child_count() == 2)
            return {};
        next = required(node->child(2));
    }
    DotnetRegexLiteralAfterLoop result;
    result.loop = loop;
    switch (next->kind()) {
    case K::One:
        if (!dotnet_regex_char_in_class(next->character(), required(loop->text()))) {
            result.character = next->character();
            return result;
        }
        break;
    case K::Multi: {
        auto value = required(next->text());
        if (value.empty())
            throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                        "Index was outside the bounds of the array.");
        auto first = value[0];
        if (!dotnet_regex_char_in_class(first, required(loop->text()))) {
            result.string = next->text();
            return result;
        }
        break;
    }
    case K::Set: {
        auto set = class_text(next->text());
        if (dotnet_regex_class_negated(set))
            break;
        std::array<char16_t, 5> chars;
        int count = dotnet_regex_set_chars(set, chars);
        if (count == 0)
            break;
        for (int i = 0; i < count; ++i)
            if (dotnet_regex_char_in_class(chars[i], required(loop->text())))
                return {};
        result.chars = std::make_shared<const std::vector<char16_t>>(chars.begin(), chars.begin() + count);
        return result;
    }
    default:
        break;
    }
    return {};
}
void dotnet_regex_sort_fixed_distance_sets(Sets& values) {
    regex_detail::sort(values, compare_sets);
}
DotnetRegexNodeKind dotnet_regex_find_leading_anchor(DotnetRegexNode* node,
                                                     const DotnetRegexExecutionStack& stack) {
    return anchor(node, true, stack);
}
DotnetRegexNodeKind dotnet_regex_find_trailing_anchor(DotnetRegexNode* node,
                                                      const DotnetRegexExecutionStack& stack) {
    return anchor(node, false, stack);
}
}
