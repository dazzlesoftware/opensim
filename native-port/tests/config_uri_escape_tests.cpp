#include "opensim/framework/config_uri_escape.hpp"
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
    auto safe = std::make_shared<const std::u16string>(u"abc-._~");
    if (config_uri_escape(safe, true, ConfigUriEscapeSet::Unreserved) != safe)
        throw std::runtime_error("Escape identity regression");
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int set, existing;
        std::string text;
        in >> set >> existing >> text;
        ConfigUriText s;
        if (text != "~") {
            std::u16string v;
            if (text != "-")
                for (size_t i = 0; i < text.size(); i += 4)
                    v += static_cast<char16_t>(std::stoul(text.substr(i, 4), nullptr, 16));
            s = std::make_shared<const std::u16string>(v);
        }
        try {
            auto r = config_uri_escape(s, existing != 0, static_cast<ConfigUriEscapeSet>(set));
            std::cout << "S:" << (r == s ? 1 : 0) << ":" << encode(*r) << '\n';
        } catch (const ConfigUriEscapeError& e) {
            std::cout << "E:" << e.source_type << ":" << e.hresult << ":" << e.what() << '\n';
        }
    }
}
