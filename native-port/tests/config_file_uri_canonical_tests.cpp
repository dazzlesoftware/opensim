#include "opensim/framework/config_file_uri_canonical.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int flags, delimiter;
        std::string text;
        in >> flags >> delimiter >> text;
        std::u16string s;
        if (text != "-")
            for (size_t i = 0; i < text.size(); i += 4)
                s += static_cast<char16_t>(std::stoul(text.substr(i, 4), nullptr, 16));
        auto r = config_file_uri_check_canonical(s, 0, s.size(), static_cast<char16_t>(delimiter),
                                                 (flags & 1) != 0, (flags & 2) != 0);
        std::cout << r.check << ":" << r.position << '\n';
    }
}
