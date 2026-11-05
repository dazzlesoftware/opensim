#include "opensim/framework/nini_xml_character.hpp"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <string>
using namespace opensim::framework;
void require(bool value) {
    if (!value)
        throw std::runtime_error("XML character kernel unit failed");
}
int main(int argc, char**) {
    if (argc == 1) {
        require(nini_xml_character_properties(u'a') == 254);
        require(nini_xml_parse_ncname(u"a:b") == 1);
        require(nini_xml_parse_name(u"a:b") == 3);
        require(nini_xml_invalid_character_data(u"\xd800\xdc00") == -1);
        require(nini_xml_invalid_character_data(u"x\xd800") == 1);
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::size_t offset = 0;
        auto separator = line.find(' ');
        if (separator != std::string::npos) {
            offset = std::stoul(line.substr(0, separator));
            line.erase(0, separator + 1);
        }
        std::u16string s;
        if (line != "-")
            for (std::size_t i = 0; i < line.size(); i += 4)
                s.push_back(static_cast<char16_t>(std::stoul(line.substr(i, 4), nullptr, 16)));
        std::size_t colon = 0;
        auto q = nini_xml_parse_qname(s, offset, colon);
        std::cout << nini_xml_invalid_whitespace(s) << ' ' << nini_xml_invalid_character_data(s) << ' '
                  << nini_xml_invalid_public_id(s) << ' ' << nini_xml_parse_ncname(s, offset) << ' '
                  << nini_xml_parse_name(s, offset) << ' ' << nini_xml_parse_nmtoken(s, offset) << ' '
                  << nini_xml_parse_nmtoken(s, offset, false) << ' ' << q << ' ' << colon;
        if (s.size() == 1)
            std::cout << ' ' << static_cast<unsigned>(nini_xml_character_properties(s[0]));
        std::cout << '\n';
    }
}
