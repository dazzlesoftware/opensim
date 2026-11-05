#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/config_file_uri_object.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>
using namespace opensim::framework;
ConfigUriText decode(std::string s) {
    if (s == "~")
        return {};
    if (s == "-")
        return runtime_empty_text();
    std::u16string v;
    for (size_t i = 0; i < s.size(); i += 4)
        v += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return std::make_shared<const std::u16string>(v);
}
std::string hex(std::u16string_view value) {
    std::string s;
    for (auto c : value)
        for (int b = 12; b >= 0; b -= 4)
            s += "0123456789abcdef"[(c >> b) & 15];
    return s.empty() ? "-" : s;
}
std::string error(const ConfigUriComponentsError& e) {
    auto range = dynamic_cast<const ConfigFileUriRangeError*>(&e);
    return "E:" + e.source_type + ":" + std::to_string(static_cast<std::int32_t>(e.hresult)) + ":" +
           hex(e.message) + ":" + (e.parameter ? hex(*e.parameter) : "~") + ":" +
           (range && range->actual_value ? std::to_string(*range->actual_value) : "~");
}

int main(int argc, char**) {
    auto backend = native_config_uri_components_backend();
    bool windows =
#ifdef _WIN32
        true;
#else
        false;
#endif
    if (argc == 1) {
        auto uri = config_generic_uri_create(std::make_shared<const std::u16string>(u"ws://HOST:81/a?q"),
                                             ConfigUriObjectKind::Absolute, backend, windows);
        if (*config_generic_uri_component(uri, ConfigGenericUriComponent::Host) != u"host" ||
            config_generic_uri_port(uri) != 81 ||
            *config_generic_uri_component(uri, ConfigGenericUriComponent::Query) != u"?q")
            return 1;
        return 0;
    }
    std::string raw;
    while (std::getline(std::cin, raw)) {
        std::unordered_map<const void*, int> ids;
        std::vector<ConfigUriText> roots;
        auto input = decode(raw);
        auto render = [&](ConfigUriText t) {
            roots.push_back(t);
            auto [it, added] = ids.emplace(t.get(), int(ids.size() + 1));
            return std::to_string(it->second) + "," + hex(*t);
        };
        render(input);
        try {
            auto uri = config_generic_uri_create(input, ConfigUriObjectKind::Absolute, backend, windows);
            std::cout << "A";
            for (char c : std::string("phiduaqotalfsphiduaq")) {
                std::cout << "|" << c << ":";
                try {
                    if (c == 'p') {
                        std::cout << config_generic_uri_port(uri);
                        continue;
                    }
                    ConfigUriText value;
                    switch (c) {
                    case 'h':
                        value = config_generic_uri_component(uri, ConfigGenericUriComponent::Host);
                        break;
                    case 'i':
                        value = config_generic_uri_component(uri, ConfigGenericUriComponent::IdnHost);
                        break;
                    case 'd':
                        value = config_generic_uri_component(uri, ConfigGenericUriComponent::DnsSafeHost);
                        break;
                    case 'u':
                        value = config_generic_uri_component(uri, ConfigGenericUriComponent::UserInfo);
                        break;
                    case 'a':
                        value = config_generic_uri_component(uri, ConfigGenericUriComponent::AbsolutePath);
                        break;
                    case 'q':
                        value = config_generic_uri_component(uri, ConfigGenericUriComponent::Query);
                        break;
                    case 'o':
                        value = uri->original_string();
                        break;
                    case 't':
                        value = uri->to_string();
                        break;
                    case 'l':
                        value = uri->local_path();
                        break;
                    case 'f':
                        value = uri->fragment();
                        break;
                    default:
                        value = uri->scheme();
                    }
                    std::cout << render(value);
                } catch (const ConfigUriComponentsError& e) {
                    std::cout << error(e);
                }
            }
        } catch (const ConfigUriComponentsError& e) {
            std::cout << error(e);
        } catch (const std::exception& e) {
            std::cout << "NATIVE:" << e.what();
        }
        std::cout << "\n";
    }
}
