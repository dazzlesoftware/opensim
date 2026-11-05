#include "opensim/framework/config_uri_unescape.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
int main() {
    auto require = [](bool value) {
        if (!value)
            throw std::runtime_error("URI unescape native regression");
    };
    require(config_uri_unescape(u"%41%2F%7F", 0xffff, 0xffff, 0xffff, 3, true, false) == u"A%2F%7F");
    require(config_uri_unescape(u"%F0%90%80%80", 0xffff, 0xffff, 0xffff, 10, true, false) == u"\U00010000");
    require(config_uri_unescape(u"%E1%80%41", 0xffff, 0xffff, 0xffff, 10, true, false) == u"%E1%80A");
    bool threw = false;
    try {
        config_uri_unescape(u"%zz", 0xffff, 0xffff, 0xffff, 26, true, false);
    } catch (const ConfigUriUnescapeError&) {
        threw = true;
    }
    require(threw);
    std::string line;
    char16_t sets[][3] = {{0xffff, 0xffff, 0xffff}, {u'?', u'#', 0xffff}, {u'%', u'/', u':'}, {0, 127, 128}};
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, syntax, query, set;
        std::string hex;
        in >> mode >> syntax >> query >> set >> hex;
        std::u16string s;
        for (size_t i = 0; i < hex.size(); i += 4)
            s += static_cast<char16_t>(std::stoul(hex.substr(i, 4), nullptr, 16));
        try {
            auto r = config_uri_unescape(s, sets[set][0], sets[set][1], sets[set][2], mode, syntax != 0,
                                         query != 0);
            std::string out;
            for (auto c : r)
                for (int b = 12; b >= 0; b -= 4)
                    out += "0123456789abcdef"[(c >> b) & 15];
            std::cout << "S" << out << '\n';
        } catch (const ConfigUriUnescapeError& e) {
            std::cout << "E" << e.what() << '\n';
        }
    }
}
