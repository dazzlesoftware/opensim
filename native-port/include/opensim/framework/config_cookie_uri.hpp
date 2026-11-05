// Exact consumed URI surface for CookieContainer, including non-HTTP schemes.
#pragma once
#include "opensim/framework/config_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
namespace opensim::framework {
class ConfigCookieUri {
public:
 virtual ~ConfigCookieUri()=default;
 virtual ConfigUriText host()const=0;
 virtual ConfigUriText absolute_path()const=0;
 virtual std::int32_t port()const=0;
 virtual ConfigUriText scheme()const=0;
 virtual ConfigUriText absolute_uri()const=0;
};
std::shared_ptr<ConfigCookieUri> config_cookie_uri_components(std::shared_ptr<ConfigUriComponents>);
std::shared_ptr<ConfigCookieUri> config_cookie_uri_object(ConfigUriObjectPtr);
}
