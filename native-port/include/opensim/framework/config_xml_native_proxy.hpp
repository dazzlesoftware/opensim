#pragma once
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_xml_uri_object.hpp"
#include "opensim/framework/config_http_default_proxy.hpp"
namespace opensim::framework {
struct ConfigXmlNativeProxyOptions {
 std::shared_ptr<ConfigHttpDefaultProxy> default_proxy;
 std::shared_ptr<ConfigXmlCredentials> default_proxy_credentials;

};
ConfigXmlNativeHttpServices config_xml_bind_native_proxy(ConfigXmlNativeHttpServices, ConfigXmlUriObjects,
                                                         ConfigXmlNativeProxyOptions = {});
}
