// SocketsHttpHandler single-IWebProxy lookup; .NET 8.0.31, MIT runtime license.
#pragma once
#include "opensim/framework/config_http_environment_proxy.hpp"
namespace opensim::framework {
// null is direct. Lookup exceptions are ignored, but missing native dependency
// errors are not source-managed failures and must remain explicit.
ConfigUriObjectPtr config_http_resolve_proxy(ConfigUriObjectPtr,const std::shared_ptr<ConfigHttpProxy>&);
struct ConfigHttpProxyEndpoint {
    ConfigUriText host;
    std::int32_t port;
};
// Native URI endpoint consumed by the connection pool/DnsEndPoint constructor.
ConfigHttpProxyEndpoint config_http_proxy_endpoint(ConfigUriObjectPtr);
bool config_http_supported_proxy_scheme(std::u16string_view);
}
