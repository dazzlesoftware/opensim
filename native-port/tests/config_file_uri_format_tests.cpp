#include "opensim/framework/config_file_uri_format.hpp"
#include "opensim/framework/config_file_uri_path.hpp"
#include "opensim/framework/config_uri_iri.hpp"
#include "opensim/framework/config_file_uri_resources.hpp"
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
        int format;
        std::string text;
        in >> format >> text;
        std::u16string input;
        for (size_t i = 0; i < text.size(); i += 4)
            input += static_cast<char16_t>(std::stoul(text.substr(i, 4), nullptr, 16));
        auto scan = config_file_uri_scan_scheme(input, true);
        auto flags = scan.flags;
        if (config_uri_has_iri_characters(input))
            flags |= 0x200000000ull;
        auto m = config_file_uri_parse_minimal(input, input, flags, true, scan.unix_file_syntax, unicode);
        if (scan.error || m.error) {
            auto error = scan.error ? scan.error : m.error;
            std::cout << "E" << encode(config_file_uri_error_messages[error]) << '\n';
            continue;
        }
        ConfigFileUriState state;
        state.flags = m.flags;
        state.parse_string = m.parse_string;
        state.original_string = input;
        state.unix_file_syntax = m.unix_file_syntax;
        try {
            auto result = config_file_uri_canonical_path(state, u"", format, true);
            std::cout << "S" << encode(result) << '\n';
        } catch (const ConfigFileUriStateError& e) {
            std::u16string message;
            for (auto c : std::string(e.what()))
                message += c;
            std::cout << "E" << encode(message) << '\n';
        }
    }
}
