#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
struct Node {
    std::shared_ptr<ConfigUriComponents> value;
};
std::shared_ptr<ConfigXmlUri> wrap(std::shared_ptr<ConfigUriComponents> value) {
    auto u = std::make_shared<ConfigXmlUri>();
    u->scheme = [value]() { return value->scheme(); };
    u->native_uri = std::make_shared<Node>(Node{std::move(value)});
    return u;
}
std::shared_ptr<ConfigXmlUri> uri(ConfigXmlHttpAddress a) {
    auto text =
        std::string(a.tls ? "https://" : "http://") + a.host + ":" + std::to_string(a.port) + a.target;
    return wrap(ConfigUriComponents::absolute(std::make_shared<const std::u16string>(utf16(text)),
                                              native_config_uri_components_backend()));
}
// The raw UTF16 transport boundary delegates directly to the source cookie jar.
struct Cookies : ConfigXmlCookieContainer {
    ConfigHttpCookies jar{native_invariant_config_http_cookies_backend()};
    std::shared_ptr<ConfigUriComponents> target(const std::shared_ptr<ConfigXmlUri>& u) {
        return std::static_pointer_cast<const Node>(u->native_uri)->value;
    }
    NiniWriterText header(const std::shared_ptr<ConfigXmlUri>& u) override {
        return jar.header(target(u));
    }
    void set(const std::shared_ptr<ConfigXmlUri>& u, NiniWriterText v) override {
        jar.receive(target(u), {v});
    }
};
std::string hextext(std::u16string_view text) {
    if (text.empty())
        return "-";
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : text)
        out << std::setw(4) << unsigned(c);
    return out.str();
}
std::string error(const ConfigHttpHeaderError& e) {
    std::ostringstream out;
    out << e.source_type << ':' << std::hex << e.hresult << ':' << hextext(e.message) << ':' << std::dec
        << e.request_error << ":~:" << (e.retry_on_connection_failure ? "1" : "0") << ':'
        << (e.inner ? error(*e.inner) : "~");
    return out.str();
}
std::string error(const ConfigXmlHttpError& e) {
    std::ostringstream out;
    out << e.source_type << ':' << std::hex << e.hresult << ':' << hextext(e.message)
        << ":0:" << (e.status ? std::to_string(*e.status) : "~") << ":0:~";
    return out.str();
}
int main() {
    int port;
    std::string path;
    while (std::cin >> port >> path) {
        ConfigXmlNativeHttpServices s;
        s.address = [](auto u) {
            auto v = std::static_pointer_cast<const Node>(u->native_uri)->value;
            return ConfigXmlHttpAddress{utf8(*v->idn_host()), utf8(*v->absolute_path()) + utf8(*v->query()),
                                        static_cast<std::uint16_t>(v->port()), *v->scheme() == u"https"};
        };
        s.redirect = [](auto u, NiniWriterText path) -> std::shared_ptr<ConfigXmlUri> {
            try {
                return wrap(ConfigUriComponents::resolve(
                    std::static_pointer_cast<const Node>(u->native_uri)->value, path));
            } catch (const ConfigUriComponentsError&) {
                return {};
            }
        };
        s.cookies = []() { return std::make_shared<Cookies>(); };
        s.proxy = [](auto, auto) -> std::optional<ConfigXmlHttpRoute> { return {}; };
        s.error = [](auto phase, int code) {
            return std::make_exception_ptr(std::runtime_error(phase + std::to_string(code)));
        };
        try {
            auto stream =
                config_xml_download(uri({"127.0.0.1", path, static_cast<std::uint16_t>(port), false}),
                                    config_xml_native_http_backend(s));
            auto bytes = stream->copy_to_memory();
            static constexpr char h[] = "0123456789abcdef";
            std::cout << "ok:";
            for (auto b : bytes)
                std::cout << h[b >> 4] << h[b & 15];
            std::cout << '\n';
        } catch (const ConfigXmlHttpError& e) {
            std::cout << error(e) << '\n';
        } catch (const ConfigHttpHeaderError& e) {
            std::cout << error(e) << '\n';
        } catch (const std::exception& e) {
            std::cout << "NATIVE:" << e.what() << '\n';
        }
    }
}
