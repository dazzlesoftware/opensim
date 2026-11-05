#include "opensim/framework/config_xml_http_multi_proxy.hpp"
#include "opensim/framework/config_xml_native_proxy.hpp"
#include "opensim/framework/config_http_windows_proxy.hpp"
#include "opensim/framework/config_http_connect_error.hpp"
#include "opensim/framework/config_http_dns_error.hpp"
#include "opensim/framework/config_http_read_error.hpp"
#include "opensim/framework/config_http_write_error.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
namespace opensim::framework {
struct ConfigHttpWindowsProxyFixture {
    static auto cache(ConfigHttpWindowsProxy& proxy) {
        return proxy.cache_;
    }
};
struct ConfigHttpFailedProxyCacheFixture {
    static std::vector<int> ports(ConfigHttpFailedProxyCache& cache) {
        std::lock_guard lock(cache.mutex_);
        std::vector<int> result;
        for (auto& e : cache.entries_)
            result.push_back(e.uri->http_components()->port());
        std::sort(result.begin(), result.end());
        return result;
    }
};
}
using namespace opensim::framework;
std::string hex(std::u16string_view s) {
    std::string t;
    for (auto c : s)
        for (int n = 12; n >= 0; n -= 4)
            t += "0123456789abcdef"[(c >> n) & 15];
    return t;
}
std::string byteshex(const std::vector<std::uint8_t>& bytes) {
    std::string out;
    for (auto c : bytes) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string description(std::string type, std::uint32_t hr, int code, std::u16string_view message) {
    std::ostringstream out;
    out << type << ":" << std::hex << hr << ":" << std::dec << code << ":" << hex(message);
    return out.str();
}
std::string error(std::exception_ptr pointer);
std::string header_error(const ConfigHttpHeaderError& e) {
    return description(e.source_type, e.hresult, e.request_error, e.message) +
           (e.inner ? "|" + header_error(*e.inner) : "");
}
std::string error(std::exception_ptr pointer) {
    try {
        std::rethrow_exception(pointer);
    } catch (const ConfigHttpConnectionError& e) {
        return description(e.source_type, e.hresult, e.request_error, e.message) +
               (e.inner ? "|" + error(e.inner) : "");
    } catch (const ConfigHttpReadError& e) {
        return description(e.source_type, e.hresult, e.request_error, e.message) +
               (e.inner ? "|" + error(e.inner) : "");
    } catch (const ConfigHttpSocketError& e) {
        return description(e.source_type, e.hresult, e.socket_error, e.message);
    } catch (const ConfigHttpHeaderError& e) {
        return header_error(e);
    } catch (const ConfigXmlHttpError& e) {
        return description(e.source_type, e.hresult, 0, e.message);
    } catch (const ConfigUriComponentsError& e) {
        return description(e.source_type, e.hresult, -1, e.message);
    } catch (const std::exception& e) {
        return "NATIVE:" + std::string(e.what());
    }
}
struct TracePlan : ConfigXmlHttpProxyRoutePlan {
    std::shared_ptr<ConfigXmlHttpProxyRoutePlan> inner;
    std::vector<int>& trace;
    TracePlan(std::shared_ptr<ConfigXmlHttpProxyRoutePlan> p, std::vector<int>& t)
        : inner(std::move(p)), trace(t) {
    }
    std::optional<ConfigXmlHttpRoute> current() override {
        auto route = inner->current();
        if (!route || route->host != "127.0.0.1")
            throw ConfigHttpProxyProviderError("Fixture expected a loopback proxy route");
        trace.push_back(route ? route->port : 8123);
        return route;
    }
    bool advance() override {
        return inner->advance();
    }
};
struct TraceProvider : ConfigXmlHttpProxyProvider {
    std::shared_ptr<ConfigXmlHttpProxyProvider> inner;
    std::vector<int>& trace;
    TraceProvider(std::shared_ptr<ConfigXmlHttpProxyProvider> p, std::vector<int>& t)
        : inner(std::move(p)), trace(t) {
    }
    std::shared_ptr<ConfigXmlCredentials> credentials() const override {
        return inner->credentials();
    }
    bool default_credentials_used() const override {
        return inner->default_credentials_used();
    }
    std::shared_ptr<ConfigXmlHttpProxyRoutePlan> routes(const std::shared_ptr<ConfigXmlUri>& uri) override {
        return std::make_shared<TracePlan>(inner->routes(uri), trace);
    }
};
int unit() {
    if (config_xml_http_proxy_retryable({}) ||
        config_xml_http_proxy_retryable(std::make_exception_ptr(std::runtime_error("fatal"))))
        return 1;
    ConfigHttpConnectionError connect(u"connect", {});
    if (!config_xml_http_proxy_retryable(std::make_exception_ptr(connect)))
        return 2;
    connect.retry_on_next_proxy = false;
    if (config_xml_http_proxy_retryable(std::make_exception_ptr(connect)))
        return 3;
    ConfigHttpHeaderError headers("System.Net.Http.HttpRequestException", u"headers", 0x80131500);
    headers.retry_on_connection_failure = true;
    if (!config_xml_http_proxy_retryable(std::make_exception_ptr(headers)))
        return 4;
    headers.retry_on_connection_failure = false;
    if (config_xml_http_proxy_retryable(std::make_exception_ptr(headers)))
        return 5;
    if (config_xml_http_proxy_retryable(std::make_exception_ptr(
            ConfigHttpReadError("System.Net.Http.HttpRequestException", u"read", 0, {}))))
        return 6;
    return 0;
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--unit")
        return unit();
    if (argc != 3)
        return 20;
    ConfigXmlUriObjects objects;
    auto uris = *native_config_uri_object_backend();
    ConfigHttpWindowsProxyOptions options;
    options.read_settings = [&] {
        return ConfigHttpWinInetSettings{{}, nini_writer_text(utf16(argv[1])), {}, false, true};
    };
    std::shared_ptr<ConfigHttpProxy> proxy;
    std::shared_ptr<ConfigHttpFailedProxyCache> cache;
    if (std::string(argv[1]).starts_with("env:")) {
        ConfigHttpEnvironmentProxyNativeOptions environment;
        auto value = nini_writer_text(utf16(std::string(argv[1]).substr(4)));
        environment.environment = [value](auto name) -> ConfigUriText {
            return (*name == u"http_proxy" || *name == u"HTTP_PROXY") ? value : ConfigUriText{};
        };
#ifdef _WIN32
        auto platform = ConfigHttpEnvironmentProxyPlatform::Windows;
#else
        auto platform = ConfigHttpEnvironmentProxyPlatform::Unix;
#endif
        proxy = ConfigHttpEnvironmentProxy::try_create(
            native_config_http_environment_proxy_backend(environment), platform);
    } else {
        auto windows = ConfigHttpWindowsProxy::try_create(options);
        cache = ConfigHttpWindowsProxyFixture::cache(*windows);
        proxy = windows;
    }
    std::vector<int> trace;
    ConfigXmlNativeHttpServices s;
    s.connect_error = config_http_connect_error;
    s.dns_error = config_http_dns_error;
    s.read_error = config_http_read_error;
    s.write_error = config_http_write_error;
    s.error = [](auto phase, int code) {
        return std::make_exception_ptr(std::runtime_error(phase + std::to_string(code)));
    };
    s = config_xml_bind_native_proxy(std::move(s), objects);
    auto factory = s.proxy_provider;
    s.proxy_provider = [&](auto selected, bool explicit_proxy) {
        return std::make_shared<TraceProvider>(factory(selected, explicit_proxy), trace);
    };
    auto backend = config_xml_native_http_backend(config_xml_bind_typed_http(
        s, objects, uris, [] { return native_invariant_config_http_cookies_backend(); }));
    auto handler = backend.new_handler();
    handler->proxy(proxy);
    auto client = backend.new_client(handler);
    for (int request = 0; request < 2; ++request) {
        try {
            auto uri = objects.wrap(
                uris.create(nini_writer_text(utf16(std::string("http://target.example:8123") + argv[2])),
                            ConfigUriObjectKind::Absolute));
            auto stream = client->get_stream(uri);
            auto bytes = stream->copy_to_memory();
            std::cout << "OK:" << byteshex(bytes) << "\n";
        } catch (...) {
            std::cout << "E:" << error(std::current_exception()) << "\n";
        }
    }
    std::cout << "TRACE:";
    for (auto port : trace)
        std::cout << port << ",";
    std::cout << "\nFAILED:";
    for (auto port : cache ? ConfigHttpFailedProxyCacheFixture::ports(*cache) : std::vector<int>{})
        std::cout << port << ",";
    std::cout << "\n";
}
