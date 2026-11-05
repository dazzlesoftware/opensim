#include "opensim/framework/config_file_uri_state.hpp"
#include "opensim/framework/config_uri_iri.hpp"
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
        std::string text;
        in >> flags >> text;
        std::u16string input;
        for (size_t i = 0; i < text.size(); i += 4)
            input += static_cast<char16_t>(std::stoul(text.substr(i, 4), nullptr, 16));
        auto m = config_file_uri_parse_minimal(input, input, flags, true, false, unicode);
        ConfigFileUriState s;
        s.flags = m.flags;
        s.parse_string = m.parse_string;
        s.original_string = input;
        s.unix_file_syntax = m.unix_file_syntax;
        std::string status = m.error ? "P" + std::to_string(m.error) : "S";
        if (!m.error)
            try {
                config_file_uri_parse_remaining(s);
            } catch (const ConfigFileUriStateError& e) {
                status = "E" + std::string(e.what());
            }
        auto o = s.offsets;
        std::cout << status << ":" << s.flags << ":" << encode(s.parse_string) << ":"
                  << (s.info ? std::to_string(o.scheme) + "," + std::to_string(o.user) + "," +
                                   std::to_string(o.host) + "," + std::to_string(o.port) + "," +
                                   std::to_string(o.path) + "," + std::to_string(o.query) + "," +
                                   std::to_string(o.fragment) + "," + std::to_string(o.end)
                             : "N")
                  << '\n';
    }
}
