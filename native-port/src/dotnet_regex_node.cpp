// Copyright (c) .NET Foundation and Contributors.
// .NET8.0.31 RegexNode constructors/length/literal/set foundations; MIT third_party/dotnet_collation/LICENSE.
#include "opensim/framework/dotnet_regex_node.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/dotnet_regex_class_builder.hpp"
#include <algorithm>
#include <bit>
#include <limits>
namespace opensim::framework {
namespace {
using K = DotnetRegexNodeKind;
using namespace std::literals;
#ifdef _WIN32
constexpr auto exception_newline = "\r\n";
#else
constexpr auto exception_newline = "\n";
#endif
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
DotnetRegexClassText class_text(const DotnetRegexNodeText& text) {
    return text ? DotnetRegexClassText(*text) : std::nullopt;
}
std::int32_t wrap(std::int64_t value) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}
bool family(K kind, K normal, K lazy, K single, K atomic) {
    return kind == normal || kind == lazy || kind == single || kind == atomic;
}
bool stack_available(const DotnetRegexExecutionStack& stack) {
    if (!stack)
        throw std::invalid_argument("Regex node length analysis requires its execution-stack policy");
    return stack();
}
bool same_text(const DotnetRegexNodeText& a, const DotnetRegexNodeText& b) {
    return a == b || (a && b && *a == *b);
}
DotnetRegexNodeText concat_text(const DotnetRegexNodeText& a, const DotnetRegexNodeText& b) {
    if (!a || a->empty())
        return !b || b->empty() ? runtime_empty_text() : b;
    if (!b || b->empty())
        return a;
    return std::make_shared<const std::u16string>(*a + *b);
}
bool combine_counts(std::int32_t minimum, std::int32_t maximum, std::int32_t next_minimum,
                    std::int32_t next_maximum) {
    if (minimum == 2147483647 || next_minimum == 2147483647 ||
        static_cast<std::uint32_t>(static_cast<std::int64_t>(minimum) + next_minimum) >= 2147483647u)
        return false;
    return maximum == 2147483647 || next_maximum == 2147483647 ||
           static_cast<std::uint32_t>(static_cast<std::int64_t>(maximum) + next_maximum) < 2147483647u;
}
bool exceeds_depth(const DotnetRegexNode* node, int depth) {
    if (depth <= 0)
        return true;
    if (!node)
        null_reference();
    for (int i = 0; i < node->child_count(); ++i)
        if (exceeds_depth(node->child(i), depth - 1))
            return true;
    return false;
}
}
DotnetRegexNode* DotnetRegexNodeArena::create(K kind, std::int32_t options) {
    auto node = std::unique_ptr<DotnetRegexNode>(new DotnetRegexNode(kind, options));
    node->arena_ = this;
    auto result = node.get();
    nodes_.push_back(std::move(node));
    return result;
}
DotnetRegexNode* DotnetRegexNodeArena::create(K kind, std::int32_t options, char16_t c) {
    auto node = create(kind, options);
    node->ch_ = c;
    return node;
}
DotnetRegexNode* DotnetRegexNodeArena::create(K kind, std::int32_t options, DotnetRegexNodeText str) {
    auto node = create(kind, options);
    node->str_ = std::move(str);
    return node;
}
DotnetRegexNode* DotnetRegexNodeArena::create(K kind, std::int32_t options, std::int32_t m, std::int32_t n) {
    auto node = create(kind, options);
    node->m_ = m;
    node->n_ = n;
    return node;
}
DotnetRegexNode* DotnetRegexNodeArena::create_one_with_case_conversion(char16_t c, std::int32_t options,
                                                                       const DotnetRegexCultureName& culture,
                                                                       DotnetRegexCaseBehavior& behavior) {
    if (options & 1) {
        auto equivalents = dotnet_regex_case_equivalences(c, culture, behavior);
        if (equivalents.empty())
            return create(K::One, options & ~1, c);
        return create(K::Set, options & ~1, dotnet_regex_chars_to_string_class(equivalents));
    }
    return create(K::One, options, c);
}
bool DotnetRegexNode::is_set_family() const {
    return family(kind_, K::Setloop, K::Setlazy, K::Set, K::Setloopatomic);
}
bool DotnetRegexNode::is_one_family() const {
    return family(kind_, K::Oneloop, K::Onelazy, K::One, K::Oneloopatomic);
}
bool DotnetRegexNode::is_notone_family() const {
    return family(kind_, K::Notoneloop, K::Notonelazy, K::Notone, K::Notoneloopatomic);
}
std::vector<DotnetRegexNode*>& DotnetRegexNode::child_list() {
    if (auto list = std::get_if<std::vector<DotnetRegexNode*>>(&children_))
        return *list;
    if (std::holds_alternative<std::monostate>(children_))
        null_reference();
    throw DotnetRegexClassError(
        "System.InvalidCastException", 0x80004002u,
        "Unable to cast object of type 'System.Text.RegularExpressions.RegexNode' to type 'System.Collections.Generic.List`1[System.Text.RegularExpressions.RegexNode]'.");
}
void DotnetRegexNode::reduce_adjacent_strings() {
    bool adjacent = false;
    std::int32_t options = 0;
    auto& list = child_list();
    int input = 0, output = 0;
    while (input < static_cast<int>(list.size())) {
        auto node = required(list[input]);
        if (output < input)
            list[output] = node;
        if (node->kind_ == K::Concatenate && (node->options_ & 64) == (options_ & 64)) {
            if (auto nested = std::get_if<std::vector<DotnetRegexNode*>>(&node->children_)) {
                for (auto child : *nested)
                    required(child)->parent_ = this;
                auto copy = *nested;
                list.insert(list.begin() + input + 1, copy.begin(), copy.end());
            } else {
                auto child = required(node->child(0));
                child->parent_ = this;
                list.insert(list.begin() + input + 1, child);
            }
            --output;
        } else if (node->kind_ == K::One || node->kind_ == K::Multi) {
            auto current_options = node->options_ & 65;
            if (!adjacent || options != current_options) {
                adjacent = true;
                options = current_options;
            } else {
                auto previous = required(list[--output]);
                if (previous->kind_ == K::One) {
                    previous->kind_ = K::Multi;
                    previous->str_ = std::make_shared<const std::u16string>(1, previous->ch_);
                }
                if (node->kind_ == K::One) {
                    std::u16string text;
                    if (previous->str_)
                        text = *previous->str_;
                    if ((current_options & 64) == 0)
                        text.push_back(node->ch_);
                    else
                        text.insert(text.begin(), node->ch_);
                    previous->str_ = std::make_shared<const std::u16string>(std::move(text));
                } else
                    previous->str_ = (current_options & 64) == 0 ? concat_text(previous->str_, node->str_)
                                                                 : concat_text(node->str_, previous->str_);
            }
        } else if (node->kind_ == K::Empty)
            --output;
        else
            adjacent = false;
        ++input;
        ++output;
    }
    if (output < input)
        list.erase(list.begin() + output, list.begin() + input);
}
void DotnetRegexNode::reduce_adjacent_loops() {
    auto& list = child_list();
    int previous_index = 0, next_index = 1, output = 1;
    while (next_index < static_cast<int>(list.size())) {
        auto node = required(list[previous_index]);
        auto next = required(list[next_index]);
        auto merge_counts = [&]() {
            if (next->m_ > 0 && (node->kind_ == K::Oneloopatomic || node->kind_ == K::Notoneloopatomic ||
                                 node->kind_ == K::Setloopatomic))
                return false;
            if (!combine_counts(node->m_, node->n_, next->m_, next->n_))
                return false;
            node->m_ = wrap(static_cast<std::int64_t>(node->m_) + next->m_);
            if (node->n_ != 2147483647)
                node->n_ = next->n_ == 2147483647 ? 2147483647
                                                  : wrap(static_cast<std::int64_t>(node->n_) + next->n_);
            return true;
        };
        auto add_single = [&]() {
            if (!combine_counts(node->m_, node->n_, 1, 1))
                return false;
            node->m_ = wrap(static_cast<std::int64_t>(node->m_) + 1);
            if (node->n_ != 2147483647)
                node->n_ = wrap(static_cast<std::int64_t>(node->n_) + 1);
            return true;
        };
        auto adopt_loop = [&]() {
            if (!combine_counts(1, 1, next->m_, next->n_))
                return false;
            node->kind_ = next->kind_;
            node->m_ = wrap(static_cast<std::int64_t>(next->m_) + 1);
            node->n_ = next->n_ == 2147483647 ? 2147483647 : wrap(static_cast<std::int64_t>(next->n_) + 1);
            return true;
        };
        bool consumed = false;
        if (node->options_ == next->options_) {
            switch (node->kind_) {
            case K::Oneloop:
            case K::Onelazy:
                if (next->kind_ == node->kind_ && next->ch_ == node->ch_)
                    consumed = merge_counts();
                else if (next->kind_ == K::One && node->ch_ == next->ch_)
                    consumed = add_single();
                else if (next->kind_ == K::Multi && node->ch_ == next->first_char_of_one_or_multi()) {
                    auto text = required(next->str_);
                    std::size_t count = 1;
                    while (count < text.size() && text[count] == node->ch_)
                        ++count;
                    if (combine_counts(node->m_, node->n_, static_cast<std::int32_t>(count),
                                       static_cast<std::int32_t>(count))) {
                        node->m_ =
                            wrap(static_cast<std::int64_t>(node->m_) + static_cast<std::int32_t>(count));
                        if (node->n_ != 2147483647)
                            node->n_ =
                                wrap(static_cast<std::int64_t>(node->n_) + static_cast<std::int32_t>(count));
                        if (count == text.size())
                            consumed = true;
                        else if (text.size() - count == 1) {
                            next->kind_ = K::One;
                            next->ch_ = text.back();
                            next->str_.reset();
                        } else
                            next->str_ = std::make_shared<const std::u16string>(text.substr(count));
                    }
                }
                break;
            case K::Oneloopatomic:
            case K::Notoneloopatomic:
                if (next->kind_ == node->kind_ && next->ch_ == node->ch_)
                    consumed = merge_counts();
                break;
            case K::Notoneloop:
            case K::Notonelazy:
                if (next->kind_ == node->kind_ && next->ch_ == node->ch_)
                    consumed = merge_counts();
                else if (next->kind_ == K::Notone && next->ch_ == node->ch_)
                    consumed = add_single();
                break;
            case K::Setloopatomic:
                if (next->kind_ == node->kind_ && same_text(node->str_, next->str_))
                    consumed = merge_counts();
                break;
            case K::Setloop:
            case K::Setlazy:
                if (next->kind_ == node->kind_ && same_text(node->str_, next->str_))
                    consumed = merge_counts();
                else if (next->kind_ == K::Set && same_text(node->str_, next->str_))
                    consumed = add_single();
                break;
            case K::One:
                if ((next->kind_ == K::Oneloop || next->kind_ == K::Onelazy ||
                     next->kind_ == K::Oneloopatomic) &&
                    next->ch_ == node->ch_)
                    consumed = adopt_loop();
                break;
            case K::Notone:
                if ((next->kind_ == K::Notoneloop || next->kind_ == K::Notonelazy ||
                     next->kind_ == K::Notoneloopatomic) &&
                    next->ch_ == node->ch_)
                    consumed = adopt_loop();
                else if (next->kind_ == K::Notone && node->ch_ == next->ch_) {
                    node->kind_ = K::Notoneloop;
                    node->m_ = node->n_ = 2;
                    consumed = true;
                }
                break;
            case K::Set:
                if ((next->kind_ == K::Setloop || next->kind_ == K::Setlazy ||
                     next->kind_ == K::Setloopatomic) &&
                    same_text(node->str_, next->str_))
                    consumed = adopt_loop();
                else if (next->kind_ == K::Set && same_text(node->str_, next->str_)) {
                    node->kind_ = K::Setloop;
                    node->m_ = node->n_ = 2;
                    consumed = true;
                }
                break;
            default:
                break;
            }
        }
        if (consumed) {
            ++next_index;
            continue;
        }
        list[output++] = list[next_index];
        previous_index = next_index;
        ++next_index;
    }
    if (output < static_cast<int>(list.size()))
        list.erase(list.begin() + output, list.end());
}
DotnetRegexNode* DotnetRegexNode::reduce_concatenation() {
    switch (child_count()) {
    case 0:
        return arena_->create(K::Empty, options_);
    case 1:
        return child(0);
    default:
        for (int i = 0, count = child_count(); i < count; ++i) {
            auto node = required(child(i));
            if (node->kind_ == K::Nothing)
                return node;
        }
        reduce_adjacent_loops();
        reduce_adjacent_strings();
        return replace_node_if_unnecessary();
    }
}
DotnetRegexNode* DotnetRegexNode::replace_node_if_unnecessary() {
    switch (child_count()) {
    case 0:
        return arena_->create(kind_ == K::Alternate ? K::Nothing : K::Empty, options_);
    case 1:
        return child(0);
    default:
        return this;
    }
}
void DotnetRegexNode::add_child(DotnetRegexNode* node, const DotnetRegexExecutionStack& stack) {
    required(node)->parent_ = this;
    node = required(node->reduce(stack));
    node->parent_ = this;
    if (std::holds_alternative<std::monostate>(children_))
        children_ = node;
    else if (auto one = std::get_if<DotnetRegexNode*>(&children_))
        children_ = std::vector<DotnetRegexNode*>{*one, node};
    else
        child_list().push_back(node);
}
void DotnetRegexNode::insert_child(int index, DotnetRegexNode* node, const DotnetRegexExecutionStack& stack) {
    required(node)->parent_ = this;
    node = required(node->reduce(stack));
    node->parent_ = this;
    auto& list = child_list();
    if (index < 0 || static_cast<std::size_t>(index) > list.size())
        throw DotnetRegexClassError("System.ArgumentOutOfRangeException", 0x80131502u,
                                    "Index must be within the bounds of the List. (Parameter 'index')");
    list.insert(list.begin() + index, node);
}
void DotnetRegexNode::replace_child(int index, DotnetRegexNode* node,
                                    const DotnetRegexExecutionStack& stack) {
    required(node)->parent_ = this;
    node = required(node->reduce(stack));
    node->parent_ = this;
    if (std::holds_alternative<DotnetRegexNode*>(children_))
        children_ = node;
    else {
        child(index);
        child_list()[index] = node;
    }
}
DotnetRegexNode* DotnetRegexNode::reduce(const DotnetRegexExecutionStack& stack) {
    if (kind_ != K::Backreference)
        options_ &= ~1;
    switch (kind_) {
    case K::Alternate:
        return reduce_alternation(stack);
    case K::Atomic:
        return reduce_atomic(stack);
    case K::Concatenate:
        return reduce_concatenation();
    case K::Group:
        return reduce_group();
    case K::Loop:
    case K::Lazyloop:
        return reduce_loops();
    case K::PositiveLookaround:
    case K::NegativeLookaround:
        return reduce_lookaround(stack);
    case K::Setloop:
    case K::Setlazy:
    case K::Set:
    case K::Setloopatomic:
        return reduce_set();
    case K::ExpressionConditional:
        return reduce_expression_conditional(stack);
    case K::BackreferenceConditional:
        return reduce_backreference_conditional(stack);
    default:
        return this;
    }
}
DotnetRegexNode* DotnetRegexNode::final_optimize(const DotnetRegexExecutionStack& stack) {
    if ((options_ & (64 | 1024)) == 0) {
        find_and_make_loops_atomic(stack);
        eliminate_ending_backtracking(stack);
        auto node = required(child(0));
        bool directly_under_root = true;
        for (;;) {
            auto kind = node->kind_;
            if (kind == K::Concatenate) {
                directly_under_root = false;
                node = required(node->child(0));
                continue;
            }
            if (kind == K::Atomic) {
                node = required(node->child(0));
                continue;
            }
            bool eligible =
                kind == K::Oneloop || kind == K::Notoneloop || kind == K::Setloop ||
                kind == K::Oneloopatomic || kind == K::Notoneloopatomic || kind == K::Setloopatomic ||
                (!directly_under_root && (kind == K::Onelazy || kind == K::Notonelazy || kind == K::Setlazy));
            if (eligible && node->n_ == 2147483647) {
                auto parent = node->parent_;
                if (parent && parent->kind_ == K::Concatenate)
                    parent->insert_child(1, arena_->create(K::UpdateBumpalong, node->options_), stack);
            }
            break;
        }
    }
    return this;
}
DotnetRegexNode* DotnetRegexNode::make_quantifier(bool lazy, std::int32_t minimum, std::int32_t maximum,
                                                  const DotnetRegexExecutionStack& stack) {
    if (minimum == maximum && maximum <= 64) {
        if (maximum == 0)
            return arena_->create(K::Empty, options_);
        if (maximum == 1)
            return this;
        if (kind_ == K::One) {
            kind_ = K::Multi;
            if (maximum < 0) {
                auto value = std::to_string(maximum);
                throw DotnetRegexClassError("System.ArgumentOutOfRangeException", 0x80131502u,
                                            "count ('" + value +
                                                "') must be a non-negative value. (Parameter 'count')" +
                                                exception_newline + "Actual value was " + value + ".");
            }
            str_ = std::make_shared<const std::u16string>(maximum, ch_);
            ch_ = 0;
            return this;
        }
    }
    if (kind_ == K::One || kind_ == K::Notone || kind_ == K::Set) {
        kind_ = static_cast<K>(static_cast<std::uint8_t>(static_cast<unsigned>(kind_) +
                                                         static_cast<std::uint8_t>((lazy ? 6 : 3) - 9)));
        m_ = minimum;
        n_ = maximum;
        return this;
    }
    auto loop = arena_->create(lazy ? K::Lazyloop : K::Loop, options_, minimum, maximum);
    loop->add_child(this, stack);
    return loop;
}
void DotnetRegexNode::eliminate_ending_backtracking(const DotnetRegexExecutionStack& stack) {
    if (!stack_available(stack) || (options_ & (64 | 1024)) != 0)
        return;
    auto node = this;
    for (;;) {
        required(node);
        switch (node->kind_) {
        case K::Oneloop:
        case K::Notoneloop:
        case K::Setloop:
        case K::Onelazy:
        case K::Notonelazy:
        case K::Setlazy:
            node->make_loop_atomic();
            return;
        case K::PositiveLookaround:
        case K::NegativeLookaround:
        case K::Atomic:
            node = node->child(0);
            continue;
        case K::Concatenate:
        case K::Capture: {
            auto last = required(node->child(node->child_count() - 1));
            auto kind = last->kind_;
            bool wrap_atomic = kind == K::Alternate || kind == K::Loop || kind == K::Lazyloop ||
                               kind == K::BackreferenceConditional || kind == K::ExpressionConditional;
            if (wrap_atomic && (!node->parent_ || node->parent_->kind_ != K::Atomic)) {
                auto atomic = arena_->create(K::Atomic, last->options_);
                atomic->add_child(last, stack);
                node->replace_child(node->child_count() - 1, atomic, stack);
            }
            node = last;
            continue;
        }
        case K::Alternate:
        case K::BackreferenceConditional:
        case K::ExpressionConditional: {
            for (int i = 1, count = node->child_count(); i < count; ++i)
                required(node->child(i))->eliminate_ending_backtracking(stack);
            if (node->kind_ == K::ExpressionConditional)
                return;
            node = node->child(0);
            continue;
        }
        case K::Lazyloop:
            node->n_ = node->m_;
            break;
        case K::Loop:
            break;
        default:
            return;
        }
        if (node->n_ == 1) {
            node = node->child(0);
            continue;
        }
        auto last = node->find_last_expression_in_loop_for_auto_atomic(stack);
        if (!last)
            return;
        node = last;
    }
}
DotnetRegexNode* DotnetRegexNode::reduce_lookaround(const DotnetRegexExecutionStack& stack) {
    eliminate_ending_backtracking(stack);
    if (required(child(0))->kind_ == K::Empty) {
        kind_ = kind_ == K::PositiveLookaround ? K::Empty : K::Nothing;
        children_ = std::monostate{};
    }
    return this;
}
DotnetRegexNode* DotnetRegexNode::reduce_backreference_conditional(const DotnetRegexExecutionStack& stack) {
    if (child_count() == 1)
        add_child(arena_->create(K::Empty, options_), stack);
    return this;
}
DotnetRegexNode* DotnetRegexNode::reduce_expression_conditional(const DotnetRegexExecutionStack& stack) {
    if (child_count() == 2)
        add_child(arena_->create(K::Empty, options_), stack);
    auto condition = required(child(0));
    if (condition->kind_ == K::PositiveLookaround && (condition->options_ & 64) == 0)
        replace_child(0, condition->child(0), stack);
    required(child(0))->eliminate_ending_backtracking(stack);
    return this;
}
DotnetRegexNode* DotnetRegexNode::reduce_atomic(const DotnetRegexExecutionStack& stack) {
    if (options_ & 1024)
        return this;
    auto wrapper = this;
    auto node = required(child(0));
    while (node->kind_ == K::Atomic) {
        wrapper = node;
        node = required(wrapper->child(0));
    }
    switch (node->kind_) {
    case K::Nothing:
    case K::Empty:
    case K::Oneloopatomic:
    case K::Notoneloopatomic:
    case K::Setloopatomic:
        return node;
    case K::Oneloop:
    case K::Notoneloop:
    case K::Setloop:
    case K::Onelazy:
    case K::Notonelazy:
    case K::Setlazy:
        node->make_loop_atomic();
        return node;
    case K::Alternate: {
        if (options_ & 64)
            break;
        // Source uses 'as List', so a one-child representation fails as null here.
        if (!std::holds_alternative<std::vector<DotnetRegexNode*>>(node->children_))
            null_reference();
        auto& list = node->child_list();
        if (required(node->child(0))->kind_ == K::Empty)
            return arena_->create(K::Empty, node->options_);
        for (int i = 1; i < static_cast<int>(list.size()) - 1; ++i)
            if (required(list[i])->kind_ == K::Empty) {
                list.erase(list.begin() + i + 1, list.end());
                break;
            }
        bool changed = false;
        for (int j = 0; j < static_cast<int>(list.size()); ++j) {
            if (!required(list[j])->find_branch_one_or_multi_start())
                continue;
            int end = j + 1;
            while (end < static_cast<int>(list.size()) &&
                   required(list[end])->find_branch_one_or_multi_start())
                ++end;
            if (end - j >= 3) {
                int position = j;
                while (position < end) {
                    auto c = required(list[position]->find_branch_one_or_multi_start())
                                 ->first_char_of_one_or_multi();
                    while (position < end && required(list[position]->find_branch_one_or_multi_start())
                                                     ->first_char_of_one_or_multi() == c)
                        ++position;
                    if (position >= end)
                        continue;
                    for (int i = position + 1; i < end; ++i) {
                        auto candidate = list[i];
                        if (required(candidate->find_branch_one_or_multi_start())
                                ->first_char_of_one_or_multi() == c) {
                            list.erase(list.begin() + i);
                            list.insert(list.begin() + position++, candidate);
                            changed = true;
                        }
                    }
                }
            }
            j = end;
        }
        if (changed) {
            wrapper->replace_child(0, node, stack);
            node = required(wrapper->child(0));
        }
        break;
    }
    default:
        break;
    }
    node->eliminate_ending_backtracking(stack);
    return wrapper;
}
void DotnetRegexNode::remove_prefix(DotnetRegexNode* node, std::size_t count) {
    required(node);
    if (node->kind_ == K::One) {
        node->kind_ = K::Empty;
        node->ch_ = 0;
        return;
    }
    auto text = required(node->str_);
    if (text.size() == count) {
        node->kind_ = K::Empty;
        node->str_.reset();
    } else if (text.size() - 1 == count) {
        node->kind_ = K::One;
        node->ch_ = text.back();
        node->str_.reset();
    } else {
        if (count > text.size())
            throw DotnetRegexClassError(
                "System.ArgumentOutOfRangeException", 0x80131502u,
                "startIndex cannot be larger than length of string. (Parameter 'startIndex')");
        if (count != 0)
            node->str_ = std::make_shared<const std::u16string>(text.substr(count));
    }
}
DotnetRegexNode* DotnetRegexNode::extract_common_prefix_text(const DotnetRegexExecutionStack& stack) {
    auto& list = child_list();
    if (options_ & 64)
        return this;
    for (int i = 0; i < static_cast<int>(list.size()) - 1; ++i) {
        auto start = required(list[i])->find_branch_one_or_multi_start();
        if (!start)
            return this;
        auto options = start->options_;
        auto backing = start->str_;
        char16_t character = start->ch_;
        std::u16string_view prefix = backing ? std::u16string_view(*backing) : std::u16string_view{};
        if (start->kind_ == K::One)
            prefix = {&character, 1};
        int end = i + 1;
        for (; end < static_cast<int>(list.size()); ++end) {
            auto next = required(list[end])->find_branch_one_or_multi_start();
            if (!next || next->options_ != options)
                break;
            if (next->kind_ == K::One) {
                if (prefix.empty())
                    throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                                "Index was outside the bounds of the array.");
                if (prefix[0] != next->ch_)
                    break;
                if (prefix.size() != 1)
                    prefix = prefix.substr(0, 1);
            } else {
                auto text = required(next->str_);
                std::size_t length = 0, limit = std::min(prefix.size(), text.size());
                while (length < limit && prefix[length] == text[length])
                    ++length;
                if (length == 0)
                    break;
                prefix = prefix.substr(0, length);
            }
        }
        if (end - i > 1) {
            auto first = prefix.size() == 1 ? arena_->create(K::One, options, prefix[0])
                                            : arena_->create(K::Multi, options,
                                                             std::make_shared<const std::u16string>(prefix));
            auto alternatives = arena_->create(K::Alternate, options);
            for (int j = i; j < end; ++j) {
                auto branch = required(list[j]);
                remove_prefix(branch->kind_ == K::Concatenate ? branch->child(0) : branch, prefix.size());
                branch = required(branch->reduce(stack));
                alternatives->add_child(branch, stack);
            }
            if (parent_ && parent_->kind_ == K::Atomic) {
                auto atomic = arena_->create(K::Atomic, options);
                atomic->add_child(alternatives, stack);
                alternatives = atomic;
            }
            auto concat = arena_->create(K::Concatenate, options);
            concat->add_child(first, stack);
            concat->add_child(alternatives, stack);
            replace_child(i, concat, stack);
            list.erase(list.begin() + i + 1, list.begin() + end);
        }
    }
    return child_count() == 1 ? child(0) : this;
}
DotnetRegexNode*
DotnetRegexNode::extract_common_prefix_one_notone_set(const DotnetRegexExecutionStack& stack) {
    auto& list = child_list();
    if (options_ & 64)
        return this;
    for (auto branch : list) {
        required(branch);
        if (branch->kind_ != K::Concatenate || branch->child_count() < 2)
            return this;
    }
    for (int i = 0; i < static_cast<int>(list.size()) - 1; ++i) {
        auto first = required(list[i]->child(0));
        auto kind = static_cast<unsigned>(first->kind_);
        if (kind >= 3 && kind <= 8) {
            if (first->m_ != first->n_)
                continue;
        } else if (!((kind >= 9 && kind <= 11) || (kind >= 43 && kind <= 45)))
            continue;
        int end = i + 1;
        for (; end < static_cast<int>(list.size()); ++end) {
            auto next = required(list[end]->child(0));
            if (first->kind_ != next->kind_ || first->options_ != next->options_ || first->m_ != next->m_ ||
                first->n_ != next->n_ || first->ch_ != next->ch_ || !same_text(first->str_, next->str_))
                break;
        }
        if (end - i > 1) {
            auto alternatives = arena_->create(K::Alternate, options_);
            for (int j = i; j < end; ++j) {
                auto branch = required(list[j]);
                auto& children = branch->child_list();
                children.erase(children.begin());
                alternatives->add_child(branch, stack);
            }
            if (parent_ && parent_->kind_ == K::Atomic) {
                auto atomic = arena_->create(K::Atomic, options_);
                atomic->add_child(alternatives, stack);
                alternatives = atomic;
            }
            auto concat = arena_->create(K::Concatenate, options_);
            concat->add_child(first, stack);
            concat->add_child(alternatives, stack);
            replace_child(i, concat, stack);
            list.erase(list.begin() + i + 1, list.begin() + end);
        }
    }
    return replace_node_if_unnecessary();
}
DotnetRegexNode* DotnetRegexNode::reduce_alternation(const DotnetRegexExecutionStack& stack) {
    if (child_count() == 0)
        return arena_->create(K::Nothing, options_);
    if (child_count() == 1)
        return child(0);
    reduce_single_letter_and_nested_alternations();
    auto node = required(replace_node_if_unnecessary());
    if (node->kind_ == K::Alternate) {
        node = required(node->extract_common_prefix_text(stack));
        if (node->kind_ == K::Alternate) {
            node = required(node->extract_common_prefix_one_notone_set(stack));
            if (node->kind_ == K::Alternate)
                node = node->remove_redundant_empties_and_nothings();
        }
    }
    return node;
}
void DotnetRegexNode::reduce_single_letter_and_nested_alternations() {
    bool adjacent = false, unmergeable = false;
    std::int32_t options = 0;
    auto& list = child_list();
    int input = 0, output = 0;
    while (input < static_cast<int>(list.size())) {
        auto node = required(list[input]);
        if (output < input)
            list[output] = node;
        if (node->kind_ == K::Alternate) {
            if (auto nested = std::get_if<std::vector<DotnetRegexNode*>>(&node->children_)) {
                for (auto child : *nested)
                    required(child)->parent_ = this;
                auto copy = *nested;
                list.insert(list.begin() + input + 1, copy.begin(), copy.end());
            } else {
                auto child = required(node->child(0));
                child->parent_ = this;
                list.insert(list.begin() + input + 1, child);
            }
            --output;
        } else if (node->kind_ == K::One || node->kind_ == K::Set) {
            auto current_options = node->options_ & 65;
            bool start = !adjacent || options != current_options || unmergeable;
            if (node->kind_ == K::Set) {
                if (start || !dotnet_regex_class_mergeable(class_text(node->str_))) {
                    adjacent = true;
                    unmergeable = !dotnet_regex_class_mergeable(class_text(node->str_));
                    options = current_options;
                    ++input;
                    ++output;
                    continue;
                }
            } else if (start) {
                adjacent = true;
                unmergeable = false;
                options = current_options;
                ++input;
                ++output;
                continue;
            }
            auto previous = required(list[--output]);
            std::shared_ptr<DotnetRegexClassBuilder> builder;
            if (previous->kind_ == K::One) {
                builder = std::make_shared<DotnetRegexClassBuilder>();
                builder->add_char(previous->ch_);
            } else
                builder = DotnetRegexClassBuilder::parse(required(previous->str_));
            if (node->kind_ == K::One)
                builder->add_char(node->ch_);
            else
                builder->add_class(*DotnetRegexClassBuilder::parse(required(node->str_)));
            previous->kind_ = K::Set;
            previous->str_ = std::make_shared<const std::u16string>(builder->to_string_class());
            previous->options_ &= ~1;
        } else if (node->kind_ == K::Nothing)
            --output;
        else {
            adjacent = false;
            unmergeable = false;
        }
        ++input;
        ++output;
    }
    if (output < input)
        list.erase(list.begin() + output, list.begin() + input);
}
DotnetRegexNode* DotnetRegexNode::remove_redundant_empties_and_nothings() {
    auto& list = child_list();
    int output = 0;
    bool empty = false;
    for (auto node : list) {
        required(node);
        if (node->kind_ == K::Nothing)
            continue;
        if (node->kind_ == K::Empty) {
            if (empty)
                continue;
            empty = true;
        }
        list[output++] = node;
    }
    list.erase(list.begin() + output, list.end());
    return replace_node_if_unnecessary();
}
bool DotnetRegexNode::can_be_made_atomic(DotnetRegexNode* node, DotnetRegexNode* subsequent,
                                         bool iterate_nullable, bool allow_lazy,
                                         const DotnetRegexExecutionStack& stack) {
    if (!stack_available(stack))
        return false;
    for (;;) {
        auto next = required(subsequent);
        int count = next->child_count();
        if (count > 0 &&
            (next->kind_ == K::Concatenate || next->kind_ == K::Capture || next->kind_ == K::Atomic ||
             (next->kind_ == K::PositiveLookaround && (next->options_ & 64) == 0) ||
             ((next->kind_ == K::Loop || next->kind_ == K::Lazyloop) && next->m_ > 0))) {
            subsequent = next->child(0);
            continue;
        }
        auto current = required(node);
        if (current->options_ != next->options_)
            return false;
        if (next->kind_ == K::Alternate || (next->kind_ == K::ExpressionConditional && count == 3)) {
            for (int i = 0; i < count; ++i)
                if (!can_be_made_atomic(node, next->child(i), iterate_nullable, false, stack))
                    return false;
            return true;
        }
        auto kind = current->kind_;
        if (allow_lazy && (kind == K::Onelazy || kind == K::Notonelazy || kind == K::Setlazy))
            kind = static_cast<K>(static_cast<unsigned>(kind) - 3);
        // A successful disjoint required expression returns immediately. A disjoint
        // nullable expression advances through original Parent links below.
        bool nullable = false;
        auto repeated = [&](bool disjoint) {
            if (!disjoint || next->m_ < 0)
                return 0;
            return next->m_ > 0 ? 2 : 1;
        };
        int decision = 0;
        if (kind == K::Oneloop) {
            switch (next->kind_) {
            case K::One:
                decision = current->ch_ != next->ch_ ? 2 : 0;
                break;
            case K::Notone:
                decision = current->ch_ == next->ch_ ? 2 : 0;
                break;
            case K::Set:
                decision = !dotnet_regex_char_in_class(current->ch_, required(next->str_)) ? 2 : 0;
                break;
            case K::Oneloop:
            case K::Onelazy:
            case K::Oneloopatomic:
                decision = repeated(current->ch_ != next->ch_);
                break;
            case K::Notoneloop:
            case K::Notonelazy:
            case K::Notoneloopatomic:
                decision = repeated(current->ch_ == next->ch_);
                break;
            case K::Setloop:
            case K::Setlazy:
            case K::Setloopatomic:
                if (next->m_ >= 0) {
                    decision = repeated(!dotnet_regex_char_in_class(current->ch_, required(next->str_)));
                }
                break;
            case K::Multi:
                decision = current->ch_ != next->first_char_of_one_or_multi() ? 2 : 0;
                break;
            case K::Eol:
            case K::EndZ:
                decision = current->ch_ != u'\n' ? 2 : 0;
                break;
            case K::End:
                decision = 2;
                break;
            case K::Boundary:
                nullable = current->m_ > 0 && dotnet_regex_boundary_word_char(current->ch_);
                break;
            case K::NonBoundary:
                nullable = current->m_ > 0 && !dotnet_regex_boundary_word_char(current->ch_);
                break;
            case K::ECMABoundary:
                nullable = current->m_ > 0 && dotnet_regex_ecma_word_char(current->ch_);
                break;
            case K::NonECMABoundary:
                nullable = current->m_ > 0 && !dotnet_regex_ecma_word_char(current->ch_);
                break;
            default:
                break;
            }
        } else if (kind == K::Notoneloop) {
            switch (next->kind_) {
            case K::One:
                decision = current->ch_ == next->ch_ ? 2 : 0;
                break;
            case K::Multi:
                decision = current->ch_ == next->first_char_of_one_or_multi() ? 2 : 0;
                break;
            case K::End:
                decision = 2;
                break;
            case K::Oneloop:
            case K::Onelazy:
            case K::Oneloopatomic:
                decision = repeated(current->ch_ == next->ch_);
                break;
            default:
                break;
            }
        } else if (kind == K::Setloop) {
            auto equals = [&](std::u16string_view value) { return current->str_ && *current->str_ == value; };
            switch (next->kind_) {
            case K::One:
                decision = !dotnet_regex_char_in_class(next->ch_, required(current->str_)) ? 2 : 0;
                break;
            case K::Set:
                decision =
                    !dotnet_regex_may_overlap(class_text(current->str_), class_text(next->str_)) ? 2 : 0;
                break;
            case K::Oneloop:
            case K::Onelazy:
            case K::Oneloopatomic:
                if (next->m_ >= 0) {
                    decision = repeated(!dotnet_regex_char_in_class(next->ch_, required(current->str_)));
                }
                break;
            case K::Setloop:
            case K::Setlazy:
            case K::Setloopatomic:
                if (next->m_ >= 0) {
                    decision = repeated(
                        !dotnet_regex_may_overlap(class_text(current->str_), class_text(next->str_)));
                }
                break;
            case K::Multi: {
                auto first = next->first_char_of_one_or_multi();
                decision = !dotnet_regex_char_in_class(first, required(current->str_)) ? 2 : 0;
                break;
            }
            case K::Eol:
            case K::EndZ:
                decision = !dotnet_regex_char_in_class(u'\n', required(current->str_)) ? 2 : 0;
                break;
            case K::End:
                decision = 2;
                break;
            case K::Boundary:
                nullable =
                    current->m_ > 0 && (equals(u"\0\0\n\0\x0002\x0004\x0005\x0003\x0001\x0006\t\x0013\0"sv) ||
                                        equals(u"\0\0\x0001\t"sv));
                break;
            case K::NonBoundary:
                nullable = current->m_ > 0 &&
                           (equals(u"\0\0\n\0\xfffe\xfffc\xfffb\xfffd\xffff\xfffa\xfff7\xffed\0"sv) ||
                            equals(u"\0\0\x0001\xfff7"sv));
                break;
            case K::ECMABoundary:
                nullable = current->m_ > 0 && (equals(u"\0\n\0"
                                                      u"0:A[_`a{\x0130\x0131"sv) ||
                                               equals(u"\0\x0002\0"
                                                      u"0:"sv));
                break;
            case K::NonECMABoundary:
                nullable = current->m_ > 0 && (equals(u"\x0001\n\0"
                                                      u"0:A[_`a{\x0130\x0131"sv) ||
                                               equals(u"\0\0\x0001\xfff7"sv));
                break;
            default:
                break;
            }
        }
        if (decision == 2)
            return true;
        if (!(nullable || decision == 1) || !iterate_nullable)
            return false;
        for (;;) {
            auto parent = next->parent_;
            if (!parent)
                return true;
            if (parent->kind_ == K::Alternate || parent->kind_ == K::Capture || parent->kind_ == K::Atomic) {
                next = parent;
                continue;
            }
            if (parent->kind_ != K::Concatenate)
                return false;
            auto& list = parent->child_list();
            auto found = std::find(list.begin(), list.end(), next);
            auto index = found == list.end() ? -1 : static_cast<int>(found - list.begin());
            if (index + 1 != static_cast<int>(list.size())) {
                subsequent = list[index + 1];
                break;
            }
            next = parent;
        }
    }
}
DotnetRegexNode*
DotnetRegexNode::find_last_expression_in_loop_for_auto_atomic(const DotnetRegexExecutionStack& stack) {
    auto node = required(child(0));
    while (node->kind_ == K::Capture)
        node = required(node->child(0));
    if (node->kind_ == K::Concatenate) {
        auto last = node->child(node->child_count() - 1);
        if (can_be_made_atomic(last, node->child(0), false, false, stack))
            return last;
    }
    return nullptr;
}
void DotnetRegexNode::find_and_make_loops_atomic(const DotnetRegexExecutionStack& stack) {
    if (!stack_available(stack) || (options_ & 64) != 0)
        return;
    int count = child_count();
    for (int i = 0; i < count; ++i)
        required(child(i))->find_and_make_loops_atomic(stack);
    if (kind_ == K::Concatenate) {
        if (std::holds_alternative<std::monostate>(children_))
            return;
        auto& list = child_list();
        for (int i = 0; i < count - 1; ++i)
            process_auto_atomic(list[i], list[i + 1], stack);
    }
}
void DotnetRegexNode::process_auto_atomic(DotnetRegexNode* node, DotnetRegexNode* subsequent,
                                          const DotnetRegexExecutionStack& stack) {
    if (!stack_available(stack))
        return;
    for (;;) {
        required(node);
        if (node->kind_ == K::Concatenate || node->kind_ == K::Capture)
            node = node->child(node->child_count() - 1);
        else if (node->kind_ == K::Loop) {
            auto last = node->find_last_expression_in_loop_for_auto_atomic(stack);
            if (!last)
                break;
            node = last;
        } else
            break;
    }
    switch (node->kind_) {
    case K::Oneloop:
    case K::Notoneloop:
    case K::Setloop:
        if (can_be_made_atomic(node, subsequent, true, false, stack))
            node->make_loop_atomic();
        break;
    case K::Onelazy:
    case K::Notonelazy:
    case K::Setlazy:
        if (can_be_made_atomic(node, subsequent, false, true, stack)) {
            node->kind_ = static_cast<K>(static_cast<unsigned>(node->kind_) - 3);
            node->make_loop_atomic();
        }
        break;
    case K::Alternate:
    case K::BackreferenceConditional:
    case K::ExpressionConditional:
        for (int i = node->kind_ == K::ExpressionConditional ? 1 : 0, count = node->child_count(); i < count;
             ++i)
            process_auto_atomic(node->child(i), subsequent, stack);
        break;
    default:
        break;
    }
}
DotnetRegexNode* DotnetRegexNode::reduce_group() {
    auto node = this;
    while (required(node)->kind_ == K::Group)
        node = node->child(0);
    return node;
}
DotnetRegexNode* DotnetRegexNode::reduce_loops() {
    auto node = this;
    const auto original_kind = kind_;
    auto minimum = m_, maximum = n_;
    while (node->child_count() > 0) {
        auto next = required(node->child(0));
        if (next->kind_ != original_kind) {
            bool compatible = false;
            auto kind = static_cast<unsigned>(next->kind_);
            if (original_kind == K::Loop)
                compatible = (kind >= 3 && kind <= 5) || (kind >= 43 && kind <= 45);
            else
                compatible = kind >= 6 && kind <= 8;
            if (!compatible)
                break;
        }
        if ((node->m_ == 0 && next->m_ > 1) || next->n_ < wrap(static_cast<std::int64_t>(next->m_) * 2))
            break;
        node = next;
        if (node->m_ > 0)
            minimum = node->m_ =
                (2147483646 / node->m_ < minimum ? 2147483647
                                                 : wrap(static_cast<std::int64_t>(node->m_) * minimum));
        if (node->n_ > 0)
            maximum = node->n_ =
                (2147483646 / node->n_ < maximum ? 2147483647
                                                 : wrap(static_cast<std::int64_t>(node->n_) * maximum));
    }
    if (minimum == 2147483647)
        return arena_->create(K::Nothing, options_);
    if (node->child_count() == 1) {
        auto leaf = required(node->child(0));
        if (leaf->kind_ == K::One || leaf->kind_ == K::Notone || leaf->kind_ == K::Set) {
            auto repeat = node->kind_ == K::Lazyloop ? K::Onelazy : K::Oneloop;
            leaf->kind_ = static_cast<K>(
                static_cast<std::uint8_t>(static_cast<unsigned>(leaf->kind_) +
                                          static_cast<std::uint8_t>(static_cast<int>(repeat) - 9)));
            leaf->m_ = node->m_;
            leaf->n_ = node->n_;
            node = leaf;
        }
    }
    return node;
}
int DotnetRegexNode::child_count() const {
    if (std::holds_alternative<std::monostate>(children_))
        return 0;
    if (auto list = std::get_if<std::vector<DotnetRegexNode*>>(&children_))
        return static_cast<int>(list->size());
    return 1;
}
DotnetRegexNode* DotnetRegexNode::child(int index) const {
    if (auto one = std::get_if<DotnetRegexNode*>(&children_))
        return *one;
    auto list = std::get_if<std::vector<DotnetRegexNode*>>(&children_);
    if (!list)
        null_reference();
    if (index < 0 || static_cast<std::size_t>(index) >= list->size())
        throw DotnetRegexClassError(
            "System.ArgumentOutOfRangeException", 0x80131502u,
            "Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')");
    return (*list)[index];
}
DotnetRegexNode* DotnetRegexNode::reverse_concatenation_if_right_to_left() {
    if ((options_ & 64) && kind_ == K::Concatenate && child_count() > 1) {
        auto& list = std::get<std::vector<DotnetRegexNode*>>(children_);
        std::reverse(list.begin(), list.end());
    }
    return this;
}
void DotnetRegexNode::make_loop_atomic() {
    if (kind_ == K::Oneloop || kind_ == K::Notoneloop || kind_ == K::Setloop)
        kind_ = static_cast<K>(static_cast<unsigned>(kind_) + 40);
    else if (kind_ == K::Onelazy || kind_ == K::Notonelazy || kind_ == K::Setlazy) {
        kind_ = static_cast<K>(static_cast<unsigned>(kind_) + 37);
        n_ = m_;
        if (n_ == 0) {
            kind_ = K::Empty;
            str_.reset();
            ch_ = 0;
        } else if (kind_ == K::Oneloopatomic && n_ >= 2 && n_ <= 64) {
            kind_ = K::Multi;
            str_ = std::make_shared<const std::u16string>(n_, ch_);
            ch_ = 0;
            m_ = n_ = 0;
        }
    }
}
DotnetRegexNode* DotnetRegexNode::reduce_set() {
    auto text = class_text(str_);
    if (dotnet_regex_class_empty(text)) {
        kind_ = K::Nothing;
        str_.reset();
    } else if (dotnet_regex_class_singleton(text)) {
        ch_ = dotnet_regex_singleton_char(text);
        str_.reset();
        kind_ = kind_ == K::Set             ? K::One
                : kind_ == K::Setloop       ? K::Oneloop
                : kind_ == K::Setloopatomic ? K::Oneloopatomic
                                            : K::Onelazy;
    } else if (dotnet_regex_class_singleton_inverse(text)) {
        ch_ = dotnet_regex_singleton_char(text);
        str_.reset();
        kind_ = kind_ == K::Set             ? K::Notone
                : kind_ == K::Setloop       ? K::Notoneloop
                : kind_ == K::Setloopatomic ? K::Notoneloopatomic
                                            : K::Notonelazy;
    }
    if (str_) {
        auto set = *str_;
        constexpr auto word = u"\0\u0002\u0004\u0005\u0003\u0001\u0006\t\u0013\0"sv;
        constexpr auto notword = u"\0\ufffe\ufffc\ufffb\ufffd\uffff\ufffa\ufff7\uffed\0"sv;
        std::u16string head = {0, 0, 20};
        std::u16string combined = head;
        combined += word;
        combined += notword;
        std::u16string reversed = head;
        reversed += notword;
        reversed += word;
        if (set == combined || set == reversed || set == u"\0\0\u0002\t\ufff7"sv ||
            set == u"\0\0\u0002\ufff7\t"sv || set == u"\0\0\u0002d\uff9c"sv ||
            set == u"\0\0\u0002\uff9c"
                   u"d"sv) {
            static const auto any = dotnet_regex_class_literal(std::u16string{0, 1, 0, 0});
            str_ = any;
        }
    }
    return this;
}
std::int32_t DotnetRegexNode::compute_min_length(const DotnetRegexExecutionStack& stack) const {
    if (!stack_available(stack))
        return 0;
    if (is_one_family() || is_notone_family() || is_set_family()) {
        if (kind_ == K::One || kind_ == K::Notone || kind_ == K::Set)
            return 1;
        return m_;
    }
    switch (kind_) {
    case K::Multi:
        return static_cast<std::int32_t>(required(str_).size());
    case K::Loop:
    case K::Lazyloop:
        return wrap(std::min<std::int64_t>(2147483646, static_cast<std::int64_t>(m_) *
                                                           required(child(0))->compute_min_length(stack)));
    case K::Alternate: {
        int minimum = required(child(0))->compute_min_length(stack);
        for (int i = 1, count = child_count(); i < count; ++i) {
            if (minimum <= 0)
                break;
            minimum = std::min(minimum, required(child(i))->compute_min_length(stack));
        }
        return minimum;
    }
    case K::BackreferenceConditional: {
        auto first = required(child(0))->compute_min_length(stack);
        return std::min(first, required(child(1))->compute_min_length(stack));
    }
    case K::ExpressionConditional: {
        auto first = required(child(1))->compute_min_length(stack);
        return std::min(first, required(child(2))->compute_min_length(stack));
    }
    case K::Concatenate: {
        std::int64_t sum = 0;
        for (int i = 0, count = child_count(); i < count; ++i)
            sum += required(child(i))->compute_min_length(stack);
        return wrap(std::min<std::int64_t>(2147483646, sum));
    }
    case K::Capture:
    case K::Group:
    case K::Atomic:
        return required(child(0))->compute_min_length(stack);
    default:
        return 0;
    }
}
std::optional<std::int32_t>
DotnetRegexNode::compute_max_length(const DotnetRegexExecutionStack& stack) const {
    if (!stack_available(stack))
        return {};
    if (is_one_family() || is_notone_family() || is_set_family()) {
        if (kind_ == K::One || kind_ == K::Notone || kind_ == K::Set)
            return 1;
        if (n_ != 2147483647)
            return n_;
        return {};
    }
    switch (kind_) {
    case K::Multi:
        return static_cast<std::int32_t>(required(str_).size());
    case K::Loop:
    case K::Lazyloop:
        if (n_ != 2147483647) {
            if (auto length = required(child(0))->compute_max_length(stack)) {
                auto product = static_cast<std::int64_t>(n_) * (*length);
                if (product < 2147483647)
                    return wrap(product);
            }
        }
        return {};
    case K::Alternate: {
        auto length = required(child(0))->compute_max_length(stack);
        if (!length)
            return {};
        int maximum = *length;
        for (int i = 1, count = child_count(); i < count; ++i) {
            length = required(child(i))->compute_max_length(stack);
            if (!length)
                return {};
            maximum = std::max(maximum, *length);
        }
        return maximum;
    }
    case K::BackreferenceConditional:
    case K::ExpressionConditional: {
        int first = kind_ == K::BackreferenceConditional ? 0 : 1;
        auto length = required(child(first))->compute_max_length(stack);
        if (!length)
            return {};
        auto next = required(child(first + 1))->compute_max_length(stack);
        if (!next)
            return {};
        return std::max(*length, *next);
    }
    case K::Concatenate: {
        std::int64_t sum = 0;
        for (int i = 0, count = child_count(); i < count; ++i) {
            auto length = required(child(i))->compute_max_length(stack);
            if (!length)
                return {};
            sum += *length;
        }
        if (sum < 2147483647)
            return wrap(sum);
        return {};
    }
    case K::Capture:
    case K::Atomic:
        return required(child(0))->compute_max_length(stack);
    case K::Backreference:
        return {};
    default:
        return 0;
    }
}
bool DotnetRegexNode::supports_compilation(std::optional<std::u16string>& reason) const {
    if (options_ & 1024) {
        reason = u"RegexOptions.NonBacktracking isn't supported";
        return false;
    }
    if (exceeds_depth(this, 40)) {
        reason = u"the expression may result exceeding run-time or compiler limits";
        return false;
    }
    reason.reset();
    return true;
}
DotnetRegexNode* DotnetRegexNode::find_branch_one_or_multi_start() {
    auto node = kind_ == K::Concatenate ? required(child(0)) : this;
    return node->kind_ == K::One || node->kind_ == K::Multi ? node : nullptr;
}
char16_t DotnetRegexNode::first_char_of_one_or_multi() const {
    if (kind_ == K::One)
        return ch_;
    auto text = required(str_);
    if (text.empty())
        throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                    "Index was outside the bounds of the array.");
    return text[0];
}
DotnetRegexNode* DotnetRegexNode::find_starting_literal_node() {
    auto node = this;
    while (node && (node->options_ & 64) == 0) {
        if (node->is_one_family() || node->is_notone_family() || node->is_set_family()) {
            if (node->kind_ == K::One || node->kind_ == K::Notone || node->kind_ == K::Set || node->m_ > 0)
                return node;
            break;
        }
        if (node->kind_ == K::Multi)
            return node;
        switch (node->kind_) {
        case K::Loop:
        case K::Lazyloop:
            if (node->m_ <= 0)
                return nullptr;
            break;
        case K::Concatenate:
        case K::Capture:
        case K::Group:
        case K::PositiveLookaround:
        case K::Atomic:
            break;
        default:
            return nullptr;
        }
        node = node->child(0);
    }
    return nullptr;
}
std::optional<DotnetRegexStartingLiteral> DotnetRegexNode::find_starting_literal(int capacity) {
    auto node = find_starting_literal_node();
    if (!node)
        return {};
    DotnetRegexStartingLiteral data;
    if (node->is_one_family() || node->is_notone_family()) {
        data.range = {node->ch_, node->ch_};
        data.negated = node->is_notone_family();
        return data;
    }
    if (node->kind_ == K::Multi) {
        data.string = node->str_;
        return data;
    }
    if (node->is_set_family()) {
        if (capacity < 0)
            throw std::invalid_argument("Regex starting literal requires nonnegative stack-buffer capacity");
        std::vector<char16_t> buffer(capacity);
        auto set = class_text(node->str_);
        int count = dotnet_regex_set_chars(set, buffer);
        if (count) {
            data.set_chars = std::make_shared<const std::u16string>(buffer.data(), count);
            data.negated = dotnet_regex_class_negated(set);
            return data;
        }
        if (dotnet_regex_single_range(set, data.range.first, data.range.last)) {
            data.negated = dotnet_regex_class_negated(set);
            return data;
        }
        if (auto ascii = dotnet_regex_ascii_set_chars(set)) {
            data.ascii_chars = std::make_shared<const std::vector<char16_t>>(ascii->begin(), ascii->end());
            data.negated = dotnet_regex_class_negated(set);
            return data;
        }
    }
    return {};
}
bool DotnetRegexNode::try_get_ordinal_case_insensitive_string(int start, int bound, int& consumed,
                                                              DotnetRegexNodeText& result, bool zero_width) {
    std::u16string value;
    char16_t chars[2];
    int index = start, capacity = 32;
    auto ensure = [&](int count) {
        int position = static_cast<int>(value.size());
        if (position > wrap(static_cast<std::int64_t>(capacity) - count)) {
            auto minimum =
                std::max(static_cast<std::uint32_t>(wrap(static_cast<std::int64_t>(position) + count)),
                         std::min(static_cast<std::uint32_t>(capacity) * 2u, 2147483591u));
            int length = std::bit_cast<std::int32_t>(minimum);
            if (length < 0) {
                auto actual = std::to_string(length);
                throw DotnetRegexClassError(
                    "System.ArgumentOutOfRangeException", 0x80131502u,
                    "minimumLength ('" + actual +
                        "') must be a non-negative value. (Parameter 'minimumLength')" + exception_newline +
                        "Actual value was " + actual + ".");
            }
            capacity =
                minimum > (1u << 30) ? length : static_cast<int>(std::bit_ceil(std::max(16u, minimum)));
        }
    };
    for (; index < bound; ++index) {
        auto node = required(child(index));
        auto kind = node->kind_;
        if (kind == K::One) {
            if (node->ch_ >= 128 || dotnet_regex_participates_in_case_conversion(node->ch_))
                break;
            ensure(1);
            value.push_back(node->ch_);
            continue;
        }
        if (kind == K::Multi) {
            auto str = node->str_ ? std::u16string_view(*node->str_) : std::u16string_view{};
            if (!dotnet_regex_is_ascii(std::span<const char16_t>(str)) ||
                dotnet_regex_participates_in_case_conversion(std::span<const char16_t>(str)))
                break;
            ensure(static_cast<int>(str.size()));
            value += str;
            continue;
        }
        if (kind == K::Set || ((kind == K::Setloop || kind == K::Setlazy || kind == K::Setloopatomic) &&
                               node->m_ == node->n_)) {
            auto set = class_text(node->str_);
            if (dotnet_regex_class_negated(set) || dotnet_regex_set_chars(set, chars) != 2 ||
                chars[0] >= 128 || chars[1] >= 128 || chars[0] == chars[1] ||
                !((chars[0] >= u'A' && chars[0] <= u'Z') || (chars[0] >= u'a' && chars[0] <= u'z')) ||
                !((chars[1] >= u'A' && chars[1] <= u'Z') || (chars[1] >= u'a' && chars[1] <= u'z')) ||
                (chars[0] | 32) != (chars[1] | 32))
                break;
            int count = kind == K::Set ? 1 : node->m_;
            ensure(count);
            if (count < 0)
                throw DotnetRegexClassError("System.ArgumentOutOfRangeException", 0x80131502u,
                                            "Specified argument was out of the range of valid values.");
            value.append(count, static_cast<char16_t>(chars[0] | 32));
        } else {
            if (kind == K::Empty)
                continue;
            bool can_skip = false;
            if (zero_width)
                switch (kind) {
                case K::Bol:
                case K::Boundary:
                case K::NonBoundary:
                case K::Beginning:
                case K::Start:
                case K::PositiveLookaround:
                case K::NegativeLookaround:
                case K::ECMABoundary:
                case K::NonECMABoundary:
                case K::UpdateBumpalong:
                    can_skip = true;
                    break;
                default:
                    break;
                }
            if (!can_skip)
                break;
        }
    }
    if (value.size() >= 2) {
        result = std::make_shared<const std::u16string>(std::move(value));
        consumed = index - start;
        return true;
    }
    result.reset();
    consumed = 0;
    return false;
}
}
