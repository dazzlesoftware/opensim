// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/ossl_format.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
#include <type_traits>

namespace opensim::script {
namespace {
template <class Character>
std::basic_string<Character> format_string(std::basic_string_view<Character> format,
                                           std::span<const std::basic_string<Character>> arguments) {
    std::basic_string<Character> output;
    std::size_t at = 0;
    const auto fail = [] { throw std::runtime_error("Input string was not in a correct format."); };
    const auto digit = [&](std::size_t position) {
        return position < format.size() && format[position] >= '0' && format[position] <= '9';
    };
    const auto spaces = [&] {
        while (at < format.size() && format[at] == ' ')
            ++at;
    };
    const auto number = [&]() {
        if (!digit(at))
            fail();
        unsigned value = static_cast<unsigned>(format[at++] - '0');
        while (digit(at) && value < 1000000)
            value = value * 10 + static_cast<unsigned>(format[at++] - '0');
        return value;
    };
    while (at < format.size()) {
        const auto ch = format[at++];
        if (ch != '{' && ch != '}') {
            output += ch;
            continue;
        }
        if (at < format.size() && format[at] == ch) {
            output += ch;
            ++at;
            continue;
        }
        if (ch == '}')
            fail();
        const auto index = number();
        spaces();
        unsigned width = 0;
        bool left = false;
        if (at < format.size() && format[at] == ',') {
            ++at;
            spaces();
            if (at < format.size() && format[at] == '-') {
                left = true;
                ++at;
            }
            width = number();
            spaces();
        }
        if (at < format.size() && format[at] == ':') {
            ++at;
            while (at < format.size() && format[at] != '}') {
                if (format[at++] == '{')
                    fail();
            }
        }
        if (at == format.size() || format[at++] != '}')
            fail();
        if (index >= arguments.size())
            throw std::runtime_error("Index must refer to an argument in the list.");
        const auto &value = arguments[index];
        const auto length = [&] {
            if constexpr (std::is_same_v<Character, char>)
                return framework::utf16_replacing(value).size();
            else
                return value.size();
        }();
        const auto padding = width > length ? width - length : 0;
        if (!left)
            output.append(padding, ' ');
        output += value;
        if (left)
            output.append(padding, ' ');
    }
    return output;
}
} // namespace

std::string ossl_format_string(std::string_view format, std::span<const std::string> arguments) {
    return format_string<char>(format, arguments);
}

std::u16string ossl_format_string_utf16(std::u16string_view format, std::span<const std::u16string> arguments) {
    return format_string<char16_t>(format, arguments);
}
} // namespace opensim::script
