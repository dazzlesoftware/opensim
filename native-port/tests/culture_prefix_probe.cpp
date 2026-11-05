#include "opensim/framework/culture_prefix.hpp"
#include <iostream>
#include <string>
std::string decode(std::string_view value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); i += 2)
        out += static_cast<char>(std::stoi(std::string(value.substr(i, 2)), nullptr, 16));
    return out;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        auto at = line.find('|');
        try {
            std::cout << (opensim::framework::invariant_starts_with(decode(line.substr(0, at)),
                                                                    decode(line.substr(at + 1)))
                              ? "1"
                              : "0")
                      << "\n";
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return 1;
        }
    }
}
