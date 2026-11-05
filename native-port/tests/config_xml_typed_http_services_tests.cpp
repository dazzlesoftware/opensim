#include "opensim/framework/config_http_argument_error.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_http_uri_object.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include <stdexcept>
using namespace opensim::framework;
int main() {
    ConfigXmlUriObjects objects;
    auto uris = *native_config_uri_object_backend();
    int created = 0;
    ConfigXmlNativeHttpServices policy;
    policy.proxy = [](auto, auto) -> std::optional<ConfigXmlHttpRoute> { return {}; };
    policy.error = [](auto, int) { return std::make_exception_ptr(std::runtime_error("explicit-policy")); };
    auto services = config_xml_bind_typed_http(policy, objects, uris, [&]() {
        ++created;
        return native_invariant_config_http_cookies_backend();
    });
    auto uri =
        objects.wrap(uris.create(std::make_shared<const std::u16string>(u"HTTP://127.1:81/a/../b?q#base"),
                                 ConfigUriObjectKind::Absolute));
    auto address = services.address(uri);
    if (address.host != "127.0.0.1" || address.port != 81 || address.target != "/b?q" || address.tls)
        return 1;
    auto first = services.cookies(), second = services.cookies();
    if (created != 2)
        return 2;
    first->set(uri, std::make_shared<const std::u16string>(u"a=first;Path=/"));
    if (*first->header(uri) != u"a=first" || !second->header(uri)->empty())
        return 3;
    auto next = services.redirect(uri, 302, {std::make_shared<const std::u16string>(u"/c")});
    if (!next || *objects.get(next)->absolute_uri() != u"http://127.0.0.1:81/c#base")
        return 4;
    if (services.redirect(uri, 200, {std::make_shared<const std::u16string>(u"/c")}))
        return 5;
    bool required = false;
    try {
        config_xml_bind_typed_http(policy, objects, uris, {});
    } catch (const std::invalid_argument&) {
        required = true;
    }
    if (!required)
        return 6;
    required = false;
    auto missing = config_xml_bind_typed_http(
        {}, objects, uris, []() { return native_invariant_config_http_cookies_backend(); });
    try {
        config_xml_native_http_backend(missing);
    } catch (const std::invalid_argument&) {
        required = true;
    }
    if (!required)
        return 7;
    auto null_factory = config_xml_bind_typed_http(
        policy, objects, uris, []() { return std::shared_ptr<ConfigHttpCookiesBackend>{}; });
    required = false;
    try {
        null_factory.cookies();
    } catch (const std::invalid_argument&) {
        required = true;
    }
    if (!required)
        return 8;
    auto no_port = objects.wrap(uris.create(std::make_shared<const std::u16string>(u"a+b://127.0.0.1/path"),
                                            ConfigUriObjectKind::Absolute));
    bool port_error = false;
    try {
        services.address(no_port);
    } catch (const ConfigHttpArgumentOutOfRangeError& e) {
        port_error = e.actual_value == -1 && e.parameter == std::optional<std::u16string>(u"port") &&
                     e.source_type == "System.ArgumentOutOfRangeException";
    }
    if (!port_error)
        return 9;
    return 0;
}
