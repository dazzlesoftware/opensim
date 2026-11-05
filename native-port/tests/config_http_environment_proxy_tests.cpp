#include "opensim/framework/config_http_environment_proxy_native.hpp"
#include "opensim/framework/config_http_uri_object.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/runtime_environment.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/nini_writer.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <map>
using namespace opensim::framework;
using Text = ConfigUriText;
using Object = ConfigUriObjectPtr;
Text d(std::string s) {
    if (s == "~")
        return {};
    std::u16string text;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            text += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return nini_writer_text(text);
}
std::string h(Text text) {
    if (!text)
        return "~";
    if (text->empty())
        return "-";
    std::ostringstream o;
    o << std::hex << std::setfill('0');
    for (auto c : *text)
        o << std::setw(4) << unsigned(c);
    return o.str();
}
std::string error(const ConfigUriComponentsError& e) {
    std::ostringstream o;
    o << e.source_type << ':' << std::hex << e.hresult << ':' << h(nini_writer_text(e.message)) << ':'
      << (e.parameter ? h(nini_writer_text(*e.parameter)) : "~");
    return o.str();
}
struct Credential : ConfigXmlCredentials {
    Text user, password, domain;
    bool is_default = false;
};
std::shared_ptr<Credential> default_credential() {
    static auto value = []() {
        auto c = std::make_shared<Credential>();
        c->user = c->password = c->domain = nini_writer_text(u"");
        c->is_default = true;
        return c;
    }();
    return value;
}
std::string credential(const std::shared_ptr<ConfigXmlCredentials>& value) {
    if (!value)
        return "null";
    auto n = std::dynamic_pointer_cast<ConfigHttpNetworkCredential>(value);
    if (n)
        return std::string(n->is_default ? "D" : "N") + ":" + h(n->user_name) + ":" + h(n->password) + ":" +
               h(n->domain);
    auto c = std::dynamic_pointer_cast<Credential>(value);
    if (!c)
        return "external";
    return std::string(value == default_credential() ? "D" : "N") + ":" + h(c->user) + ":" + h(c->password) +
           ":" + h(c->domain);
}
ConfigHttpEnvironmentProxyNativeOptions options() {
    return {};
}
std::string snapshot(Object uri) {
    if (!uri)
        return "null";
    auto c = uri->http_components();
    if (!c)
        return h(uri->original_string()) + "/" + h(uri->scheme()) + "/" +
               h(config_generic_uri_component(uri, ConfigGenericUriComponent::Host)) + "/" +
               std::to_string(config_generic_uri_port(uri)) + "/" +
               h(config_generic_uri_component(uri, ConfigGenericUriComponent::UserInfo)) + "/" +
               h(uri->absolute_uri()) + "/" + h(uri->to_string());
    return h(uri->original_string()) + "/" + h(uri->scheme()) + "/" + h(c->host()) + "/" +
           std::to_string(c->port()) + "/" + h(c->user_info()) + "/" + h(uri->absolute_uri()) + "/" +
           h(uri->to_string());
}
int main(int argc, char**) {
    auto uris = *native_config_uri_object_backend();
    if (argc == 1) {
        auto o = options();
        o.environment = [](Text key) -> Text {
            if (*key == u"HTTP_PROXY")
                return nini_writer_text(u"proxy:8080");
            if (*key == u"NO_PROXY")
                return nini_writer_text(u".example.com");
            return {};
        };
        auto b = native_config_http_environment_proxy_backend(o);
        auto p = ConfigHttpEnvironmentProxy::try_create(b, ConfigHttpEnvironmentProxyPlatform::Windows);
        auto input = uris.create(nini_writer_text(u"http://sub.example.com/"), ConfigUriObjectKind::Absolute);
        if (!p || !p->is_bypassed(input) || *p->get_proxy(input)->http_components()->host() != u"proxy")
            return 1;
        auto marker = std::make_shared<Credential>();
        p->credentials(marker);
        if (p->credentials() != marker)
            return 2;
        p->credentials({});
        if (p->credentials())
            return 3;
        auto socks = config_http_environment_proxy_uri(nini_writer_text(u"socks5://proxy:1080"), b);
        if (!socks || *config_generic_uri_component(socks, ConfigGenericUriComponent::Host) != u"proxy" ||
            config_generic_uri_port(socks) != 1080)
            return 4;
        auto native = native_config_http_environment_proxy_backend();
        auto target =
            config_http_environment_proxy_uri(nini_writer_text(u"domain%5Cuser:secret@host"), native);
        auto credentials = ConfigHttpEnvironmentProxyCredentials::try_create(target, {}, native);
        auto value =
            std::dynamic_pointer_cast<ConfigHttpNetworkCredential>(credentials->get_credential(target, {}));
        if (!value || *value->user_name != u"user" || *value->password != u"secret" ||
            *value->domain != u"domain" || value->is_default)
            return 5;
        auto first = config_http_default_network_credentials(),
             second = config_http_default_network_credentials();
        if (first != second || !std::dynamic_pointer_cast<ConfigHttpNetworkCredential>(first)->is_default)
            return 6;
        return 0;
    }
    for (std::string line; std::getline(std::cin, line);) {
        std::istringstream in(line);
        std::string mode, a, b, c, e;
        in >> mode;
        try {
            auto backend = native_config_http_environment_proxy_backend(options());
            if (mode == "E") {
                const char16_t* names[] = {u"http_proxy",  u"HTTP_PROXY", u"https_proxy",
                                           u"HTTPS_PROXY", u"all_proxy",  u"ALL_PROXY",
                                           u"no_proxy",    u"NO_PROXY",   u"GATEWAY_INTERFACE"};
                for (auto name : names)
                    runtime_environment_set(nini_writer_text(name), {});
                for (auto name : names) {
                    in >> a;
                    auto value = d(a);
                    if (value)
                        runtime_environment_set(nini_writer_text(name), value);
                }
                auto environment =
                    ConfigHttpEnvironmentProxy::try_create(backend,
#ifdef _WIN32
                                                           ConfigHttpEnvironmentProxyPlatform::Windows
#else
                                                           ConfigHttpEnvironmentProxyPlatform::Unix
#endif
                    );
                in >> a;
                if (!environment) {
                    std::cout << "null\n";
                    continue;
                }
                auto target = uris.create(d(a), ConfigUriObjectKind::Absolute);
                auto proxy = environment->get_proxy(target);
                std::cout << snapshot(proxy) << ":" << (environment->is_bypassed(target) ? "1" : "0") << ":"
                          << credential(
                                 environment->credentials()
                                     ? std::dynamic_pointer_cast<ConfigHttpEnvironmentProxyCredentials>(
                                           environment->credentials())
                                           ->get_credential(proxy, {})
                                     : nullptr)
                          << '\n';
                continue;
            }
            if (mode == "G") {
                in >> a;
                std::cout << (config_http_proxy_dotnet8_is_digit((*d(a))[0]) ? "1" : "0") << '\n';
                continue;
            }
            if (mode == "U") {
                in >> a;
                std::cout << snapshot(config_http_environment_proxy_uri(d(a), backend)) << '\n';
                continue;
            }
            if (mode == "Q") {
                in >> a >> b;
                auto x = d(a), y = d(b);
                std::cout << (config_http_proxy_http_uri_equal(
                                  x ? uris.create(x, ConfigUriObjectKind::RelativeOrAbsolute) : Object{},
                                  y ? uris.create(y, ConfigUriObjectKind::RelativeOrAbsolute) : Object{})
                                  ? "1"
                                  : "0")
                          << '\n';
                continue;
            }
            if (mode == "C") {
                in >> a;
                auto raw = d(a);
                backend->user_info = [raw](Object) { return raw; };
                auto target = uris.create(nini_writer_text(u"http://x/"), ConfigUriObjectKind::Absolute);
                auto v = ConfigHttpEnvironmentProxyCredentials::try_create(target, {}, backend);
                std::cout << credential(v ? v->get_credential(target, {}) : nullptr) << '\n';
                continue;
            }
            if (mode == "K") {
                in >> a >> b >> c;
                auto x = d(a), y = d(b), z = d(c);
                auto first = x ? uris.create(x, ConfigUriObjectKind::Absolute) : Object{},
                     second = y ? uris.create(y, ConfigUriObjectKind::Absolute) : Object{},
                     request = z ? uris.create(z, ConfigUriObjectKind::RelativeOrAbsolute) : Object{};
                auto v = ConfigHttpEnvironmentProxyCredentials::try_create(first, second, backend);
                std::cout << credential(v ? v->get_credential(request, {}) : nullptr) << '\n';
                continue;
            }
            if (mode == "P") {
                in >> a >> b >> c >> e;
                auto o = options();
                o.environment = [a, b, c](Text key) -> Text {
                    if (*key == u"HTTP_PROXY")
                        return d(a);
                    if (*key == u"HTTPS_PROXY")
                        return d(b);
                    if (*key == u"NO_PROXY")
                        return d(c);
                    return {};
                };
                auto p =
                    ConfigHttpEnvironmentProxy::try_create(native_config_http_environment_proxy_backend(o),
                                                           ConfigHttpEnvironmentProxyPlatform::Windows);
                if (!p) {
                    std::cout << "null" << '\n';
                    continue;
                }
                auto request = d(e);
                auto target =
                    request ? uris.create(request, ConfigUriObjectKind::RelativeOrAbsolute) : Object{};
                auto proxy = p->get_proxy(target);
                std::cout << snapshot(proxy) << ":" << (p->is_bypassed(target) ? "1" : "0") << '\n';
                continue;
            }
            throw std::runtime_error("Unknown mode");
        } catch (const ConfigUriComponentsError& e) {
            std::cout << error(e) << '\n';
        } catch (const ConfigHttpProxyProviderError& e) {
            std::cout << "REQUIRES:" << e.what() << '\n';
        } catch (const std::exception& e) {
            std::cout << "NATIVE:" << e.what() << '\n';
        }
    }
}
