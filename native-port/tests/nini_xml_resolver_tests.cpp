#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/nini_xml_resolver.hpp"
#include "opensim/framework/config_http_uri_object.hpp"
#include "opensim/framework/config_file_uri_object.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include <iostream>
#include <sstream>
#include <bit>
using namespace opensim::framework;
std::string hex(std::u16string_view v) {
    std::string s;
    for (auto c : v)
        for (int b = 12; b >= 0; b -= 4)
            s += "0123456789abcdef"[(c >> b) & 15];
    return s;
}
ConfigUriText decode(const std::string& s) {
    if (s == "~")
        return {};
    if (s == "-")
        return nini_writer_text(u"");
    std::u16string v;
    for (size_t i = 0; i < s.size(); i += 4)
        v += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return nini_writer_text(std::move(v));
}
void error(std::string type, std::uint32_t hr, std::u16string_view message,
           std::optional<std::u16string> parameter) {
    std::cout << "E:" << type << ":" << std::bit_cast<std::int32_t>(hr) << ":" << hex(message) << ":"
              << (parameter ? hex(*parameter) : "~");
}
std::shared_ptr<ConfigUriObjectBackend> backend() {
    return native_config_uri_object_backend();
}
int main(int argc, char**) {
    auto operations = backend();
    if (argc == 1) {
        auto base = operations->create(nini_writer_text(u"http://example/a"), ConfigUriObjectKind::Absolute);
        if (native_nini_xml_resolve_uri(base, {}, operations) != base)
            throw std::runtime_error("Resolver must preserve base identity for null relative");
        return 0;
    }
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream in(row);
        std::string b, v;
        in >> b >> v;
        try {
            auto text = decode(b);
            auto base = text ? operations->create(text, ConfigUriObjectKind::RelativeOrAbsolute)
                             : ConfigUriObjectPtr{};
            auto uri = native_nini_xml_resolve_uri(base, decode(v), operations);
            std::cout << (uri == base ? "S" : "N") << ":" << (uri->is_absolute() ? "A" : "R");
            for (int i = 0; i < 5; ++i) {
                std::cout << "|";
                try {
                    ConfigUriText t;
                    switch (i) {
                    case 0:
                        t = uri->original_string();
                        break;
                    case 1:
                        t = uri->to_string();
                        break;
                    case 2:
                        t = uri->absolute_uri();
                        break;
                    case 3:
                        t = uri->local_path();
                        break;
                    default:
                        t = uri->scheme();
                    }
                    std::cout << hex(*t);
                } catch (const ConfigUriComponentsError& e) {
                    error(e.source_type, e.hresult, e.message, e.parameter);
                }
            }
        } catch (const ConfigUriComponentsError& e) {
            error(e.source_type, e.hresult, e.message, e.parameter);
        } catch (const NiniXmlProviderError& e) {
            error(e.source_type, e.hresult, e.message, e.parameter);
        } catch (const std::exception& e) {
            std::cout << "NATIVE:" << e.what();
        }
        std::cout << "\n";
    }
}
