#include "opensim/framework/config_xml_transport_http.hpp"
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
struct Cookies : ConfigXmlCookieContainer {
    std::string value;
    std::string header(const std::shared_ptr<ConfigXmlUri>&) override {
        return value;
    }
    void set(const std::shared_ptr<ConfigXmlUri>&, std::string v) override {
        value = v.substr(0, v.find(';'));
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
