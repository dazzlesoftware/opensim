#include "opensim/framework/config_http_connect_error.hpp"
#include "opensim/framework/config_http_dns_error.hpp"
#include "opensim/framework/config_http_read_error.hpp"
#include "opensim/framework/config_http_write_error.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/config_xml_uri_object.hpp"
#include "opensim/framework/config_http_uri_object.hpp"
#include "opensim/framework/config_http_location.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
ConfigXmlUriObjects objects;
ConfigUriObjectBackend uris = *native_config_uri_object_backend();
std::shared_ptr<ConfigXmlUri> uri(ConfigXmlHttpAddress a) {
    auto text =
        std::string(a.tls ? "https://" : "http://") + a.host + ":" + std::to_string(a.port) + a.target;
    return objects.wrap(
        uris.create(std::make_shared<const std::u16string>(utf16(text)), ConfigUriObjectKind::Absolute));
}
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
std::string read_error(const ConfigHttpReadError& e) {
    if (e.source_type == "System.Net.Http.HttpRequestException")
        std::cerr << "RETRY:NoRetry\n";
    std::ostringstream out;
    out << e.source_type << ':' << std::hex << e.hresult << ':' << hextext(e.message) << ':' << std::dec
        << e.request_error << ":~:0:";
    try {
        std::rethrow_exception(e.inner);
    } catch (const ConfigHttpReadError& inner) {
        out << read_error(inner);
    } catch (const ConfigHttpSocketError& inner) {
        std::cerr << "SOCKET:" << inner.native_error << ":" << inner.socket_error << ":" << inner.native_error
                  << ":" << static_cast<std::int32_t>(inner.hresult) << '\n';
        out << inner.source_type << ':' << std::hex << inner.hresult << ':' << hextext(inner.message)
            << ":-1:~:0:~";
    }
    return out.str();
}
int main() {
    std::string row;
    while (std::getline(std::cin, row)) {
        ConfigXmlNativeHttpServices s;
        s.write_error = config_http_write_error;
        s.read_error = config_http_read_error;
        s.dns_error = config_http_dns_error;
        s.connect_error = config_http_connect_error;
        s.proxy = [](auto, auto) -> std::optional<ConfigXmlHttpRoute> { return {}; };
        s.error = [](auto phase, int code) {
            return std::make_exception_ptr(std::runtime_error(phase + std::to_string(code)));
        };
        try {
            std::shared_ptr<ConfigXmlUri> address;
            auto separator = row.find(' ');
            bool legacy = separator != std::string::npos && separator > 0;
            for (std::size_t i = 0; legacy && i < separator; ++i)
                legacy = row[i] >= '0' && row[i] <= '9';
            if (!legacy)
                address = objects.wrap(uris.create(std::make_shared<const std::u16string>(utf16(row)),
                                                   ConfigUriObjectKind::Absolute));
            else {
                int port;
                std::string path;
                std::istringstream in(row);
                in >> port >> path;
                address = uri({"127.0.0.1", path, static_cast<std::uint16_t>(port), false});
            }
            auto stream = config_xml_download(
                address, config_xml_native_http_backend(config_xml_bind_typed_http(s, objects, uris, []() {
                    return native_invariant_config_http_cookies_backend();
                })));
            auto bytes = stream->copy_to_memory();
            static constexpr char h[] = "0123456789abcdef";
            std::cout << "ok:";
            for (auto b : bytes)
                std::cout << h[b >> 4] << h[b & 15];
            std::cout << '\n';
        } catch (const ConfigHttpReadError& e) {
            std::cout << read_error(e) << "\n";
        } catch (const ConfigHttpConnectionError& e) {
            std::cerr << "RETRY:" << (e.retry_on_next_proxy ? "RetryOnNextProxy" : "NoRetry") << "\n";
            std::cout << e.source_type << ':' << std::hex << e.hresult << ':' << hextext(e.message) << ":"
                      << std::dec << e.request_error << ":~:0:";
            try {
                std::rethrow_exception(e.inner);
            } catch (const ConfigHttpSocketError& inner) {
                std::cerr << std::dec << "SOCKET:" << inner.native_error << ":" << inner.socket_error << ":"
                          << inner.native_error << ":" << static_cast<std::int32_t>(inner.hresult) << '\n';
                std::cout << inner.source_type << ':' << std::hex << inner.hresult << ':'
                          << hextext(inner.message) << ":-1:~:0:~" << std::dec;
            }
            std::cout << '\n';
        } catch (const ConfigUriComponentsError& e) {
            std::cout << e.source_type << ':' << std::hex << e.hresult << ':' << hextext(e.message)
                      << ":-1:~:0:~" << std::dec << '\n';
        } catch (const ConfigXmlHttpError& e) {
            std::cout << error(e) << '\n';
        } catch (const ConfigHttpHeaderError& e) {
            std::cout << error(e) << '\n';
        } catch (const std::exception& e) {
            std::cout << "NATIVE:" << e.what() << '\n';
        }
    }
}
