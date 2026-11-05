// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include <limits>
#include <string_view>

namespace opensim::framework {
template <class UInt>
bool try_parse_unsigned_integer(std::string_view input, UInt& result) {
    result = 0;
    const auto is_whitespace = [](char character) {
        return character == ' ' || (character >= '\t' && character <= '\r');
    };
    while (!input.empty() && is_whitespace(input.front()))
        input.remove_prefix(1);

    bool negative = false;
    if (!input.empty() && (input.front() == '+' || input.front() == '-')) {
        negative = input.front() == '-';
        input.remove_prefix(1);
    }

    bool has_digits = false;
    UInt parsed_value = 0;
    while (!input.empty() && input.front() >= '0' && input.front() <= '9') {
        const auto digit = static_cast<UInt>(input.front() - '0');
        if (parsed_value > (std::numeric_limits<UInt>::max() - digit) / 10)
            return false;
        has_digits = true;
        parsed_value = parsed_value * 10 + digit;
        input.remove_prefix(1);
    }

    while (!input.empty() && is_whitespace(input.front()))
        input.remove_prefix(1);
    while (!input.empty() && input.front() == '\0')
        input.remove_prefix(1);
    if (!has_digits || !input.empty() || (negative && parsed_value != 0))
        return false;

    result = parsed_value;
    return true;
}
}
