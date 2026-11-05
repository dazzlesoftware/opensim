#include "opensim/framework/config_file_uri_path.hpp"
#include <iostream>
using namespace opensim::framework;
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        auto mode = line[0] - '0';
        std::u16string s;
        for (size_t i = 2; i < line.size(); i += 4)
            s += static_cast<char16_t>(std::stoul(line.substr(i, 4), nullptr, 16));
        if (mode >= 6) {
            auto scan = config_file_uri_scan_scheme(s, true);
            std::string raw;
            for (auto c : s)
                for (int b = 12; b >= 0; b -= 4)
                    raw += "0123456789abcdef"[(c >> b) & 15];
            std::cout << (config_file_uri_implicit_kind_is_relative(scan, s, mode - 6, true) ? "R:" : "P:")
                      << raw << "\n";
            continue;
        }
        if (mode >= 5) {
            auto v = config_file_uri_scan_scheme(s, true);
            std::string name;
            for (auto c : v.scheme)
                for (int b = 12; b >= 0; b -= 4)
                    name += "0123456789abcdef"[(c >> b) & 15];
            std::cout << v.error << ":" << v.flags << ":" << name << "\n";
            continue;
        }
        auto r = mode >= 3 ? config_file_uri_unescape_dots(std::move(s), mode == 3)
                           : config_file_uri_compress(std::move(s), mode == 0, mode != 2);
        std::string out;
        for (auto c : r)
            for (int b = 12; b >= 0; b -= 4)
                out += "0123456789abcdef"[(c >> b) & 15];
        std::cout << out << '\n';
    }
}
