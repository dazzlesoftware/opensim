// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/invariant_number_format.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace opensim::framework {
namespace {
[[noreturn]] void invalid_format() {
    throw std::runtime_error("FormatException: Format specifier was invalid.");
}

struct Format {
    char code = 0;
    int precision = -1;
    std::string_view text;
};

Format parse_format(std::string_view text) {
    text = text.substr(0, text.find('\0'));
    if (text.empty())
        return {'G', -1, text};
    const auto first = text.front();
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z')))
        return {0, -1, text};
    int precision = -1;
    if (text.size() > 1) {
        precision = 0;
        for (const auto c : text.substr(1)) {
            if (c < '0' || c > '9')
                return {0, -1, text};
            if (precision > (999999999 - (c - '0')) / 10)
                invalid_format();
            precision = precision * 10 + (c - '0');
        }
    }
    return {first, precision, text};
}

char upper(char c) {
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

std::string decimal(std::uint64_t value) {
    std::array<char, 32> bytes{};
    const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
    return {bytes.data(), result.ptr};
}

std::string converted(double value, std::chars_format mode, int precision = -1) {
    // The longest finite fixed Double has 309 integer digits. Precision is the
    // public CLR limit; allocation failures remain resource failures, not a
    // silently substituted format or a reduced-precision result.
    std::string buffer(static_cast<std::size_t>(std::max(precision, 0)) + 768, '\0');
    const auto result = precision < 0
                            ? std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, mode)
                            : std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, mode, precision);
    if (result.ec != std::errc{})
        throw std::runtime_error("Invariant numeric digit conversion failed");
    buffer.resize(static_cast<std::size_t>(result.ptr - buffer.data()));
    return buffer;
}

std::string grouped(std::string value) {
    auto end = value.find('.');
    if (end == std::string::npos)
        end = value.size();
    for (auto at = end; at > 3;) {
        at -= 3;
        value.insert(at, 1, ',');
    }
    return value;
}

std::string exponent_text(int exponent, char marker, bool positive_sign, int width) {
    auto digits = decimal(static_cast<std::uint64_t>(exponent < 0 ? -exponent : exponent));
    if (digits.size() < static_cast<std::size_t>(width))
        digits.insert(0, static_cast<std::size_t>(width) - digits.size(), '0');
    return std::string(1, marker) + (exponent < 0 ? "-" : positive_sign ? "+" : "") + digits;
}

struct Number {
    std::string digits;
    int scale = 0;
    bool negative = false;
};

Number floating_number(double value, int precision) {
    auto scientific = converted(std::abs(value), std::chars_format::scientific, precision - 1);
    const auto marker = scientific.find('e');
    Number number;
    number.negative = std::signbit(value);
    number.scale = std::stoi(scientific.substr(marker + 1)) + 1;
    number.digits = scientific.substr(0, marker);
    std::erase(number.digits, '.');
    while (!number.digits.empty() && number.digits.back() == '0')
        number.digits.pop_back();
    if (number.digits.empty())
        number.scale = 0;
    return number;
}

Number shortest_number(double value) {
    auto scientific = converted(std::abs(value), std::chars_format::scientific);
    const auto marker = scientific.find('e');
    Number number;
    number.negative = std::signbit(value);
    number.scale = std::stoi(scientific.substr(marker + 1)) + 1;
    number.digits = scientific.substr(0, marker);
    std::erase(number.digits, '.');
    while (!number.digits.empty() && number.digits.back() == '0')
        number.digits.pop_back();
    if (number.digits.empty())
        number.scale = 0;
    return number;
}

void round_custom(Number &number, int position) {
    if (position < 0) {
        number.digits.clear();
        number.scale = 0;
        return;
    }
    auto count = static_cast<std::size_t>(position);
    if (count < number.digits.size() && number.digits[count] >= '5') {
        number.digits.resize(count);
        while (!number.digits.empty() && number.digits.back() == '9')
            number.digits.pop_back();
        if (number.digits.empty()) {
            number.digits = "1";
            ++number.scale;
        } else
            ++number.digits.back();
    } else if (count < number.digits.size())
        number.digits.resize(count);
    while (!number.digits.empty() && number.digits.back() == '0')
        number.digits.pop_back();
    if (number.digits.empty())
        number.scale = 0;
}

