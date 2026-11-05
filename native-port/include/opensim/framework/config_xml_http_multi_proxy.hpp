#pragma once
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_http_multi_proxy.hpp"
namespace opensim::framework {
// Windows MultiProxy produces HTTP proxy URIs; HTTPS/SOCKS proxy protocols
// require their own route/transport implementations rather than plain HTTP.
std::shared_ptr<ConfigXmlHttpProxyRoutePlan> config_xml_http_multi_proxy_plan(ConfigHttpMultiProxy);
// Verified HttpRequestException AllowRetry classification, before response only.
bool config_xml_http_proxy_retryable(std::exception_ptr);
}
