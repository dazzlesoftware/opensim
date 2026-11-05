// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/vector_text.hpp"
#include <iostream>
#include <bit>
#include <iomanip>
std::u16string Text(const std::string& s) {
    std::u16string out;
    if (s == "-" || s == ".")
        return out;
    for (std::size_t i = 0; i < s.size(); i += 4)
        out += static_cast<char16_t>(std::stoi(s.substr(i, 2), nullptr, 16) |
                                     (std::stoi(s.substr(i + 2, 2), nullptr, 16) << 8));
    return out;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            opensim::framework::Vector3 value;
            bool ok = opensim::framework::try_parse_sdk_vector3(Text(line), value);
            std::cout << (ok ? 1 : 0);
            for (float number : {value.x, value.y, value.z})
                std::cout << ":" << std::hex << std::setw(8) << std::setfill('0')
                          << std::bit_cast<std::uint32_t>(number) << std::dec;
            std::cout << "\n";
        } catch (const std::exception& e) {
            std::cout << "EX:" << e.what() << "\n";
        }
    }
}