std::string general(Number number, int precision, char marker) {
    std::string result = number.negative ? "-" : "";
    if (number.digits.empty())
        return result + "0";
    if (number.scale > precision || number.scale < -3) {
        result += number.digits.front();
        if (number.digits.size() > 1)
            result += "." + number.digits.substr(1);
        return result + exponent_text(number.scale - 1, marker, true, 2);
    }
    if (number.scale <= 0)
        return result + "0." + std::string(static_cast<std::size_t>(-number.scale), '0') + number.digits;
    if (number.scale >= static_cast<int>(number.digits.size()))
        return result + number.digits + std::string(static_cast<std::size_t>(number.scale) - number.digits.size(), '0');
    return result + number.digits.substr(0, number.scale) + "." + number.digits.substr(number.scale);
}

enum class Kind { Literal, Zero, Hash, Point, Comma, Percent, PerMille, Exponent };

struct Token {
    Kind kind = Kind::Literal;
    std::u16string text;
    bool positive_sign = false;
    int width = 0;
};

std::vector<std::u16string> sections(std::u16string_view text) {
    std::vector<std::u16string> result(1);
    char16_t quote = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = text[i];
        if (c == u'\\') {
            result.back() += c;
            if (++i < text.size())
                result.back() += text[i];
        } else if (quote) {
            result.back() += c;
            if (c == quote)
                quote = 0;
        } else if (c == u'\'' || c == u'"') {
            quote = c;
            result.back() += c;
        } else if (c == u';') {
            if (result.size() == 3)
                break;
            result.emplace_back();
        } else
            result.back() += c;
    }
    return result;
}

std::vector<Token> tokenize(std::u16string_view text) {
    std::vector<Token> tokens;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = text[i];
        if (c == u'\\') {
            if (++i < text.size())
                tokens.push_back({Kind::Literal, std::u16string(1, text[i])});
        } else if (c == u'\'' || c == u'"') {
            std::u16string literal;
            while (++i < text.size() && text[i] != c)
                literal += text[i];
            tokens.push_back({Kind::Literal, std::move(literal)});
        } else if (c == u'E' || c == u'e') {
            auto next = i + 1;
            bool sign = false;
            if (next < text.size() && (text[next] == u'+' || text[next] == u'-'))
                sign = text[next++] == u'+';
            const auto start = next;
            while (next < text.size() && text[next] == u'0')
                ++next;
            if (next != start) {
                tokens.push_back(
                    {Kind::Exponent, std::u16string(text.substr(i, next - i)), sign, static_cast<int>(next - start)});
                i = next - 1;
            } else
                tokens.push_back({Kind::Literal, std::u16string(1, c)});
        } else {
            const auto kind = c == u'0'        ? Kind::Zero
                              : c == u'#'      ? Kind::Hash
                              : c == u'.'      ? Kind::Point
                              : c == u','      ? Kind::Comma
                              : c == u'%'      ? Kind::Percent
                              : c == u'\u2030' ? Kind::PerMille
                                               : Kind::Literal;
            tokens.push_back({kind, std::u16string(1, c)});
        }
    }
    return tokens;
}

