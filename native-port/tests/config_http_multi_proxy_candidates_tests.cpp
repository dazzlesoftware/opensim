#include "opensim/framework/config_http_multi_proxy_candidates.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include <iostream>
#include "opensim/framework/nini_writer.hpp"
#include <sstream>
using namespace opensim::framework;
std::u16string decode(std::string s) {
    std::u16string t;
    for (std::size_t i = 0; i < s.size(); i += 4)
        t += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return t;
}
std::string hex(std::u16string_view s) {
    std::string t;
    for (auto c : s)
        for (int n = 12; n >= 0; n -= 4)
            t += "0123456789abcdef"[(c >> n) & 15];
    return t;
}
int main() {
    auto backend = *native_config_uri_object_backend();
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream in(row);
        int lazy, secure;
        std::string encoded;
        in >> lazy >> secure >> encoded;
        try {
            ConfigUriText text = encoded == "null" ? ConfigUriText{}
                                                   : nini_writer_text(encoded == "-" ? u"" : decode(encoded));
            auto plan = lazy ? ConfigHttpMultiProxyCandidates::lazy(text, secure, backend)
                             : ConfigHttpMultiProxyCandidates::parse(text, secure, backend);
            for (int i = 0; i < 1024; ++i) {
                auto next = plan.next();
                if (i)
                    std::cout << "|";
                std::cout << (next.found ? "1" : "0") << ":" << (next.is_final ? "1" : "0") << ":"
                          << plan.position() << ":" << (next.uri ? hex(*next.uri->absolute_uri()) : "-");
                if (!next.found)
                    break;
            }
        } catch (const ConfigUriComponentsError& e) {
            std::cout << "E:" << e.source_type << ":" << static_cast<std::int32_t>(e.hresult) << ":"
                      << hex(e.message);
        }
        std::cout << "\n";
    }
}
