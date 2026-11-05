// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/field_string.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::framework {
std::string field_to_string(std::span<const std::uint8_t> bytes, std::optional<std::string_view> name) {
    if (bytes.empty())
        return {};
    bool printable = std::all_of(bytes.begin(), bytes.end(), [](auto b) {
        return (b >= 0x20 && b <= 0x7e) || b == 9 || b == 13 || b == 10 || b == 0;
    });
    if (!name)
        throw std::invalid_argument("Null field name");
    std::string output;
    auto prefix = [&] {
        if (!name->empty()) {
            output.append(*name);
            output += ": ";
        }
    };
    if (printable) {
        prefix();
        auto count = bytes.size() - 1;
        std::size_t end = 0;
        while (end < count && bytes[end] && bytes[end] != 13 && bytes[end] != 10)
            ++end;
        output.append(reinterpret_cast<const char*>(bytes.data()), end);
        return output;
    }
    constexpr char digits[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < bytes.size(); i += 16) {
        if (i) {
#ifdef _WIN32
            output += "\r\n";
#else
            output += '\n';
#endif
        }
        prefix();
        for (std::size_t j = 0; j < 16; ++j) {
            if (i + j < bytes.size()) {
                auto b = bytes[i + j];
                output += digits[b >> 4];
                output += digits[b & 15];
                output += ' ';
            } else
                output += "   ";
        }
        for (std::size_t j = 0; j < 16 && i + j < bytes.size(); ++j) {
            auto b = bytes[i + j];
            output += b >= 0x20 && b < 0x7e ? static_cast<char>(b) : '.';
        }
    }
    return output;
}
}
