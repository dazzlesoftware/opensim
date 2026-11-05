#include "opensim/framework/config_file_uri_object.hpp"
#include <iostream>
#include <sstream>
#include <map>
using namespace opensim::framework;
std::string encode(std::u16string_view s) {
    std::string out;
    for (auto c : s)
        for (int b = 12; b >= 0; b -= 4)
            out += "0123456789abcdef"[(c >> b) & 15];
    return out;
}
int main() {
    auto backend = native_config_uri_components_backend();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int attempt, kind;
        std::string raw;
        in >> attempt >> kind >> raw;
        ConfigUriText input;
        if (raw != "~") {
            std::u16string v;
            for (size_t i = 0; i < raw.size(); i += 4)
                v += static_cast<char16_t>(std::stoul(raw.substr(i, 4), nullptr, 16));
            input = std::make_shared<const std::u16string>(v);
        }
        std::map<const void*, int> ids;
        std::vector<ConfigUriText> roots;
        int next = 1;
        auto render = [&](ConfigUriText t) {
            roots.push_back(t);
            auto [it, added] = ids.emplace(t.get(), next);
            if (added)
                ++next;
            return std::to_string(it->second) + "," + encode(*t);
        };
        if (input)
            render(input);
        try {
            auto uri =
                attempt
                    ? config_file_uri_try_create(input, static_cast<ConfigUriObjectKind>(kind), backend, true)
                    : config_file_uri_create(input, static_cast<ConfigUriObjectKind>(kind), backend, true);
            if (!uri) {
                std::cout << "N\n";
                continue;
            }
            std::cout << (uri->is_absolute() ? "A" : "R");
            auto get = [&](char c) {
                try {
                    ConfigUriText text;
                    switch (c) {
                    case 'o':
                        text = uri->original_string();
                        break;
                    case 't':
                        text = uri->to_string();
                        break;
                    case 'a':
                        text = uri->absolute_uri();
                        break;
                    case 'l':
                        text = uri->local_path();
                        break;
                    case 'f':
                        text = uri->fragment();
                        break;
                    default:
                        text = uri->scheme();
                    }
                    std::cout << "|" << c << ":" << render(text);
                } catch (const ConfigUriComponentsError& e) {
                    std::cout << "|" << c << ":E" << e.source_type << "," << e.hresult << ","
                              << encode(e.message) << "," << (e.parameter ? encode(*e.parameter) : "-")
                              << ",";
                    auto range = dynamic_cast<const ConfigFileUriRangeError*>(&e);
                    if (range && range->actual_value)
                        std::cout << *range->actual_value;
                    else
                        std::cout << "-";
                }
            };
            for (char c : std::string("otalfssflatolato"))
                get(c);
            std::cout << '\n';
        } catch (const ConfigUriComponentsError& e) {
            std::cout << "E" << e.source_type << "," << e.hresult << "," << encode(e.message) << ","
                      << (e.parameter ? encode(*e.parameter) : "-") << '\n';
        } catch (const std::exception& e) {
            std::cout << "X" << e.what() << '\n';
        }
    }
}
