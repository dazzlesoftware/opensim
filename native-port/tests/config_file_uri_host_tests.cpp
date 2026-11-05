#include "opensim/framework/config_file_uri_host.hpp"
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
        std::size_t start;
        std::uint64_t flags;
        std::string text;
        in >> start >> flags >> text;
        std::u16string s;
        if (text != "-")
            for (size_t i = 0; i < text.size(); i += 4)
                s += static_cast<char16_t>(std::stoul(text.substr(i, 4), nullptr, 16));
        auto r = config_file_uri_check_host(s, start, s.size(), flags, unicode);
        std::cout << r.end << ":" << r.flags << ":" << r.error << ":"
                  << (r.rewritten_prefix ? encode(*r.rewritten_prefix) : "-") << '\n';
    }
}
