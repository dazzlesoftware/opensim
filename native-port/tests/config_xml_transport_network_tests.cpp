#include "opensim/framework/config_http_connect_error.hpp"
#include "opensim/framework/config_http_credential_cache.hpp"
#include "opensim/framework/config_http_dns_error.hpp"
#include "opensim/framework/config_http_read_error.hpp"
#include "opensim/framework/config_http_write_error.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/config_http_environment_proxy_native.hpp"
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
class ScopedBasicCredentials final : public ConfigHttpOriginCredentials {
    mutable unsigned basic_lookups_ = 0;

  public:
    std::shared_ptr<ConfigHttpNetworkCredential>
    get_credential(const std::shared_ptr<ConfigXmlUri>& uri, std::string_view scheme) const override {
        const auto address = objects.get(uri)->http_components();
        std::cerr << "CREDENTIAL " << utf8(*address->absolute_path()) << ' ' << scheme << '\n';
        if (scheme != "Basic" || !address || !address->absolute_path()->starts_with(u"/auth-scoped/"))
            return {};
        const bool first_stateful_lookup = *address->absolute_path() == u"/auth-scoped/stateful" &&
                                           basic_lookups_++ == 0;
        return std::make_shared<ConfigHttpNetworkCredential>(
            nini_writer_text(u"fixture"), nini_writer_text(first_stateful_lookup ? u"unused" : u"secret"),
            nini_writer_text(u""));
    }
};
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
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--credential-cache-host") {
        ConfigHttpCredentialCache cache(objects);
        auto credential = std::make_shared<ConfigHttpNetworkCredential>(
            nini_writer_text(u"fixture"), nini_writer_text(u"secret"), nini_writer_text(u""));
        cache.add(u"Example.COM", 80, u"bAsIc", credential);
        const auto print = [&](int port) {
            const auto match = cache.get_credential(u"example.com", port, u"BASIC");
            std::cout << (match ? utf8(*match->user_name) : "-") << '\n';
        };
        print(80);
        print(81);
        cache.remove(u"example.COM", 80, u"Basic");
        print(80);
        return 0;
    }
    bool tls_proxy = false;
    bool authenticate_proxy = false;
    bool use_proxy_cache = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view option = argv[i];
        tls_proxy |= option == "--tls-proxy";
        authenticate_proxy |= option == "--proxy-auth";
        use_proxy_cache |= option == "--proxy-cache";
    }
    std::optional<ConfigXmlHttpRoute> proxy;
    if (argc >= 2) {
        const auto port = static_cast<std::uint16_t>(std::stoi(argv[1]));
        auto address = uri({"127.0.0.1", "/", port, tls_proxy});
        proxy = ConfigXmlHttpRoute{"127.0.0.1", port, {}, objects.get(address)};
        if (authenticate_proxy || use_proxy_cache) {
            auto credential = std::make_shared<ConfigHttpNetworkCredential>(
                nini_writer_text(u"f\u00efxture"), nini_writer_text(u"p\u00e4ss"), nini_writer_text(u""));
            if (use_proxy_cache) {
                auto cache = std::make_shared<ConfigHttpCredentialCache>(objects);
                cache->add(proxy->proxy_uri, u"NTLM", credential);
                cache->add(proxy->proxy_uri, u"Negotiate", credential);
                cache->add(proxy->proxy_uri, u"Digest", credential);
                proxy->credentials = std::move(cache);
            } else {
                proxy->credentials = std::move(credential);
            }
        }
    }
    std::string row;
    while (std::getline(std::cin, row)) {
        ConfigXmlNativeHttpServices s;
        if ((row.starts_with("https://") || tls_proxy) &&
            (row.ends_with("/auth-ntlm") || row.ends_with("/auth-kerberos")))
            s.tls_settings = ConfigXmlHttpTlsSettings{false, false};
        s.write_error = config_http_write_error;
        s.read_error = config_http_read_error;
        s.dns_error = config_http_dns_error;
        s.connect_error = config_http_connect_error;
        s.proxy = [proxy](auto, auto) { return proxy; };
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
            std::shared_ptr<ConfigXmlCredentials> credentials;
            if (row.ends_with("/cache-redirect")) {
                const auto origin = objects.get(address);
                auto cache = std::make_shared<ConfigHttpCredentialCache>(objects);
                const auto add = [&](std::u16string path, std::u16string password) {
                    cache->add(uris.combine_string(origin, nini_writer_text(std::move(path))), u"bAsIc",
                               std::make_shared<ConfigHttpNetworkCredential>(
                                   nini_writer_text(u"fixture"), nini_writer_text(std::move(password)),
                                   nini_writer_text(u"")));
                };
                add(u"/", u"wrong");
                add(u"/AUTH-SCOPED/file", u"secret");
                add(u"/AUTH-SCOPED/a-longer-file", u"removed");
                cache->remove(uris.combine_string(origin, nini_writer_text(u"/AUTH-SCOPED/a-longer-file#fragment")),
                              u"BASIC");
                credentials = std::move(cache);
            }
            else if (row.find("/auth-scoped") != row.npos)
                credentials = std::make_shared<ScopedBasicCredentials>();
            else if (row.ends_with("/auth-default"))
                credentials = config_http_default_network_credentials();
            else if (row.ends_with("/auth-kerberos"))
                credentials = std::make_shared<ConfigHttpNetworkCredential>(
                    nini_writer_text(u"fixture"), nini_writer_text(u"fixture-password"), nini_writer_text(u"OPENSIM.TEST"));
            else if (row.find("/auth-ntlm") != row.npos)
                credentials = std::make_shared<ConfigHttpNetworkCredential>(
                    nini_writer_text(u"f\u00efxture"), nini_writer_text(u"p\u00e4ss"), nini_writer_text(u""));
            else if (row.find("/auth-") != row.npos)
                credentials = std::make_shared<ConfigHttpNetworkCredential>(
                    nini_writer_text(u"fixture"), nini_writer_text(u"secret"), nini_writer_text(u""));
            auto stream = config_xml_download(
                address, config_xml_native_http_backend(config_xml_bind_typed_http(s, objects, uris, []() {
                    return native_invariant_config_http_cookies_backend();
                })), credentials);
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
