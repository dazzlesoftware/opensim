// Portions derived from .NET Runtime v8.0.31 Number.Parsing.cs, MIT license:
// third_party/dotnet_collation/LICENSE.
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <limits>
#include <string_view>
#include <type_traits>
namespace opensim::framework {
InvariantNumberError::InvariantNumberError(Kind k, std::u16string text)
    : runtime_error(utf8(text)), kind(k), message(std::move(text)) {
}
namespace {
bool space(char16_t c) {
    return c == u' ' || (c >= u'\t' && c <= u'\r');
}
[[noreturn]] void format(const std::u16string& value) {
    throw InvariantNumberError(InvariantNumberError::Kind::Format,
                               u"The input string '" + value + u"' was not in a correct format.");
}
template <class T> T integer(const std::optional<std::u16string>& value) {
    if (!value)
        return 0;
    std::u16string_view input = *value;
    while (!input.empty() && space(input.front()))
        input.remove_prefix(1);
    bool negative = false;
    if (!input.empty() && (input.front() == u'+' || input.front() == u'-')) {
        negative = input.front() == u'-';
        input.remove_prefix(1);
    }
    using U = std::make_unsigned_t<T>;
    const U limit = static_cast<U>(std::numeric_limits<T>::max()) + (negative ? U(1) : U(0));
    U number = 0;
    bool digits = false, overflow = false;
    while (!input.empty() && input.front() >= u'0' && input.front() <= u'9') {
        U digit = static_cast<U>(input.front() - u'0');
        digits = true;
        if (number > (limit - digit) / 10)
            overflow = true;
        else if (!overflow)
            number = number * 10 + digit;
        input.remove_prefix(1);
    }
    while (!input.empty() && space(input.front()))
        input.remove_prefix(1);
    while (!input.empty() && input.front() == 0)
        input.remove_prefix(1);
    if (!digits || !input.empty())
        format(*value);
    if (overflow)
        throw InvariantNumberError(InvariantNumberError::Kind::Overflow,
                                   sizeof(T) == 4 ? u"Value was either too large or too small for an Int32."
                                                  : u"Value was either too large or too small for an Int64.");
    return std::bit_cast<T>(negative ? U(0) - number : number);
}
bool equal_ascii(std::u16string_view value, std::u16string_view lower) {
    if (value.size() != lower.size())
        return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto c = value[i];
        if (c >= u'A' && c <= u'Z')
            c += 32;
        if (c != lower[i])
            return false;
    }
    return true;
}
template <class T> T real(const std::optional<std::u16string>& value) {
    if (!value)
        return T(0);
    std::u16string_view input = *value;
    while (!input.empty() && space(input.front()))
        input.remove_prefix(1);
    bool negative = false;
    if (!input.empty() && (input.front() == u'+' || input.front() == u'-')) {
        negative = input.front() == u'-';
        input.remove_prefix(1);
    }
    std::string normalized;
    std::size_t at = 0, digits = 0, before = 0, first_nonzero = std::string::npos;
    auto digit = [&](char16_t c) {
        if (c != u'0' && first_nonzero == std::string::npos)
            first_nonzero = digits;
        ++digits;
        normalized += static_cast<char>(c);
    };
    while (at < input.size()) {
        auto c = input[at];
        if (c >= u'0' && c <= u'9') {
            digit(c);
            ++before;
            ++at;
        } else if (c == u',' && digits)
            ++at;
        else
            break;
    }
    if (at < input.size() && input[at] == u'.') {
        normalized += '.';
        ++at;
        while (at < input.size() && input[at] >= u'0' && input[at] <= u'9')
            digit(input[at++]);
    }
    bool valid = digits != 0;
    std::int64_t exponent = 0;
    if (at < input.size() && (input[at] == u'e' || input[at] == u'E')) {
        ++at;
        bool minus = false;
        if (at < input.size() && (input[at] == u'+' || input[at] == u'-')) {
            minus = input[at] == u'-';
            ++at;
        }
        auto start = at;
        while (at < input.size() && input[at] >= u'0' && input[at] <= u'9') {
            if (exponent <= 1000) {
                exponent = exponent * 10 + (input[at] - u'0');
                if (exponent > 1000)
                    exponent = 9999;
            }
            ++at;
        }
        if (at == start)
            valid = false;
        if (minus)
            exponent = -exponent;
        normalized += 'e';
        normalized += std::to_string(exponent);
    }
    while (at < input.size() && space(input[at]))
        ++at;
    while (at < input.size() && input[at] == 0)
        ++at;
    if (valid && at == input.size()) {
        if (first_nonzero == std::string::npos)
            return negative ? -T(0) : T(0);
        T result = 0;
        // Parse directly to T. Parsing Single via Double changes midpoint rounding.
        auto parsed = std::from_chars(normalized.data(), normalized.data() + normalized.size(), result,
                                      std::chars_format::general);
        if (parsed.ec == std::errc::result_out_of_range) {
            auto power =
                exponent + static_cast<std::int64_t>(before) -
                static_cast<std::int64_t>(first_nonzero == std::string::npos ? digits : first_nonzero) - 1;
            result = power >= 0 ? std::numeric_limits<T>::infinity() : T(0);
        } else if (parsed.ec != std::errc{} || parsed.ptr != normalized.data() + normalized.size())
            format(*value);
        return negative ? -result : result;
    }
    // Special-symbol fallback uses String.Trim (Unicode whitespace), while the
    // ordinary number grammar above accepts only the source ASCII whitespace.
    std::u16string_view special = *value;
    while (!special.empty() && unicode_whitespace(special.front()))
        special.remove_prefix(1);
    while (!special.empty() && unicode_whitespace(special.back()))
        special.remove_suffix(1);
    bool special_negative = false;
    if (!special.empty() && (special.front() == u'+' || special.front() == u'-')) {
        special_negative = special.front() == u'-';
        special.remove_prefix(1);
    }
    if (equal_ascii(special, u"infinity"))
        return special_negative ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
    if (equal_ascii(special, u"nan")) {
        if constexpr (sizeof(T) == 4)
            return std::bit_cast<float>(std::uint32_t(0xffc00000));
        else
            return std::bit_cast<double>(std::uint64_t(0xfff8000000000000));
    }
    format(*value);
}
}
std::int32_t invariant_convert_to_int32(const std::optional<std::u16string>& v) {
    return integer<std::int32_t>(v);
}
std::int64_t invariant_convert_to_int64(const std::optional<std::u16string>& v) {
    return integer<std::int64_t>(v);
}
float invariant_convert_to_single(const std::optional<std::u16string>& v) {
    return real<float>(v);
}
double invariant_convert_to_double(const std::optional<std::u16string>& v) {
    return real<double>(v);
}
}