std::u16string custom(Number original, std::u16string_view format) {
    const auto choices = sections(format);
    auto section = original.digits.empty() ? (choices.size() >= 3 && !choices[2].empty() ? 2u : 0u)
                   : original.negative && choices.size() >= 2 && !choices[1].empty() ? 1u
                                                                                     : 0u;
    bool rounded_zero = false;
    for (int pass = 0; pass < 2; ++pass) {
        const auto tokens = tokenize(choices[section]);
        int integer_count = 0, fraction_count = 0, minimum_integer = 0, minimum_fraction = 0;
        int adjustment = 0, last_integer_token = -1;
        bool point = false, scientific = false, grouping = false;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const auto kind = tokens[i].kind;
            if (kind == Kind::Point)
                point = true;
            else if (kind == Kind::Zero || kind == Kind::Hash) {
                if (!point) {
                    ++integer_count;
                    last_integer_token = static_cast<int>(i);
                    if (kind == Kind::Zero && !minimum_integer)
                        minimum_integer = integer_count;
                } else {
                    ++fraction_count;
                    if (kind == Kind::Zero)
                        minimum_fraction = fraction_count;
                }
            } else if (kind == Kind::Percent)
                adjustment += 2;
            else if (kind == Kind::PerMille)
                adjustment += 3;
            else if (kind == Kind::Exponent)
                scientific = true;
        }
        if (minimum_integer)
            minimum_integer = integer_count - minimum_integer + 1;
        point = false;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].kind == Kind::Point)
                point = true;
            if (!point && tokens[i].kind == Kind::Comma) {
                if (static_cast<int>(i) > last_integer_token)
                    adjustment -= 3;
                else
                    grouping = true;
            }
        }
        auto number = original;
        if (rounded_zero) {
            number.digits.clear();
            number.scale = 0;
        }
        if (!number.digits.empty())
            number.scale += adjustment;
        round_custom(number, scientific ? integer_count + fraction_count : number.scale + fraction_count);
        if (number.digits.empty()) {
            const auto zero_section = choices.size() >= 3 && !choices[2].empty() ? 2u : 0u;
            if (section != zero_section) {
                section = zero_section;
                rounded_zero = true;
                continue;
            }
        }
        const int exponent = number.digits.empty() ? 0 : number.scale - integer_count;
        if (scientific && !number.digits.empty())
            number.scale = integer_count;
        std::string integer;
        if (number.scale > 0 && !number.digits.empty()) {
            integer = number.digits.substr(0, static_cast<std::size_t>(number.scale));
            integer.resize(static_cast<std::size_t>(number.scale), '0');
        }
        if (integer.size() < static_cast<std::size_t>(minimum_integer))
            integer.insert(0, static_cast<std::size_t>(minimum_integer) - integer.size(), '0');
        auto fractions = minimum_fraction;
        for (int i = 0; i < fraction_count; ++i) {
            const auto digit = number.scale + i;
            if (digit >= 0 && digit < static_cast<int>(number.digits.size()) && number.digits[digit] != '0')
                fractions = std::max(fractions, i + 1);
        }
        std::u16string result;
        if (original.negative && section == 0 && (!number.digits.empty() || minimum_integer || minimum_fraction))
            result = u"-";
        int integer_index = 0, fraction_index = 0;
        bool decimal_seen = false, integer_started = false, exponent_seen = false;
        const auto emit_integer = [&](std::u16string &output, char digit, std::size_t remaining) {
            output += digit;
            if (grouping && remaining && remaining % 3 == 0)
                output += ',';
        };
        for (const auto &token : tokens) {
            if (token.kind == Kind::Zero || token.kind == Kind::Hash) {
                if (!decimal_seen) {
                    const int offset = static_cast<int>(integer.size()) - integer_count;
                    if (!integer_started) {
                        for (int i = 0; i < offset; ++i)
                            emit_integer(result, integer[i], integer.size() - static_cast<std::size_t>(i) - 1);
                        integer_started = true;
                    }
                    const auto digit = offset + integer_index++;
                    if (digit >= 0 && digit < static_cast<int>(integer.size()))
                        emit_integer(result, integer[digit], integer.size() - static_cast<std::size_t>(digit) - 1);
                } else {
                    if (fraction_index < fractions) {
                        const auto digit = number.scale + fraction_index;
                        result +=
                            digit >= 0 && digit < static_cast<int>(number.digits.size()) ? number.digits[digit] : '0';
                    }
                    ++fraction_index;
                }
            } else if (token.kind == Kind::Point) {
                if (!integer_started && integer_count == 0) {
                    result += utf16_replacing(grouping ? grouped(integer) : integer);
                    integer_started = true;
                }
                if (!decimal_seen && fractions)
                    result += '.';
                decimal_seen = true;
            } else if (token.kind == Kind::Exponent) {
                if (exponent_seen)
                    result += token.text;
                else {
                    result += utf16_replacing(exponent_text(exponent, static_cast<char>(token.text.front()),
                                                            token.positive_sign, std::min(token.width, 10)));
                    exponent_seen = true;
                }
            } else if (token.kind != Kind::Comma)
                result += token.text;
        }
        return result;
    }
    throw std::logic_error("Custom numeric section selection failed");
}

std::string standard_double(double value, const Format &format) {
    const auto code = upper(format.code);
    const bool negative = std::signbit(value);
    const auto magnitude = std::abs(value);
    if (code == 'G' || code == 'R') {
        const auto precision = code == 'R' ? -1 : format.precision;
        const auto number = precision <= 0 ? shortest_number(value) : floating_number(value, precision);
        return general(number, precision <= 0 ? 17 : precision, format.code >= 'a' ? 'e' : 'E');
    }
    if (code == 'E') {
        auto text = converted(magnitude, std::chars_format::scientific, format.precision < 0 ? 6 : format.precision);
        const auto marker = text.find('e');
        return (negative ? "-" : "") + text.substr(0, marker) +
               exponent_text(std::stoi(text.substr(marker + 1)), format.code, true, 3);
    }
    if (code == 'F' || code == 'N' || code == 'C' || code == 'P') {
        const auto precision = format.precision < 0 ? 2 : format.precision;
        // Percent scaling is decimal, so do not multiply a finite IEEE value
        // into infinity before converting its digits.
        auto text = converted(magnitude, std::chars_format::fixed, code == 'P' ? precision + 2 : precision);
        if (code == 'P') {
            std::erase(text, '.');
            if (text.size() < static_cast<std::size_t>(precision + 1))
                text.insert(0, static_cast<std::size_t>(precision + 1) - text.size(), '0');
            if (precision)
                text.insert(text.size() - precision, 1, '.');
            while (text.size() > 1 && text.front() == '0' && text[1] != '.')
                text.erase(0, 1);
        }
        if (code != 'F')
            text = grouped(text);
        if (code == 'C')
            return negative ? "(\xc2\xa4" + text + ")" : "\xc2\xa4" + text;
        return (negative ? "-" : "") + text + (code == 'P' ? " %" : "");
    }
    invalid_format();
}
} // namespace

