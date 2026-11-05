#include "opensim/framework/config_http_proxy_resolution.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/config_http_argument_error.hpp"
namespace opensim::framework {
bool config_http_supported_proxy_scheme(std::u16string_view scheme) {
    std::u16string lower(scheme);
    for (auto &c : lower)
        if (c >= u'A' && c <= u'Z')
            c += u'a' - u'A';
    return lower == u"http" || lower == u"https" || lower == u"socks4" || lower == u"socks4a" ||
           lower == u"socks5";
}
ConfigHttpProxyEndpoint config_http_proxy_endpoint(ConfigUriObjectPtr uri) {
    ConfigHttpProxyEndpoint result;
    if (auto http = uri->http_components()) {
        result.host = http->idn_host();
        result.port = http->port();
    } else if (config_generic_uri_owns(uri)) {
        result.host = config_generic_uri_component(uri, ConfigGenericUriComponent::IdnHost);
        result.port = config_generic_uri_port(uri);
    } else
        throw ConfigHttpProxyProviderError("Proxy endpoint requires native URI component providers");
    if (result.port < 0) {
        std::u16string message = u"port ('-1') must be greater than or equal to '0'. (Parameter 'port')";
#ifdef _WIN32
        message += u"\r\n";
#else
        message += u"\n";
#endif
        message += u"Actual value was -1.";
        throw ConfigHttpArgumentOutOfRangeError(std::move(message), u"port", result.port);
    }
    return result;
}
ConfigUriObjectPtr config_http_resolve_proxy(ConfigUriObjectPtr target,
                                             const std::shared_ptr<ConfigHttpProxy> &proxy) {
    ConfigUriObjectPtr result;
    if (proxy) {
        try {
            if (!proxy->is_bypassed(target))
                result = proxy->get_proxy(target);
        } catch (const ConfigHttpProxyProviderError &) {
            throw;
        } catch (...) {
            return {};
        }
    }
    if (result && !config_http_supported_proxy_scheme(*result->scheme()))
        throw ConfigUriComponentsError(
            "System.NotSupportedException",
            u"Only the 'http', 'https', 'socks4', 'socks4a' and 'socks5' schemes are allowed for proxies.",
            0x80131515);
    return result;
}
}
