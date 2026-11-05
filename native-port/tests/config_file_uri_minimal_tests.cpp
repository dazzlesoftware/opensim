#include "opensim/framework/config_file_uri_minimal.hpp"
#include "opensim/framework/config_file_uri_path.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
std::string encode(std::u16string_view s) {
    std::string out;
    for (auto c : s)
        for (int b = 12; b >= 0; b -= 4)
            out += "0123456789abcdef"[(c >> b) & 15];
    return out;
}
int main() {
    auto unicode = native_config_uri_unicode_backend();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::uint64_t flags;
        int syntax;
        std::string text;
        in >> flags >> syntax >> text;
        std::u16string s;
        if (text != "-")
            for (size_t i = 0; i < text.size(); i += 4)
                s += static_cast<char16_t>(std::stoul(text.substr(i, 4), nullptr, 16));
        auto r = config_file_uri_parse_minimal(s, s, flags, true, syntax != 0, unicode);
        std::cout << r.error << ":" << r.flags << ":" << (r.unix_file_syntax ? 1 : 0) << ":"
                  << encode(r.parse_string) << '\n';
    }
}
