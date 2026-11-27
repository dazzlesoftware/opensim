// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/sdk_utf8.hpp"
#include <charconv>
#include <iostream>
#include <string>
#include <stdexcept>

int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Expected UTF16 hex units and byte bound or -");
        const std::string text = argv[1];
        if (text.size() % 4)
            throw std::runtime_error("Malformed UTF16 unit hex");
        std::u16string input;
        for (std::size_t i = 0; i < text.size(); i += 4) {
            unsigned unit{};
            auto r = std::from_chars(text.data() + i, text.data() + i + 4, unit, 16);
            if (r.ec != std::errc{} || r.ptr != text.data() + i + 4)
                throw std::runtime_error("Malformed UTF16 unit hex");
            input += static_cast<char16_t>(unit);
        }
        std::optional<std::size_t> bound;
        if (std::string_view(argv[2]) != "-") {
            std::size_t value{};
            const std::string b = argv[2];
            auto r = std::from_chars(b.data(), b.data() + b.size(), value);
            if (r.ec != std::errc{} || r.ptr != b.data() + b.size())
                throw std::runtime_error("Malformed bound");
            bound = value;
        }
        const auto bytes = opensim::framework::sdk_utf8_no_term(input, bound);
        constexpr char hex[] = "0123456789abcdef";
        std::cout << "ok:";
        for (auto b : bytes)
            std::cout << hex[b >> 4] << hex[b & 15];
        std::cout << '\n';
        return 0;
    } catch (const std::overflow_error &) {
        std::cout << "OverflowException\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
