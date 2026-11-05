#include "opensim/framework/config_http_uri_object.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
ConfigUriText d(std::string text) {
    if (text == "~")
        return {};
    std::u16string s;
    if (text != "-")
        for (std::size_t i = 0; i < text.size(); i += 4)
            s += char16_t(std::stoul(text.substr(i, 4), nullptr, 16));
    return std::make_shared<const std::u16string>(s);
}
std::string h(std::u16string_view s) {
    if (s.empty())
        return "-";
    std::ostringstream o;
    o << std::hex << std::setfill('0');
    for (auto c : s)
        o << std::setw(4) << unsigned(c);
    return o.str();
}
int main(int argc, char**) {
    auto backend = native_http_uri_object_backend();
    if (argc == 1) {
        auto base = backend.create(d("0068007400740070003a002f002f0078002f"), ConfigUriObjectKind::Absolute);
        bool exact = false;
        try {
            backend.combine_uri(base, {});
        } catch (const ConfigUriComponentsError& e) {
            exact = e.source_type == "System.NullReferenceException" && e.hresult == 0x80004003;
        }
        if (!exact || !backend.combine_string(base, {}))
            return 1;
        return 0;
    }
    for (std::string line; std::getline(std::cin, line);) {
        std::istringstream row(line);
        std::string mode, value;
        row >> mode >> value;
        try {
            auto text = d(value);
            auto base =
                text ? backend.create(text, ConfigUriObjectKind::RelativeOrAbsolute) : ConfigUriObjectPtr{};
            auto out = mode == "N" ? backend.combine_uri(base, {}) : backend.combine_string(base, {});
            std::cout << h(*out->original_string()) << '/' << h(*out->absolute_uri()) << '/'
                      << h(*out->to_string()) << '\n';
        } catch (const ConfigUriComponentsError& e) {
            std::cout << e.source_type << ':' << std::hex << e.hresult << ':' << h(e.message) << ':'
                      << (e.parameter ? h(*e.parameter) : "~") << std::dec << '\n';
        }
    }
}
