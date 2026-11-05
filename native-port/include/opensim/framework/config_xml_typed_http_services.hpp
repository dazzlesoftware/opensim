// Concrete typed URI/CookieContainer composition for versioned XML HTTP transport.
#pragma once
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_xml_uri_object.hpp"
#include "opensim/framework/config_http_cookies.hpp"
namespace opensim::framework {
// Fills canonical HTTP address, full typed RedirectHandler and fresh cookie jar.
// The caller explicitly owns proxy, TLS, credentials and OS failure policies.
// Existing policy.address is retained only as the required nonHTTP dispatch.
ConfigXmlNativeHttpServices config_xml_bind_typed_http(
 ConfigXmlNativeHttpServices policy,ConfigXmlUriObjects,ConfigUriObjectBackend,
 std::function<std::shared_ptr<ConfigHttpCookiesBackend>()> new_cookie_backend);
}
