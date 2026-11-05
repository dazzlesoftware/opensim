#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/config_xml_reader_stream.hpp"
#include "opensim/framework/nini_xml_document_save.hpp"
#include "opensim/framework/nini_xml_resolver.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
using namespace opensim::framework;
std::string hex(std::u16string_view text) {
    std::string result;
    for (auto c : text)
        for (int n = 12; n >= 0; n -= 4)
            result += "0123456789abcdef"[(c >> n) & 15];
    return result;
}
int main() {
    auto uris = native_config_uri_object_backend();
    ConfigXmlUriObjects objects;
    ConfigXmlNativeHttpServices policy;
    policy.proxy = [](auto, auto) -> std::optional<ConfigXmlHttpRoute> { return {}; };
    policy.error = [](auto phase, int value) {
        return std::make_exception_ptr(std::runtime_error(phase + std::to_string(value)));
    };
    auto download = config_xml_native_http_backend(config_xml_bind_typed_http(
        policy, objects, *uris, []() { return native_invariant_config_http_cookies_backend(); }));
    auto resolver = native_nini_xml_url_reader_backend(uris, [objects, download](auto value) {
        return config_xml_reader_stream(config_xml_download(objects.wrap(value), download));
    });
    auto io = native_xml_document_io(resolver);
    auto xml = native_nini_xml_config_backend(io);
    auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
    std::string url;
    while (std::getline(std::cin, url))
        try {
            auto source = NiniXmlConfigSource::create(arena, xml, nini_writer_text(utf16(url)));
            auto startup = source->configs()->at(nini_writer_text(u"Startup"));
            if (!startup)
                throw std::runtime_error("Missing Startup section");
            std::cout << hex(*startup->get_string(nini_writer_text(u"Name"))) << ":"
                      << hex(*startup->get_string(nini_writer_text(u"Greeting"))) << "\n";
        } catch (const std::exception& e) {
            std::cout << "E:" << e.what() << "\n";
        }
}
