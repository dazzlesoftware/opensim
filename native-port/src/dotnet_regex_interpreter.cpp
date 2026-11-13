// Copyright (c) .NET Foundation and Contributors.
// .NET8.0.31 RegexInterpreter/RegexRunner/Match storage; MIT runtime license.
#include "opensim/framework/dotnet_regex_interpreter.hpp"
#include "opensim/framework/execution_stack.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
namespace opensim::framework {
namespace {
[[noreturn]] void index_error() {
    throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                "Index was outside the bounds of the array.");
}
[[noreturn]] void null_reference() {
    throw DotnetRegexClassError("System.NullReferenceException", 0x80004003u,
                                "Object reference not set to an instance of an object.");
}
int& at(std::vector<int>& values, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= values.size())
        index_error();
    return values[index];
}
char16_t at(std::u16string_view value, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= value.size())
        index_error();
    return value[index];
}
int wrap(std::int64_t value) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}
std::int64_t ticks_now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
struct MatchStorage {
    std::vector<std::vector<int>> matches;
    std::vector<int> counts;
    bool balancing = false;
    explicit MatchStorage(int count) : matches(count), counts(count) {
        if (count > 0)
            matches[0].resize(2);
    }
    void reset() {
        std::fill(counts.begin(), counts.end(), 0);
        balancing = false;
    }
    std::vector<int>& group(int cap) {
        if (cap < 0 || static_cast<std::size_t>(cap) >= matches.size())
            index_error();
        return matches[cap];
    }
    int& count(int cap) {
        return at(counts, cap);
    }
    void add(int cap, int start, int length) {
        auto& values = group(cap);
        if (values.empty())
            values.resize(2);
        int size = count(cap);
        if (size * 2 + 2 > static_cast<int>(values.size()))
            values.resize(size * 8);
        at(values, size * 2) = start;
        at(values, size * 2 + 1) = length;
        count(cap) = size + 1;
    }
    void balance(int cap) {
        balancing = true;
        int position = count(cap) * 2 - 2;
        auto& values = group(cap);
        if (at(values, position) < 0)
            position = -3 - at(values, position);
        position -= 2;
        if (position >= 0 && at(values, position) < 0) {
            int start = at(values, position), length = at(values, position + 1);
            add(cap, start, length);
        } else
            add(cap, -3 - position, -4 - position);
    }
    void remove(int cap) {
        --count(cap);
    }
    bool matched(int cap) {
        return static_cast<unsigned>(cap) < counts.size() && counts[cap] > 0 &&
               at(group(cap), counts[cap] * 2 - 1) != -2;
    }
    int index(int cap) {
        auto& values = group(cap);
        int value = at(values, count(cap) * 2 - 2);
        return value < 0 ? at(values, -3 - value) : value;
    }
    int length(int cap) {
        auto& values = group(cap);
        int value = at(values, count(cap) * 2 - 1);
        return value < 0 ? at(values, -3 - value) : value;
    }
    bool found() {
        return count(0) > 0;
    }
    void tidy_balancing() {
        if (!balancing)
            return;
        for (int cap = 0; cap < static_cast<int>(counts.size()); ++cap) {
            auto& values = group(cap);
            int size = count(cap) * 2, i = 0;
            while (i < size && at(values, i) >= 0)
                ++i;
            int output = i;
            for (; i < size; ++i) {
                if (at(values, i) < 0) {
                    --output;
                    continue;
                }
                if (i != output)
                    at(values, output) = at(values, i);
                ++output;
            }
            count(cap) = output / 2;
        }
        balancing = false;
    }
};
}
DotnetRegexMatchTimeout::DotnetRegexMatchTimeout(DotnetRegexNodeText text, DotnetRegexNodeText expression,
                                                 int milliseconds)
    : DotnetRegexClassError(
          "System.Text.RegularExpressions.RegexMatchTimeoutException", 0x80131505u,
          "The Regex engine has timed out while trying to match a pattern to an input string. This can occur "
          "for many reasons, including very large inputs or excessive backtracking caused by nested "
          "quantifiers, back-references and other factors."),
      input(std::move(text)), pattern(std::move(expression)),
      timeout_ticks(static_cast<std::int64_t>(milliseconds) * 10000) {
}
void dotnet_regex_validate_timeout(std::int64_t ticks) {
    if (ticks != -10000 && static_cast<std::uint64_t>(ticks) - 1u >= 21474836460000ULL)
        throw DotnetRegexClassError(
            "System.ArgumentOutOfRangeException", 0x80131502u,
            "Specified argument was out of the range of valid values. (Parameter 'matchTimeout')");
}
struct DotnetRegexInterpreter::Impl {
    DotnetRegexParsedPattern tree;
    DotnetRegexNodeText pattern, input;
    DotnetRegexBytecode code;
    DotnetRegexFindOptimizations find;
    DotnetRegexCaseBehavior case_behavior = DotnetRegexCaseBehavior::NotSet;
    DotnetRegexCultureName culture;
    MatchStorage match;
    std::vector<int> track, marks, crawl;
    int tp = 0, sp = 0, cp = 0, textpos = 0, textstart = 0, textend = 0, codepos = 0, op = 0;
    bool rtl = false, check_timeout = false;
    int timeout = 0;
    std::int64_t deadline = 0;
    DotnetRegexTickCount clock;
    Impl(DotnetRegexParsedPattern value, DotnetRegexNodeText expression,
         const DotnetRegexExecutionStack& stack)
        : tree(std::move(value)), pattern(std::move(expression)), code(dotnet_regex_write(tree)),
          find(tree, stack),
          culture(tree.backreference_culture ? *tree.backreference_culture : DotnetRegexCultureName{}),
          match(tree.capture_count) {
        if (!culture)
            culture = []() -> std::u16string { null_reference(); };
    }
    void check() {
        if (check_timeout && clock() >= deadline)
            throw DotnetRegexMatchTimeout(input ? input : runtime_empty_text(), pattern, timeout);
    }
    static void double_array(std::vector<int>& array, int& position) {
        int count = static_cast<int>(array.size());
        array.resize(count * 2);
        std::copy_backward(array.begin(), array.begin() + count, array.end());
        std::fill(array.begin(), array.begin() + count, 0);
        position += count;
    }
    void ensure() {
        int minimum = code.track_count * 4;
        if (sp < minimum)
            double_array(marks, sp);
        if (tp < minimum)
            double_array(track, tp);
    }
    void track_push(std::initializer_list<int> values = {}, bool second = false) {
        for (auto value : values)
            at(track, --tp) = value;
        at(track, --tp) = second ? -codepos : codepos;
    }
    void stack_push(std::initializer_list<int> values) {
        for (auto value : values)
            at(marks, --sp) = value;
    }
    int track_peek(int offset = 0) {
        return at(track, tp - offset - 1);
    }
    int stack_peek(int offset = 0) {
        return at(marks, sp - offset - 1);
    }
    void set_operator(int value) {
        op = value & ~64;
        rtl = (value & 64) != 0;
    }
    int operand(int offset) {
        return at(code.codes, codepos + offset + 1);
    }
    void advance(int count) {
        codepos += count + 1;
        set_operator(at(code.codes, codepos));
    }
    void go(int target) {
        if (target < codepos)
            ensure();
        codepos = target;
        set_operator(at(code.codes, target));
    }
    void backtrack() {
        check();
        int target = at(track, tp++), flag = 128;
        if (target < 0) {
            target = -target;
            flag = 256;
        }
        set_operator(at(code.codes, target) | flag);
        if (target < codepos)
            ensure();
        codepos = target;
    }
    int track_position() {
        return static_cast<int>(track.size()) - tp;
    }
    int crawl_position() {
        return static_cast<int>(crawl.size()) - cp;
    }
    void track_to(int position) {
        tp = static_cast<int>(track.size()) - position;
    }
    void add_crawl(int cap) {
        if (cp == 0)
            double_array(crawl, cp);
        at(crawl, --cp) = cap;
    }
    void capture(int cap, int start, int end) {
        if (end < start)
            std::swap(start, end);
        add_crawl(cap);
        match.add(cap, start, end - start);
    }
    void transfer(int cap, int uncap, int start, int end) {
        if (end < start)
            std::swap(start, end);
        int old_start = match.index(uncap), old_end = old_start + match.length(uncap);
        if (start >= old_end) {
            end = start;
            start = old_end;
        } else if (end <= old_start)
            start = old_start;
        else {
            if (end > old_end)
                end = old_end;
            if (old_start > start)
                start = old_start;
        }
        add_crawl(uncap);
        match.balance(uncap);
        if (cap != -1) {
            add_crawl(cap);
            match.add(cap, start, end - start);
        }
    }
    void uncapture() {
        match.remove(at(crawl, cp++));
    }
    int bump() {
        return rtl ? -1 : 1;
    }
    int forward_chars() {
        return rtl ? textpos : textend - textpos;
    }
    char16_t char_next(std::u16string_view value) {
        int index = rtl ? --textpos : textpos++;
        if (index < 0 || static_cast<std::size_t>(index) >= value.size())
            index_error();
        return value[index];
    }
    void backward() {
        textpos += rtl ? 1 : -1;
    }
    bool in_set(char16_t c, int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= code.strings.size())
            index_error();
        if (!code.strings[index])
            null_reference();
        return dotnet_regex_char_in_class(c, *code.strings[index], code.strings_ascii_lookup[index]);
    }
    bool match_string(const std::u16string& str, std::u16string_view value) {
        int count = static_cast<int>(str.size()), position;
        if (!rtl) {
            if (static_cast<int>(value.size()) - textpos < count)
                return false;
            position = textpos + count;
        } else {
            if (textpos < count)
                return false;
            position = textpos;
        }
        while (count != 0)
            if (str[--count] != at(value, --position))
                return false;
        if (!rtl)
            position += static_cast<int>(str.size());
        textpos = position;
        return true;
    }
    bool match_ref(int index, int length, std::u16string_view value, bool ignore_case) {
        int position;
        if (!rtl) {
            if (static_cast<int>(value.size()) - textpos < length)
                return false;
            position = textpos + length;
        } else {
            if (textpos < length)
                return false;
            position = textpos;
        }
        int end = index + length, count = length;
        while (count-- != 0) {
            auto a = at(value, --end), b = at(value, --position);
            if (a == b)
                continue;
            if (!ignore_case)
                return false;
            if (!culture)
                null_reference();
            auto equivalents = dotnet_regex_case_equivalences(a, culture, case_behavior);
            if (std::find(equivalents.begin(), equivalents.end(), b) == equivalents.end())
                return false;
        }
        if (!rtl)
            position += length;
        textpos = position;
        return true;
    }
    bool boundary(std::u16string_view value, bool ecma) {
        int previous = textpos - 1;
        auto word = [&](char16_t c) {
            return ecma ? dotnet_regex_ecma_word_char(c) : dotnet_regex_boundary_word_char(c);
        };
        return (static_cast<unsigned>(previous) < value.size() && word(value[previous])) !=
               (static_cast<unsigned>(textpos) < value.size() && word(value[textpos]));
    }
    bool try_match(std::u16string_view value);
    DotnetRegexScanResult scan(DotnetRegexNodeText, int, int, int, int, DotnetRegexRunnerMode, std::int64_t,
                               DotnetRegexTickCount);
};
bool DotnetRegexInterpreter::Impl::try_match(std::u16string_view value) {
    set_operator(at(code.codes, 0));
    codepos = 0;
    int next = -1;
    for (;;) {
        if (next >= 0) {
            advance(next);
            next = -1;
        }
        switch (op) {
        case 40:
            return match.found();
        case 38:
            go(operand(0));
            continue;
        case 37:
            if (match.matched(operand(0))) {
                next = 1;
                continue;
            }
            break;
        case 23:
            track_push({textpos});
            next = 1;
            continue;
        case 151:
            ++tp;
            textpos = track_peek();
            go(operand(0));
            continue;
        case 31:
            stack_push({textpos});
            track_push();
            next = 0;
            continue;
        case 30:
            stack_push({-1});
            track_push();
            next = 0;
            continue;
        case 158:
        case 159:
            ++sp;
            break;
        case 33:
            ++sp;
            track_push({stack_peek()});
            textpos = stack_peek();
            next = 0;
            continue;
        case 161:
            ++tp;
            stack_push({track_peek()});
            break;
        case 32:
            if (operand(1) == -1 || match.matched(operand(1))) {
                ++sp;
                if (operand(1) != -1)
                    transfer(operand(0), operand(1), stack_peek(), textpos);
                else
                    capture(operand(0), stack_peek(), textpos);
                track_push({stack_peek()});
                next = 2;
                continue;
            }
            break;
        case 160:
            ++tp;
            stack_push({track_peek()});
            uncapture();
            if (operand(0) != -1 && operand(1) != -1)
                uncapture();
            break;
        case 24:
            ++sp;
            if (textpos != stack_peek()) {
                track_push({stack_peek(), textpos});
                stack_push({textpos});
                go(operand(0));
            } else {
                track_push({stack_peek()}, true);
                next = 1;
            }
            continue;
        case 152:
            tp += 2;
            ++sp;
            textpos = track_peek(1);
            track_push({track_peek()}, true);
            next = 1;
            continue;
        case 280:
            ++tp;
            stack_push({track_peek()});
            break;
        case 25: {
            ++sp;
            int mark = stack_peek();
            if (textpos != mark) {
                if (mark != -1)
                    track_push({mark, textpos});
                else
                    track_push({textpos, textpos});
            } else {
                stack_push({mark});
                track_push({stack_peek()}, true);
            }
            next = 1;
            continue;
        }
        case 153: {
            tp += 2;
            int position = track_peek(1);
            track_push({track_peek()}, true);
            stack_push({position});
            textpos = position;
            go(operand(0));
            continue;
        }
        case 281:
            ++sp;
            ++tp;
            stack_push({track_peek()});
            break;
        case 27:
            stack_push({textpos, operand(0)});
            track_push();
            next = 1;
            continue;
        case 26:
            stack_push({-1, operand(0)});
            track_push();
            next = 1;
            continue;
        case 154:
        case 155:
        case 162:
            sp += 2;
            break;
        case 28: {
            sp += 2;
            int mark = stack_peek(), count = stack_peek(1), distance = textpos - mark;
            if (count >= operand(1) || (distance == 0 && count >= 0)) {
                track_push({mark, count}, true);
                next = 2;
            } else {
                track_push({mark});
                stack_push({textpos, wrap(static_cast<std::int64_t>(count) + 1)});
                go(operand(0));
            }
            continue;
        }
        case 156:
            ++tp;
            sp += 2;
            if (stack_peek(1) > 0) {
                textpos = stack_peek();
                track_push({track_peek(), stack_peek(1) - 1}, true);
                next = 2;
                continue;
            }
            stack_push({track_peek(), stack_peek(1) - 1});
            break;
        case 284:
            tp += 2;
            stack_push({track_peek(), track_peek(1)});
            break;
        case 29: {
            sp += 2;
            int mark = stack_peek(), count = stack_peek(1);
            if (count < 0) {
                track_push({mark}, true);
                stack_push({textpos, count + 1});
                go(operand(0));
            } else {
                track_push({mark, count, textpos});
                next = 2;
            }
            continue;
        }
        case 157: {
            tp += 3;
            int mark = track_peek(), position = track_peek(2);
            if (track_peek(1) < operand(1) && position != mark) {
                textpos = position;
                stack_push({position, track_peek(1) + 1});
                track_push({mark}, true);
                go(operand(0));
                continue;
            }
            stack_push({track_peek(), track_peek(1)});
            break;
        }
        case 285:
            ++tp;
            sp += 2;
            stack_push({track_peek(), stack_peek(1) - 1});
            break;
        case 34:
            check();
            stack_push({track_position(), crawl_position()});
            track_push();
            next = 0;
            continue;
        case 35:
            sp += 2;
            track_to(stack_peek());
            while (crawl_position() != stack_peek(1))
                uncapture();
            break;
        case 36:
            sp += 2;
            track_to(stack_peek());
            track_push({stack_peek(1)});
            next = 0;
            continue;
        case 164:
            ++tp;
            while (crawl_position() != track_peek())
                uncapture();
            break;
        case 14: {
            int previous = textpos - 1;
            if (static_cast<unsigned>(previous) >= value.size() || value[previous] == u'\n') {
                next = 0;
                continue;
            }
            break;
        }
        case 15:
            if (static_cast<unsigned>(textpos) >= value.size() || value[textpos] == u'\n') {
                next = 0;
                continue;
            }
            break;
        case 16:
        case 17:
        case 41:
        case 42:
            if (boundary(value, op == 41 || op == 42) == (op == 16 || op == 41)) {
                next = 0;
                continue;
            }
            break;
        case 18:
            if (textpos <= 0) {
                next = 0;
                continue;
            }
            break;
        case 19:
            if (textpos == textstart) {
                next = 0;
                continue;
            }
            break;
        case 20:
            if (textpos >= static_cast<int>(value.size()) - 1 &&
                (static_cast<unsigned>(textpos) >= value.size() || value[textpos] == u'\n')) {
                next = 0;
                continue;
            }
            break;
        case 21:
            if (textpos >= static_cast<int>(value.size())) {
                next = 0;
                continue;
            }
            break;
        case 9:
            if (forward_chars() >= 1 && char_next(value) == static_cast<char16_t>(operand(0))) {
                next = 1;
                continue;
            }
            break;
        case 10:
            if (forward_chars() >= 1 && char_next(value) != static_cast<char16_t>(operand(0))) {
                next = 1;
                continue;
            }
            break;
        case 11:
            if (forward_chars() >= 1) {
                int index = operand(0);
                if (in_set(char_next(value), index)) {
                    next = 1;
                    continue;
                }
            }
            break;
        case 12:
            if (match_string(*code.strings.at(operand(0)), value)) {
                next = 1;
                continue;
            }
            break;
        case 13:
        case 525: {
            int cap = operand(0);
            if (match.matched(cap)) {
                if (!match_ref(match.index(cap), match.length(cap), value, (op & 512) != 0))
                    break;
            } else if (!(tree.options & 256))
                break;
            next = 1;
            continue;
        }
        case 0:
        case 1:
        case 2: {
            int count = operand(1);
            if (forward_chars() < count)
                break;
            int index = operand(0);
            bool success = true;
            while (count-- > 0) {
                auto c = char_next(value);
                if (op == 2   ? !in_set(c, index)
                    : op == 0 ? c != static_cast<char16_t>(index)
                              : c == static_cast<char16_t>(index)) {
                    success = false;
                    break;
                }
            }
            if (!success)
                break;
            next = 2;
            continue;
        }
        case 3:
        case 4:
        case 5:
        case 43:
        case 44:
        case 45: {
            int count = std::min(operand(1), forward_chars()), remaining = count, index = operand(0);
            for (; remaining > 0; --remaining) {
                auto c = char_next(value);
                bool contains = (op == 5 || op == 45)   ? in_set(c, index)
                                : (op == 3 || op == 43) ? c == static_cast<char16_t>(index)
                                                        : c != static_cast<char16_t>(index);
                if (!contains) {
                    backward();
                    break;
                }
            }
            if (count > remaining && (op == 3 || op == 4 || op == 5))
                track_push({count - remaining - 1, textpos - bump()});
            next = 2;
            continue;
        }
        case 131:
        case 132:
        case 133: {
            tp += 2;
            int count = track_peek(), position = track_peek(1);
            textpos = position;
            if (count > 0)
                track_push({count - 1, position - bump()});
            next = 2;
            continue;
        }
        case 6:
        case 7:
        case 8: {
            int count = std::min(operand(1), forward_chars());
            if (count > 0)
                track_push({count - 1, textpos});
            next = 2;
            continue;
        }
        case 134:
        case 135:
        case 136: {
            tp += 2;
            int position = track_peek(1);
            textpos = position;
            int index = operand(0);
            auto c = char_next(value);
            bool contains = op == 136   ? in_set(c, index)
                            : op == 134 ? c == static_cast<char16_t>(index)
                                        : c != static_cast<char16_t>(index);
            if (contains) {
                int count = track_peek();
                if (count > 0)
                    track_push({count - 1, position + bump()});
                next = 2;
                continue;
            }
            break;
        }
        case 46: {
            auto& starting = at(track, static_cast<int>(track.size()) - 1);
            if (starting < textpos)
                starting = textpos;
            next = 0;
            continue;
        }
        default:
            break;
        }
        backtrack();
    }
}
DotnetRegexScanResult DotnetRegexInterpreter::Impl::scan(DotnetRegexNodeText value, int beginning, int length,
                                                         int start, int previous, DotnetRegexRunnerMode mode,
                                                         std::int64_t timeout_ticks,
                                                         DotnetRegexTickCount tick_count) {
    if (!value)
        null_reference();
    if (static_cast<unsigned>(start) > value->size())
        throw DotnetRegexClassError(
            "System.ArgumentOutOfRangeException", 0x80131502u,
            "Start index cannot be less than 0 or greater than input length. (Parameter 'startat')");
    if (static_cast<unsigned>(length) > value->size())
        throw DotnetRegexClassError(
            "System.ArgumentOutOfRangeException", 0x80131502u,
            "Length cannot be less than 0 or exceed input length. (Parameter 'length')");
    dotnet_regex_validate_timeout(timeout_ticks);
    input = value;
    clock = tick_count ? std::move(tick_count) : DotnetRegexTickCount(ticks_now);
    check_timeout = timeout_ticks != -10000;
    if (check_timeout) {
        timeout = static_cast<int>(static_cast<double>(timeout_ticks) / 10000.0 + 0.5);
        deadline = clock() + timeout;
    }
    if (beginning < 0 || static_cast<std::size_t>(beginning) > value->size() ||
        static_cast<std::size_t>(length) > value->size() - beginning)
        throw DotnetRegexClassError(
            "System.ArgumentOutOfRangeException", 0x80131502u,
            "Specified argument was out of the range of valid values. (Parameter 'start')");
    auto text = std::u16string_view(*value).substr(beginning, length);
    textstart = start - beginning;
    textend = length;
    textpos = textstart;
    match.reset();
    if (crawl.empty()) {
        track.resize(std::max(32, code.track_count * 8));
        marks.resize(std::max(16, code.track_count * 8));
        crawl.resize(32);
    }
    tp = static_cast<int>(track.size());
    sp = static_cast<int>(marks.size());
    cp = static_cast<int>(crawl.size());
    bool backwards = (tree.options & 64) != 0;
    DotnetRegexScanResult result;
    if (previous == 0) {
        if (textstart == (backwards ? 0 : length)) {
            input.reset();
            return result;
        }
        textpos += backwards ? -1 : 1;
    }
    while (backwards ? find.try_find_right_to_left(text, textpos, textstart)
                     : find.try_find_left_to_right(text, textpos, textstart)) {
        check();
        if (try_match(text) || textpos == (backwards ? 0 : length))
            break;
        tp = static_cast<int>(track.size());
        sp = static_cast<int>(marks.size());
        cp = static_cast<int>(crawl.size());
        textpos += backwards ? -1 : 1;
    }
    if (!match.found()) {
        input.reset();
        return result;
    }
    result.success = true;
    if (mode == DotnetRegexRunnerMode::ExistenceRequired) {
        input.reset();
        return result;
    }
    result.index = match.matches[0][0] + beginning;
    result.length = match.matches[0][1];
    result.text_position = textpos + beginning;
    if (mode == DotnetRegexRunnerMode::FullMatchRequired) {
        result.input = value;
        match.tidy_balancing();
        for (int slot = 0; slot < tree.capture_count; ++slot) {
            DotnetRegexGroup group;
            group.number = slot;
            if (tree.capture_number_sparse_mapping)
                for (auto& [key, index] : *tree.capture_number_sparse_mapping)
                    if (index == slot) {
                        group.number = key;
                        break;
                    }
            group.name = dotnet_regex_group_name_from_number(tree.capture_number_sparse_mapping,
                                                             tree.capture_names, tree.capture_count, slot);
            group.success = match.count(slot) != 0;
            for (int i = 0; i < match.count(slot); ++i)
                group.captures.push_back(
                    {match.matches[slot][i * 2] + beginning, match.matches[slot][i * 2 + 1]});
            if (group.success) {
                group.index = group.captures.back().index;
                group.length = group.captures.back().length;
            }
            result.groups.push_back(std::move(group));
        }
    }
    input.reset();
    return result;
}
DotnetRegexInterpreter::DotnetRegexInterpreter(DotnetRegexParsedPattern tree, DotnetRegexNodeText pattern,
                                               const DotnetRegexExecutionStack& stack)
    : impl_(std::make_unique<Impl>(std::move(tree), std::move(pattern), stack)) {
}
DotnetRegexInterpreter::~DotnetRegexInterpreter() = default;
DotnetRegexInterpreter::DotnetRegexInterpreter(DotnetRegexInterpreter&&) noexcept = default;
DotnetRegexInterpreter& DotnetRegexInterpreter::operator=(DotnetRegexInterpreter&&) noexcept = default;
DotnetRegexScanResult DotnetRegexInterpreter::scan(DotnetRegexNodeText input, int beginning, int length,
                                                   int start, int previous, DotnetRegexRunnerMode mode,
                                                   std::int64_t timeout, DotnetRegexTickCount clock) {
    try {
        return impl_->scan(std::move(input), beginning, length, start, previous, mode, timeout,
                           std::move(clock));
    } catch (...) {
        impl_->input.reset();
        throw;
    }
}

bool dotnet_regex_is_match(std::string_view text, std::string_view pattern, std::int64_t timeout_ticks) {
    const auto expression = std::make_shared<const std::u16string>(utf16_replacing(pattern));
    const auto input = std::make_shared<const std::u16string>(utf16_replacing(text));
    const auto culture = [] { return std::u16string{}; };
    DotnetRegexInterpreter regex(
        dotnet_regex_parse(expression, 0, culture, sufficient_execution_stack),
        expression, sufficient_execution_stack);
    return regex.scan(input, 0, static_cast<std::int32_t>(input->size()), 0, -1,
                      DotnetRegexRunnerMode::ExistenceRequired, timeout_ticks).success;
}
}
