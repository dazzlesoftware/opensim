// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/http_range.hpp"
#include <iostream>
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::optional<std::string> value;
        if (line != "N") {
            value = "";
            for (size_t i = 0; i < line.size(); i += 2)
                *value += char(std::stoi(line.substr(i, 2), nullptr, 16));
        }
        try {
            int32_t start = 9, end = 9;
            bool ok = opensim::server::try_parse_http_range(value, start, end);
            std::cout << (ok ? "1" : "0") << '|' << start << '|' << end << '\n';
        } catch (...) {
            std::cout << "EX\n";
        }
    }
}
