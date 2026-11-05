// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/vector3d_text.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <bit>
#include <cstdint>
using namespace opensim::framework;
std::u16string Decode(const std::string& s) {
    std::u16string out;
    if (s == "-" || s == ".")
        return out;
    for (std::size_t i = 0; i < s.size(); i += 4)
        out += static_cast<char16_t>(std::stoi(s.substr(i, 2), nullptr, 16) |
                                     (std::stoi(s.substr(i + 2, 2), nullptr, 16) << 8));
    return out;
}
std::string Hex(std::u16string_view text) {
    std::ostringstream s;
    s << std::hex << std::setfill('0');
    for (auto c : text)
        s << std::setw(2) << (c & 255) << std::setw(2) << (c >> 8);
    return s.str();
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            std::istringstream in(line);
            char op;
            std::string a, b, c;
            in >> op >> a;
            Vector3d value;
            if (op == 'P') {
                bool ok = try_parse_sdk_vector3d(Decode(a), value);
                std::cout << (ok ? 1 : 0);
                for (double number : {value.x, value.y, value.z})
                    std::cout << ":" << std::hex << std::setw(16) << std::setfill('0')
                              << std::bit_cast<std::uint64_t>(number) << std::dec;
                std::cout << "|";
            } else {
                in >> b >> c;
                value = {std::bit_cast<double>(std::stoull(a, nullptr, 16)),
                         std::bit_cast<double>(std::stoull(b, nullptr, 16)),
                         std::bit_cast<double>(std::stoull(c, nullptr, 16))};
            }
            std::cout << Hex(utf16(vector3d_string(value))) << "\n";
        } catch (const std::exception& e) {
            std::cout << "EX:" << e.what() << "\n";
        }
    }
}
