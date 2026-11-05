#include "opensim/framework/config_http_windows_proxy_bypass.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <map>
#include <algorithm>
using namespace opensim::framework;
ConfigUriText decode(std::string s) {
    if (s == "null")
        return {};
    if (s == "-")
        return nini_writer_text(u"");
    std::u16string t;
    for (std::size_t i = 0; i < s.size(); i += 4)
        t += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return nini_writer_text(std::move(t));
}
std::string hex(std::u16string_view s) {
    if (s.empty())
        return "-";
    std::string t;
    for (auto c : s)
        for (int n = 12; n >= 0; n -= 4)
            t += "0123456789abcdef"[(c >> n) & 15];
    return t;
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--addresses") {
        std::vector<std::string> addresses;
        for (auto& ip : native_config_http_windows_local_addresses())
            addresses.push_back(ip.str());
        std::sort(addresses.begin(), addresses.end());
        for (auto& ip : addresses)
            std::cout << ip << "\n";
        return 0;
    }
    auto uris = native_config_uri_object_backend();
    auto backend = native_config_http_windows_proxy_bypass_backend();
    backend.local_addresses = [] {
        std::vector<IpAddress> result;
        for (auto s : {"192.0.2.10", "2001:db8::10", "fe80::1%7"})
            result.push_back(*IpAddress::parse(std::string(s)));
        return result;
    };
    std::map<std::string, std::shared_ptr<ConfigHttpWindowsProxyBypass>> cache;
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream in(row);
        std::string mode, a, b;
        in >> mode >> a >> b;
        try {
            if (mode == "W") {
                auto input = decode(a), pattern = decode(b);
                std::cout << config_http_windows_proxy_star_match(input ? *input : std::u16string_view{},
                                                                  pattern ? *pattern : std::u16string_view{},
                                                                  backend.casing);
            } else {
                auto uri = a == "null" ? ConfigUriObjectPtr{}
                                       : uris->create(decode(a), ConfigUriObjectKind::RelativeOrAbsolute);
                if (mode == "I") {
                    auto value = native_config_uri_network_identity(uri);
                    std::cout << value.host_name_type << ":" << value.loopback;
                } else {
                    auto& rules = cache[b];
                    if (!rules)
                        rules = std::make_shared<ConfigHttpWindowsProxyBypass>(decode(b), backend);
                    bool bypass = rules->bypasses(uri);
                    if (!bypass) {
                        if (!uri)
                            throw ConfigUriComponentsError(
                                "System.NullReferenceException",
                                u"Object reference not set to an instance of an object.", 0x80004003);
                        uri->scheme();
                    }
                    std::cout << bypass << ":" << rules->bypass_local() << ":";
                    for (auto& pattern : rules->patterns())
                        std::cout << hex(*pattern) << ",";
                }
            }
        } catch (const ConfigUriComponentsError& e) {
            std::cout << "E:" << e.source_type << ":" << static_cast<std::int32_t>(e.hresult) << ":"
                      << hex(e.message);
        }
        std::cout << "\n";
    }
}
