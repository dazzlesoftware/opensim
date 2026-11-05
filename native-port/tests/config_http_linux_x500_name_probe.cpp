#include "opensim/framework/config_http_linux_x500_name.hpp"
#include <iostream>
#include <vector>
int main() {
    std::string line;
    constexpr char hex[] = "0123456789abcdef";
    while (std::getline(std::cin, line)) {
        std::vector<std::uint8_t> bytes;
        if (line != "_")
            for (std::size_t i = 0; i < line.size(); i += 2)
                bytes.push_back(static_cast<std::uint8_t>(std::stoul(line.substr(i, 2), nullptr, 16)));
        auto value = opensim::framework::config_http_linux_x500_name(bytes);
        if (value.empty())
            std::cout << '_';
        for (auto c : value)
            for (int shift = 12; shift >= 0; shift -= 4)
                std::cout << hex[(c >> shift) & 15];
        std::cout << '\n';
    }
}
