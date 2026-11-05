/*

  Copyright (c) Opensimulator, http://opensimulator.org/ 
  All rights reserved.

  Copyright (c) 2006-2016, openmetaverse.co
  All rights reserved.
 
  - Redistribution and use in source and binary forms, with or without 
    modification, are permitted provided that the following conditions are met:
 
  - Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
  - Neither the name of the openmetaverse.co nor the names 
    of its contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.
 
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
  POSSIBILITY OF SUCH DAMAGE.
*/
// SDK vector text behavior; see protocol/LIBOPENMETAVERSE-LICENSE.txt.
#include "opensim/framework/vector3d_text.hpp"
#include "opensim/framework/text.hpp"
#include <charconv>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <bit>
#include <cstdint>
namespace opensim::framework {
namespace {
std::string number(double value) {
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return std::signbit(value) ? "-\xe2\x88\x9e" : "\xe2\x88\x9e";
    char bytes[128];
    auto result = std::to_chars(bytes, bytes + sizeof(bytes), value, std::chars_format::scientific);
    if (result.ec != std::errc{})
        throw std::runtime_error("Double text encoding failed");
    std::string scientific(bytes, result.ptr);
    const auto separator = scientific.find('e');
    const auto exponent = std::stoi(scientific.substr(separator + 1));
    if (exponent >= -4 && exponent < 17) {
        // .NET first chooses the shortest round-tripping significant digits,
        // then expands them to fixed notation. to_chars(fixed) instead prefers
        // exact integer digits when both strings have equal length.
        auto digits = scientific.substr(0, separator);
        std::string sign;
        if (digits.front() == '-') {
            sign = "-";
            digits.erase(0, 1);
        }
        std::erase(digits, '.');
        const auto point = exponent + 1;
        if (point <= 0)
            return sign + "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
        if (static_cast<std::size_t>(point) >= digits.size())
            return sign + digits + std::string(static_cast<std::size_t>(point) - digits.size(), '0');
        return sign + digits.substr(0, point) + "." + digits.substr(point);
    }
    scientific[separator] = 'E';
    return scientific;
}
bool floating(std::u16string_view input, double& value) {
    const auto nul = input.find(char16_t{});
    const bool has_nul = nul != std::u16string_view::npos;
    if (has_nul && !std::all_of(input.begin() + static_cast<std::ptrdiff_t>(nul), input.end(),
                                [](char16_t c) { return c == 0; }))
        return false;
    bool non_ascii_padding = false;
    while (!input.empty() && unicode_whitespace(input.front())) {
        non_ascii_padding |= input.front() > 127;
        input.remove_prefix(1);
    }
    while (!input.empty() && unicode_whitespace(input.back())) {
        non_ascii_padding |= input.back() > 127;
        input.remove_suffix(1);
    }
    if (input.empty())
        return false;
    bool negative = false;
    if (input.front() == u'-' || input.front() == u'+') {
        negative = input.front() == u'-';
        input.remove_prefix(1);
        if (input.empty())
            return false;
    }
    if (input == u"\u221e") {
        value = negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        return true;
    }
    if (input.size() == 3 && (input[0] == u'N' || input[0] == u'n') &&
        (input[1] == u'A' || input[1] == u'a') && (input[2] == u'N' || input[2] == u'n')) {
        value = std::bit_cast<double>(std::uint64_t{0xfff8000000000000});
        return true;
    }
    if (has_nul) {
        while (!input.empty() && input.back() == 0)
            input.remove_suffix(1);
        while (!input.empty() && unicode_whitespace(input.back())) {
            non_ascii_padding |= input.back() > 127;
            input.remove_suffix(1);
        }
    }
    if (non_ascii_padding)
        return false;
    std::string ascii;
    for (auto c : input) {
        if (c > 127)
            return false;
        ascii.push_back(static_cast<char>(c));
    }
    std::size_t pos = 0, digits = 0, before = 0, first_nonzero = std::string::npos;
    while (pos < ascii.size() && ascii[pos] >= '0' && ascii[pos] <= '9') {
        if (ascii[pos] != '0' && first_nonzero == std::string::npos)
            first_nonzero = digits;
        ++pos;
        ++digits;
        ++before;
    }
    if (pos < ascii.size() && ascii[pos] == '.') {
        ++pos;
        while (pos < ascii.size() && ascii[pos] >= '0' && ascii[pos] <= '9') {
            if (ascii[pos] != '0' && first_nonzero == std::string::npos)
                first_nonzero = digits;
            ++pos;
            ++digits;
        }
    }
    if (digits == 0)
        return false;
    int exponent = 0;
    if (pos < ascii.size() && (ascii[pos] == 'e' || ascii[pos] == 'E')) {
        ++pos;
        bool minus = false;
        if (pos < ascii.size() && (ascii[pos] == '+' || ascii[pos] == '-'))
            minus = ascii[pos++] == '-';
        const auto start = pos;
        while (pos < ascii.size() && ascii[pos] >= '0' && ascii[pos] <= '9') {
            exponent = std::min(100000, exponent * 10 + (ascii[pos++] - '0'));
        }
        if (pos == start)
            return false;
        if (minus)
            exponent = -exponent;
    }
    if (pos != ascii.size())
        return false;
    const auto converted =
        std::from_chars(ascii.data(), ascii.data() + ascii.size(), value, std::chars_format::general);
    if (converted.ec == std::errc::result_out_of_range) {
        const auto power = exponent + static_cast<int>(before) -
                           static_cast<int>(first_nonzero == std::string::npos ? digits : first_nonzero) - 1;
        value = power >= 0 ? std::numeric_limits<double>::infinity() : 0.0;
    } else if (converted.ec != std::errc{})
        return false;
    if (negative)
        value = -value;
    return true;
}
}
std::string vector3d_string(const Vector3d& value) {
    return "<" + number(value.x) + ", " + number(value.y) + ", " + number(value.z) + ">";
}
bool try_parse_sdk_vector3d(std::u16string_view input, Vector3d& value) {
    value = {};
    const auto size = input.size();
    if (size < 7)
        return false;
    std::size_t start = 0;
    while (start < size && input[start++] != u'<') {
    }
    if (start > size - 6)
        return false;
    auto comma = start + 1;
    while (comma < size && input[comma] != u',')
        ++comma;
    if (comma > size - 5)
        return false;
    double x = 0, y = 0, z = 0;
    if (!floating(input.substr(start, comma - start), x))
        return false;
    start = ++comma;
    while (++comma < size && input[comma] != u',') {
    }
    if (comma > size - 3)
        return false;
    if (!floating(input.substr(start, comma - start), y))
        return false;
    start = ++comma;
    while (++comma < size && input[comma] != u'>') {
    }
    if (comma >= size)
        return false;
    if (!floating(input.substr(start, comma - start), z))
        return false;
    value = {x, y, z};
    return true;
}
}
