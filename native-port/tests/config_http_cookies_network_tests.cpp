#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
struct Node {
    ConfigXmlHttpAddress address;
};
std::shared_ptr<ConfigXmlUri> uri(ConfigXmlHttpAddress address) {
    auto u = std::make_shared<ConfigXmlUri>();
    u->scheme = []() { return nini_writer_text(u"http"); };
    u->native_uri = std::make_shared<Node>(Node{std::move(address)});
    return u;
}
// This network fixture deliberately limits HTTP header bytes to ASCII. Header
// decoding beyond ASCII is a distinct transport-provider obligation.
struct Cookies : ConfigXmlCookieContainer {
    ConfigHttpCookies jar{native_invariant_config_http_cookies_backend()};
    std::shared_ptr<ConfigUriComponents> target(const std::shared_ptr<ConfigXmlUri>& u) {
        auto a = std::static_pointer_cast<const Node>(u->native_uri)->address;
        auto text =
            std::string(a.tls ? "https://" : "http://") + a.host + ":" + std::to_string(a.port) + a.target;
        return ConfigUriComponents::absolute(std::make_shared<const std::u16string>(text.begin(), text.end()),
                                             native_config_uri_components_backend());
    }
    std::string header(const std::shared_ptr<ConfigXmlUri>& u) override {
        auto value = jar.header(target(u));
        std::string out;
        for (auto c : *value) {
            if (c > 127)
                throw std::runtime_error("ASCII network fixture boundary");
            out += char(c);
        }
        return out;
    }
    void set(const std::shared_ptr<ConfigXmlUri>& u, std::string v) override {
        for (unsigned char c : v)
            if (c > 127)
                throw std::runtime_error("ASCII network fixture boundary");
        jar.receive(target(u), {std::make_shared<const std::u16string>(v.begin(), v.end())});
    }
};
int main() {
    int port;
    std::string path;
    while (std::cin >> port >> path) {
        ConfigXmlNativeHttpServices s;
        s.address = [](auto u) { return std::static_pointer_cast<const Node>(u->native_uri)->address; };
        s.redirect = [&](auto u, std::string path) {
            auto a = s.address(u);
            a.target = path;
            return uri(a);
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
            std::cout << e.source_type << ":" << std::hex << e.hresult << ":";
            for (auto c : std::string(e.what()))
                std::cout << "0123456789abcdef"[static_cast<unsigned char>(c) >> 4]
                          << "0123456789abcdef"[c & 15];
            std::cout << ":" << std::dec << e.status.value_or(0) << '\n';
        } catch (const std::exception& e) {
            std::cout << "EX:" << e.what() << '\n';
        }
    }
}
