#include "opensim/framework/config_uri_iri.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
int main(int argc, char**) {
    if (argc == 1) {
        if (!config_uri_has_iri_characters(u"x%41") || config_uri_has_iri_characters(u"x%2F"))
            return 1;
        if (config_uri_escape_unescape_iri(u"%41%2F", 0) != u"A%2F")
            return 2;
        if (config_uri_escape_unescape_iri(u"\ue000", 32) != u"\ue000" ||
            config_uri_escape_unescape_iri(u"\ue000", 16) != u"%EE%80%80")
            return 3;
        return 0;
    }
    for (std::string line; std::getline(std::cin, line);) {
        std::istringstream row(line);
        std::uint32_t component;
        std::string raw;
        row >> component >> raw;
        std::u16string text;
        if (raw != "-")
            for (std::size_t i = 0; i < raw.size(); i += 4)
                text += char16_t(std::stoul(raw.substr(i, 4), nullptr, 16));
        auto result = config_uri_escape_unescape_iri(text, component);
        std::cout << (config_uri_has_iri_characters(text) ? "1/" : "0/");
        if (result.empty())
            std::cout << '-';
        else {
            std::cout << std::hex << std::setfill('0');
            for (auto c : result)
                std::cout << std::setw(4) << unsigned(c);
            std::cout << std::dec;
        }
        std::cout << '\n';
    }
}
