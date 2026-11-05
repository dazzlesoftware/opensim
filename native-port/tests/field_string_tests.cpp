#include "opensim/framework/field_string.hpp"
#include <iostream>
#include <vector>
using namespace opensim::framework;
std::string hex(std::string_view s) {
    std::string out;
    constexpr char h[] = "0123456789abcdef";
    for (unsigned char c : s) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out;
}
std::string probe(int size, int seed, int mode, int label) {
    std::vector<std::uint8_t> bytes(size);
    for (int i = 0; i < size; ++i) {
        if (mode == 0 || mode == 1)
            bytes[i] = static_cast<std::uint8_t>(32 + (seed + i) % 95);
        else if (mode == 2)
            bytes[i] = static_cast<std::uint8_t>(seed + i * 17);
        else if (mode == 3)
            bytes[i] = i % 4 == 0 ? 0 : i % 4 == 1 ? 13 : i % 4 == 2 ? 10 : 9;
        else {
            constexpr std::uint8_t data[]{0xe9, 0x9b, 0xaa, 0x7e, 0};
            bytes[i] = data[i % 5];
        }
    }
    if (mode == 1 && size > 0)
        bytes.back() = 0;
    try {
        return hex(field_to_string(bytes, label == 0   ? std::optional<std::string_view>("")
                                          : label == 1 ? std::optional<std::string_view>("field")
                                          : label == 2 ? std::optional<std::string_view>("雪")
                                                       : std::nullopt));
    } catch (...) {
        return "ERROR";
    }
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int n, s, m, l;
            while (std::cin >> n >> s >> m >> l)
                std::cout << probe(n, s, m, l) << '\n';
            return 0;
        }
        auto require = [](bool v) {
            if (!v)
                throw std::runtime_error("FieldToString source rule");
        };
        require(field_to_string({}) == "");
        require(field_to_string(std::vector<std::uint8_t>{'A'}) == "");
        require(field_to_string(std::vector<std::uint8_t>{'A', 'B', 0}) == "AB");
        require(field_to_string(std::vector<std::uint8_t>{'A', 13, 'B', 0}) == "A");
        require(probe(0, 0, 0, 3) == "");
        require(probe(1, 0, 0, 3) == "ERROR");
        std::cout << "PASS:6 source field string checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
