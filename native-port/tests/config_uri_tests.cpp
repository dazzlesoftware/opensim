#include "opensim/framework/config_uri.hpp"
#include <iostream>
using namespace opensim::framework;
int main(int argc, char**) {
    if (argc == 1) {
        auto text = [](std::u16string s) { return std::make_shared<const std::u16string>(std::move(s)); };
        if (is_http_configuration_uri(nullptr) ||
            !is_http_configuration_uri(text(u"https://user:pass@example.test/?query#fragment")) ||
            is_http_configuration_uri(text(u"http://example.test\\path")) ||
            is_http_configuration_uri(text(std::u16string(u"http://") + char16_t(0xd800))) ||
            !is_http_configuration_uri(text(u"http://[fe80::1%raw scope]/")))
            return 1;
        std::cout << "PASS: raw configuration URI classification\n";
        return 0;
    }
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::string line;
    while (std::getline(std::cin, line)) {
        ConfigUriText value;
        if (line != "null") {
            std::u16string s;
            if (line != "-")
                for (std::size_t i = 0; i < line.size(); i += 4)
                    s += char16_t(std::stoul(line.substr(i, 4), nullptr, 16));
            value = std::make_shared<const std::u16string>(std::move(s));
        }
        std::cout << is_http_configuration_uri(value) << '\n';
    }
    return 0;
}
