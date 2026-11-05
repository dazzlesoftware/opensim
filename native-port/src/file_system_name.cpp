// Copyright (c) Contributors, http://opensimulator.org/
// NFA and translation ported from dotnet/runtime v8.0.31 FileSystemName.cs.
// .NET Foundation, MIT license; see third_party/dotnet_collation/LICENSE.
#include "opensim/framework/file_system_name.hpp"
#include "opensim/framework/file_system_name_case_data.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <vector>
namespace opensim::framework::file_system_name {
namespace {
template <class Table> std::uint32_t map(std::uint32_t c, const Table& table) {
    auto it = std::lower_bound(table.begin(), table.end(), c,
                               [](auto pair, auto value) { return pair[0] < value; });
    return it != table.end() && (*it)[0] == c ? (*it)[1] : c;
}
char16_t upper_unit(char16_t c, CasingMode mode) {
    using namespace file_system_name_case_data;
    return static_cast<char16_t>(mode == CasingMode::Normal ? map(c, chars_normal) : map(c, chars_invariant));
}
std::uint32_t upper_scalar(std::uint32_t c, CasingMode mode) {
    using namespace file_system_name_case_data;
    return mode == CasingMode::Normal ? map(c, ordinal_normal) : map(c, ordinal_invariant);
}
std::uint32_t next(std::u16string_view text, std::size_t& i) {
    std::uint32_t c = text[i++];
    if (c >= 0xd800 && c <= 0xdbff && i < text.size() && text[i] >= 0xdc00 && text[i] <= 0xdfff)
        c = 0x10000 + ((c - 0xd800) << 10) + (text[i++] - 0xdc00);
    return c;
}
bool ordinal_equal(std::u16string_view a, std::u16string_view b, CasingMode mode) {
    if (a.size() != b.size())
        return false;
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size())
        if (upper_scalar(next(a, i), mode) != upper_scalar(next(b, j), mode))
            return false;
    return i == a.size() && j == b.size();
}
bool ends_with(std::u16string_view text, std::u16string_view suffix, bool ignore, CasingMode mode) {
    if (suffix.size() > text.size())
        return false;
    auto tail = text.substr(text.size() - suffix.size());
    return ignore ? ordinal_equal(tail, suffix, mode) : tail == suffix;
}
bool equal_units(char16_t a, char16_t b, const CasingBackend& casing) {
    auto left = casing.upper_invariant(a);
    auto right = casing.upper_invariant(b);
    return left == right;
}
int plus(int a, int b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
int twice(int n) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(n) * 2);
}
struct Text {
    std::u16string_view value;
    int Length;
    explicit Text(std::u16string_view v) : value(v), Length(0) {
        if (v.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::length_error("Source span length exceeds Int32");
        Length = static_cast<int>(v.size());
    }
    char16_t operator[](int i) const {
        return value.at(static_cast<std::size_t>(i));
    }
    Text Slice(int i) const {
        return Text(value.substr(static_cast<std::size_t>(i)));
    }
    bool ContainsAny(std::u16string_view choices) const {
        return value.find_first_of(choices) != value.npos;
    }
    bool ContainsAny(char16_t a, char16_t b) const {
        for (auto c : value)
            if (c == a || c == b)
                return true;
        return false;
    }
};
bool match_pattern(Text expression, Text name, bool ignoreCase, bool useExtendedWildcards,
                   const CasingBackend& casing) {

    if (expression.Length == 0 || name.Length == 0)
        return false;

    if (expression[0] == u'*') {

        if (expression.Length == 1)
            return true;

        Text expressionEnd = expression.Slice(1);

        bool hasWildcards = useExtendedWildcards ? expressionEnd.ContainsAny(u"\"<>*?")
                                                 : expressionEnd.ContainsAny(u'*', u'?');
        if (!hasWildcards) {

            if (name.Length < expressionEnd.Length)
                return false;

            return ignoreCase ? casing.ordinal_ignore_case_ends_with(name.value, expressionEnd.value)
                              : ends_with(name.value, expressionEnd.value, false, CasingMode::Normal);
        }
    }

    int nameOffset = 0;
    int expressionOffset;

    int priorMatch;
    int currentMatch;
    int priorMatchCount;
    int matchCount = 1;

    char16_t nameChar = u'\0';
    char16_t expressionChar;

    std::vector<int> currentMatches(16);
    std::vector<int> priorMatches(16);
    priorMatches[0] = 0;

    int maxState = twice(expression.Length);
    int currentState;
    bool nameFinished = false;

    while (!nameFinished) {
        if (nameOffset < name.Length) {

            nameChar = name[nameOffset++];
        } else {

            if (priorMatches[matchCount - 1] == maxState)
                break;

            nameFinished = true;
        }

        priorMatch = 0;
        currentMatch = 0;
        priorMatchCount = 0;

        while (priorMatch < matchCount) {

            expressionOffset = plus(priorMatches[priorMatch++], 1) / 2;

            while (expressionOffset < expression.Length) {
                currentState = twice(expressionOffset);
                expressionChar = expression[expressionOffset];

                if (currentMatch >= static_cast<int>(currentMatches.size()) - 2) {
                    int newSize = twice(static_cast<int>(currentMatches.size()));
                    currentMatches.resize(newSize);
                    priorMatches.resize(newSize);
                }

                if (expressionChar == u'*') {

                    goto MatchZeroOrMore;
                } else if (useExtendedWildcards && expressionChar == u'<') {

                    bool notLastPeriod = false;
                    if (!nameFinished && nameChar == u'.') {
                        for (int offset = nameOffset; offset < name.Length; offset++) {
                            if (name[offset] == u'.') {
                                notLastPeriod = true;
                                break;
                            }
                        }
                    }

                    if (nameFinished || nameChar != u'.' || notLastPeriod) {
                        goto MatchZeroOrMore;
                    } else {

                        goto MatchZero;
                    }
                } else {

                    currentState = plus(currentState, 2);

                    if (useExtendedWildcards && expressionChar == u'>') {

                        if (nameFinished || nameChar == u'.')
                            goto NextExpressionCharacter;

                        currentMatches[currentMatch++] = currentState;
                        goto ExpressionFinished;
                    } else if (useExtendedWildcards && expressionChar == u'"') {

                        if (nameFinished) {
                            goto NextExpressionCharacter;
                        } else if (nameChar == u'.') {
                            currentMatches[currentMatch++] = currentState;
                        }
                        goto ExpressionFinished;
                    } else {
                        if (expressionChar == u'\\') {

                            if (++expressionOffset == expression.Length) {
                                currentMatches[currentMatch++] = maxState;
                                goto ExpressionFinished;
                            }

                            currentState = plus(twice(expressionOffset), 2);
                            expressionChar = expression[expressionOffset];
                        }

                        if (nameFinished)
                            goto ExpressionFinished;

                        if (expressionChar == u'?') {

                            currentMatches[currentMatch++] = currentState;
                        } else if (ignoreCase ? equal_units(expressionChar, nameChar, casing)
                                              : expressionChar == nameChar) {

                            currentMatches[currentMatch++] = currentState;
                        }

                        goto ExpressionFinished;
                    }
                }

            MatchZeroOrMore:
                currentMatches[currentMatch++] = currentState;
            MatchZero:
                currentMatches[currentMatch++] = plus(currentState, 1);
            NextExpressionCharacter:
                if (++expressionOffset == expression.Length)
                    currentMatches[currentMatch++] = maxState;
            }

        ExpressionFinished:

            if ((priorMatch < matchCount) && (priorMatchCount < currentMatch)) {
                while (priorMatchCount < currentMatch) {
                    int previousLength = static_cast<int>(priorMatches.size());
                    while ((priorMatch < previousLength) &&
                           (priorMatches[priorMatch] < currentMatches[priorMatchCount])) {
                        priorMatch++;
                    }
                    priorMatchCount++;
                }
            }
        }

        if (currentMatch == 0)
            return false;

        std::swap(priorMatches, currentMatches);

        matchCount = currentMatch;
    }

    currentState = priorMatches[matchCount - 1];

    return currentState == maxState;
}
}
const CasingBackend& pinned_case_backend(CasingMode mode) {
    static const CasingBackend normal{
        [](char16_t c) { return upper_unit(c, CasingMode::Normal); },
        [](auto text, auto suffix) { return ends_with(text, suffix, true, CasingMode::Normal); }};
    static const CasingBackend invariant{
        [](char16_t c) { return upper_unit(c, CasingMode::GlobalizationInvariant); },
        [](auto text, auto suffix) {
            return ends_with(text, suffix, true, CasingMode::GlobalizationInvariant);
        }};
    return mode == CasingMode::Normal ? normal : invariant;
}
String star_literal() {
    static const auto value = std::make_shared<const std::u16string>(u"*");
    return value;
}
String translate_win32_expression(const String& expression) {
    if (!expression || expression->empty() || *expression == u"*" || *expression == u"*.*")
        return star_literal();
    bool modified = false;
    std::u16string out;
    out.reserve(expression->size());
    for (std::size_t i = 0; i < expression->size(); ++i) {
        char16_t c = (*expression)[i];
        switch (c) {
        case u'.':
            modified = true;
            if (i >= 1 && i == expression->size() - 1 && (*expression)[i - 1] == u'*')
                out.back() = u'<';
            else if (i < expression->size() - 1 &&
                     ((*expression)[i + 1] == u'?' || (*expression)[i + 1] == u'*'))
                out += u'"';
            else
                out += u'.';
            break;
        case u'?':
            modified = true;
            out += u'>';
            break;
        default:
            out += c;
            break;
        }
    }
    return modified ? std::make_shared<const std::u16string>(std::move(out)) : expression;
}
bool matches_win32_expression(std::u16string_view expression, std::u16string_view name, bool ignore,
                              const CasingBackend& casing) {
    return match_pattern(Text(expression), Text(name), ignore, true, casing);
}
bool matches_simple_expression(std::u16string_view expression, std::u16string_view name, bool ignore,
                               const CasingBackend& casing) {
    return match_pattern(Text(expression), Text(name), ignore, false, casing);
}
bool matches_win32_expression(std::u16string_view expression, std::u16string_view name, bool ignore,
                              CasingMode mode) {
    return matches_win32_expression(expression, name, ignore, pinned_case_backend(mode));
}
bool matches_simple_expression(std::u16string_view expression, std::u16string_view name, bool ignore,
                               CasingMode mode) {
    return matches_simple_expression(expression, name, ignore, pinned_case_backend(mode));
}
}
