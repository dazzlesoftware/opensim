#include "opensim/framework/config_http_default_proxy.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_xml_uri_object.hpp"
#include <iostream>
#include "opensim/framework/text.hpp"
using namespace opensim::framework;
struct Stop : std::exception {};
struct Jar : ConfigXmlCookieContainer {
    NiniWriterText header(const std::shared_ptr<ConfigXmlUri> &) override { return nini_writer_text(u""); }
    void set(const std::shared_ptr<ConfigXmlUri> &, NiniWriterText) override {}
};
int main(int argc, char **argv) {
    if (argc != 2)
        return 20;
    auto port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    auto a = std::make_shared<ConfigHttpNoProxy>(), b = std::make_shared<ConfigHttpNoProxy>(),
         c = std::make_shared<ConfigHttpNoProxy>();
    std::shared_ptr<ConfigXmlProxy> current = a;
    int getters = 0, ca = 0, cb = 0, cc = 0;
    ConfigXmlNativeHttpServices s;
    s.cookies = []() { return std::make_shared<Jar>(); };
    s.default_proxy = [&]() {
        ++getters;
        return current;
    };
    s.address = [port](auto) { return ConfigXmlHttpAddress{"127.0.0.1", "/", port, false}; };
    s.redirect = [](auto, int, auto) -> std::shared_ptr<ConfigXmlUri> { return {}; };
    s.error = [](auto, int) { return std::make_exception_ptr(Stop{}); };
    s.proxy = [&](auto, auto proxy) -> std::optional<ConfigXmlHttpRoute> {
        if (proxy == a)
            ++ca;
        else if (proxy == b)
            ++cb;
        else if (proxy == c)
            ++cc;
        else
            throw std::logic_error("wrong proxy");
        return {};
    };
    auto backend = config_xml_native_http_backend(s);
    ConfigXmlUriObjects objects;
    auto uri = objects.wrap(native_config_uri_object_backend()->create(
        nini_writer_text(utf16(std::string("http://127.0.0.1:") + argv[1] + "/")),
        ConfigUriObjectKind::Absolute));
    auto send = [&](auto client) {
        auto bytes = client->get_stream(uri)->copy_to_memory();
        return std::string(bytes.begin(), bytes.end()) == "proxy-capture";
    };
    auto dormant = backend.new_client(backend.new_handler());
    auto h = backend.new_handler();
    auto client = backend.new_client(h);
    if (!send(client))
        return 1;
    current = b;
    if (!send(client))
        return 2;
    if (!send(dormant))
        return 3;
    auto explicit_handler = backend.new_handler();
    explicit_handler->proxy(c);
    if (!send(backend.new_client(explicit_handler)))
        return 4;
    if (getters != 2 || ca != 2 || cb != 1 || cc != 1)
        return 5;
    std::cout << "PASS handler proxy capture: 2,1,1\n";
}
