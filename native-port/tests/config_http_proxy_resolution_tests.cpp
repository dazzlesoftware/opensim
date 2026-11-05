#include "opensim/framework/config_http_proxy_resolution.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include <iostream>
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
struct Proxy : ConfigHttpProxy {
    std::string mode;
    ConfigUriObjectPtr value;
    mutable int bypass = 0, get = 0;
    bool is_bypassed(ConfigUriObjectPtr) const override {
        ++bypass;
        if (mode == "provider")
            throw ConfigHttpProxyProviderError("missing dependency");
        if (mode == "bthrow")
            throw std::runtime_error("bypass");
        return mode == "bypass";
    }
    ConfigUriObjectPtr get_proxy(ConfigUriObjectPtr) const override {
        ++get;
        if (mode == "gthrow")
            throw std::runtime_error("get");
        return mode == "null" ? nullptr : value;
    }
    std::shared_ptr<ConfigXmlCredentials> credentials() const override {
        return {};
    }
    void credentials(std::shared_ptr<ConfigXmlCredentials>) override {
    }
};
int main(int argc, char**) {
    if (argc > 1) {
        auto p = std::make_shared<Proxy>();
        p->mode = "provider";
        try {
            config_http_resolve_proxy({}, p);
            return 1;
        } catch (const ConfigHttpProxyProviderError&) {
            return 0;
        }
    }
    auto uris = native_config_uri_object_backend();
    auto target =
        uris->create(nini_writer_text(u"http://destination.example:8123/"), ConfigUriObjectKind::Absolute);
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream in(row);
        std::string mode, text;
        in >> mode >> text;
        auto p = std::make_shared<Proxy>();
        p->mode = mode;
        try {
            p->value = uris->create(nini_writer_text(decode(text)), ConfigUriObjectKind::RelativeOrAbsolute);
            auto result = config_http_resolve_proxy(target, mode == "none" ? nullptr : p);
            auto endpoint = config_http_proxy_endpoint(result ? result : target);
            std::cout << "OK:" << hex(*endpoint.host) << ":" << endpoint.port;
        } catch (const ConfigUriComponentsError& e) {
            std::cout << "E:" << e.source_type << ":" << static_cast<std::int32_t>(e.hresult) << ":"
                      << hex(e.message);
        }
        std::cout << ":" << p->bypass << ":" << p->get << "\n";
    }
}
