#include "opensim/framework/config_http_argument_error.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_http_location.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
namespace opensim::framework {
namespace {
class Cookies final : public ConfigXmlCookieContainer {
    ConfigXmlUriObjects objects_;
    ConfigHttpCookies cookies_;
    std::shared_ptr<ConfigCookieUri> target(const std::shared_ptr<ConfigXmlUri>& uri) {
        return config_cookie_uri_object(objects_.get(uri));
    }

  public:
    Cookies(ConfigXmlUriObjects objects, std::shared_ptr<ConfigHttpCookiesBackend> backend)
        : objects_(std::move(objects)), cookies_(std::move(backend)) {
    }
    NiniWriterText header(const std::shared_ptr<ConfigXmlUri>& uri) override {
        return cookies_.header_for_uri(target(uri));
    }
    void set(const std::shared_ptr<ConfigXmlUri>& uri, NiniWriterText value) override {
        cookies_.receive_for_uri(target(uri), {std::move(value)});
    }
};
}
ConfigXmlNativeHttpServices
config_xml_bind_typed_http(ConfigXmlNativeHttpServices policy, ConfigXmlUriObjects objects,
                           ConfigUriObjectBackend uris,
                           std::function<std::shared_ptr<ConfigHttpCookiesBackend>()> new_cookie_backend) {
    if (!new_cookie_backend)
        throw std::invalid_argument(
            "Typed XML HTTP requires an explicit fresh CookieContainer backend factory");
    auto create = uris.create;
    auto location = std::make_shared<ConfigHttpLocationAdapter>(std::move(uris));
    auto other_address = policy.address;
    policy.address = [objects, other_address, create](const std::shared_ptr<ConfigXmlUri>& uri) {
        auto object = objects.get(uri);
        auto value = object->http_components();
        if (!value) {
            if (other_address)
                return other_address(uri);
            if (!create || !config_generic_uri_owns(object))
                throw std::logic_error("Typed XML HTTP requires native URI component providers");
            auto host = *config_generic_uri_component(object, ConfigGenericUriComponent::IdnHost);
            auto port = config_generic_uri_port(object);
            if (host.find(u':') != host.npos && !host.starts_with(u"["))
                host = u"[" + host + u"]";
            auto authority = u"http://" + host;
            if (port != -1) {
                auto number = std::to_string(port);
                authority += u":" + std::u16string(number.begin(), number.end());
            }
            authority += u'/';
            auto validated = create(std::make_shared<const std::u16string>(std::move(authority)),
                                    ConfigUriObjectKind::Absolute)
                                 ->http_components();
            if (!validated)
                throw std::logic_error("HTTP authority constructor returned no native components");
            if (port < 0) {
                std::u16string message =
                    u"port ('-1') must be greater than or equal to '0'. (Parameter 'port')";
#ifdef _WIN32
                message += u"\r\n";
#else
                message += u"\n";
#endif
                message += u"Actual value was -1.";
                throw ConfigHttpArgumentOutOfRangeError(std::move(message), u"port", port);
            }
            return ConfigXmlHttpAddress{
                utf8(*validated->idn_host()),
                utf8(*config_generic_uri_component(object, ConfigGenericUriComponent::AbsolutePath)) +
                    utf8(*config_generic_uri_component(object, ConfigGenericUriComponent::Query)),
                static_cast<std::uint16_t>(port),
                *object->scheme() == u"https" || *object->scheme() == u"wss"};
        }
        return ConfigXmlHttpAddress{utf8(*value->idn_host()),
                                    utf8(*value->absolute_path()) + utf8(*value->query()),
                                    static_cast<std::uint16_t>(value->port()), *value->scheme() == u"https"};
    };
    policy.redirect = [objects, location](const std::shared_ptr<ConfigXmlUri>& uri, std::int32_t status,
                                          const std::vector<NiniWriterText>& values) {
        auto next = location->redirect(objects.get(uri), status, values);
        return next ? objects.wrap(next) : std::shared_ptr<ConfigXmlUri>{};
    };
    policy.cookies = [objects, new_cookie_backend]() {
        auto backend = new_cookie_backend();
        if (!backend)
            throw std::invalid_argument("Typed XML HTTP CookieContainer factory returned no backend");
        return std::make_shared<Cookies>(objects, std::move(backend));
    };
    return policy;
}
}
