#include "opensim/framework/configuration_culture.hpp"
#include <iostream>
using namespace opensim::framework;
int main(int argc, char**) {
    if (argc == 1) {
        auto text = [](std::u16string s) { return std::make_shared<const std::u16string>(std::move(s)); };
        if (!startup_invariant_configuration_include_key(text(u"Include-file")) ||
            startup_invariant_configuration_include_key(text(u"include-file")) ||
            !startup_invariant_configuration_include_key(
                text(std::u16string(u"In") + char16_t(0) + u"clude-file")) ||
            startup_invariant_configuration_include_key(text(u"Include-\u0300")))
            return 1;
        try {
            startup_invariant_configuration_include_key(nullptr);
            return 1;
        } catch (const ConfigurationCultureNullError& e) {
            if (e.hresult() != 0x80004003u)
                return 1;
        }
        std::cout << "PASS: raw invariant configuration Include- prefix\n";
        return 0;
    }
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::string line;
    while (std::getline(std::cin, line)) {
        ConfigurationCultureText text;
        if (line != "null") {
            std::u16string s;
            if (line != "-")
                for (std::size_t i = 0; i < line.size(); i += 4)
                    s += char16_t(std::stoul(line.substr(i, 4), nullptr, 16));
            text = std::make_shared<const std::u16string>(std::move(s));
        }
        try {
            std::cout << startup_invariant_configuration_include_key(text) << '\n';
        } catch (const ConfigurationCultureNullError& e) {
            std::cout << e.source_type() << ":" << e.hresult() << ":" << e.what() << '\n';
        }
    }
}