std::string format_invariant_double(double value, std::string_view text) {
    // CLR nonfinite symbols are emitted before validating a numeric format.
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return std::signbit(value) ? "-Infinity" : "Infinity";
    const auto format = parse_format(text);
    return format.code ? standard_double(value, format)
                       : utf8(custom(floating_number(value, 15), utf16_replacing(format.text)));
}

std::string format_invariant_integer(std::int32_t value, std::string_view text) {
    const auto format = parse_format(text);
    const auto magnitude = value < 0 ? static_cast<std::uint32_t>(0u - static_cast<std::uint32_t>(value))
                                     : static_cast<std::uint32_t>(value);
    if (!format.code)
        return utf8(custom(
            Number{value ? decimal(magnitude) : "", value ? static_cast<int>(decimal(magnitude).size()) : 0, value < 0},
            utf16_replacing(format.text)));
    const auto code = upper(format.code);
    if (code == 'D' || code == 'X' || code == 'B' || (code == 'G' && format.precision <= 0)) {
        const auto bits = code == 'D' || code == 'G' ? magnitude : static_cast<std::uint32_t>(value);
        std::array<char, 64> buffer{};
        const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), bits,
                                             code == 'X'   ? 16
                                             : code == 'B' ? 2
                                                           : 10);
        std::string result(buffer.data(), converted.ptr);
        if (format.code == 'X')
            for (auto &c : result)
                if (c >= 'a' && c <= 'f')
                    c -= 'a' - 'A';
        if (format.precision > static_cast<int>(result.size()))
            result.insert(0, static_cast<std::size_t>(format.precision) - result.size(), '0');
        if (value < 0 && (code == 'D' || code == 'G'))
            result.insert(0, 1, '-');
        return result;
    }
    if (code == 'R')
        return (value < 0 ? "-" : "") + decimal(magnitude);
    if (code == 'G' || code == 'E') {
        Number number{value ? decimal(magnitude) : "", value ? static_cast<int>(decimal(magnitude).size()) : 0,
                      value < 0};
        const auto precision = code == 'G' ? format.precision : (format.precision < 0 ? 6 : format.precision) + 1;
        round_custom(number, precision);
        if (code == 'G')
            return general(number, precision, format.code >= 'a' ? 'e' : 'E');
        auto digits = number.digits.empty() ? std::string("0") : number.digits;
        digits.resize(static_cast<std::size_t>(precision), '0');
        if (precision > 1)
            digits.insert(1, 1, '.');
        return (value < 0 ? "-" : "") + digits +
               exponent_text(number.digits.empty() ? 0 : number.scale - 1, format.code, true, 3);
    }
    return standard_double(value, format);
}

std::u16string format_invariant_double_utf16(double value, std::u16string_view text) {
    if (std::isnan(value))
        return u"NaN";
    if (std::isinf(value))
        return std::signbit(value) ? u"-Infinity" : u"Infinity";
    text = text.substr(0, text.find(char16_t{}));
    // A standard specifier contains ASCII only. Classification can use its
    // UTF-8 spelling, but custom literals must retain the original code units.
    const auto encoded = utf8(text);
    const auto format = parse_format(encoded);
    return format.code ? utf16_replacing(standard_double(value, format)) : custom(floating_number(value, 15), text);
}

std::u16string format_invariant_integer_utf16(std::int32_t value, std::u16string_view text) {
    text = text.substr(0, text.find(char16_t{}));
    const auto encoded = utf8(text);
    const auto format = parse_format(encoded);
    if (format.code)
        return utf16_replacing(format_invariant_integer(value, encoded));
    const auto magnitude = value < 0 ? static_cast<std::uint32_t>(0u - static_cast<std::uint32_t>(value))
                                     : static_cast<std::uint32_t>(value);
    return custom(
        Number{value ? decimal(magnitude) : "", value ? static_cast<int>(decimal(magnitude).size()) : 0, value < 0},
        text);
}
} // namespace opensim::framework
