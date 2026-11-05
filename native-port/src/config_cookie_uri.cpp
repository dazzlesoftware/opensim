#include "opensim/framework/config_cookie_uri.hpp"
#include "opensim/framework/config_generic_uri_object.hpp"
namespace opensim::framework {
namespace {
class Components final : public ConfigCookieUri {
    std::shared_ptr<ConfigUriComponents> uri_;

  public:
    explicit Components(std::shared_ptr<ConfigUriComponents> uri) : uri_(std::move(uri)) {}
    ConfigUriText host() const override {
        return uri_->host();
    }
    ConfigUriText absolute_path() const override {
        return uri_->absolute_path();
    }
    std::int32_t port() const override {
        return uri_->port();
    }
    ConfigUriText scheme() const override {
        return uri_->scheme();
    }
    ConfigUriText absolute_uri() const override {
        return uri_->absolute_uri();
    }
};
class Object final : public ConfigCookieUri {
    ConfigUriObjectPtr uri_;

  public:
    explicit Object(ConfigUriObjectPtr uri) : uri_(std::move(uri)) {}
    ConfigUriText host() const override {
        return config_generic_uri_component(uri_, ConfigGenericUriComponent::Host);
    }
    ConfigUriText absolute_path() const override {
        return config_generic_uri_component(uri_, ConfigGenericUriComponent::AbsolutePath);
    }
    std::int32_t port() const override {
        return config_generic_uri_port(uri_);
    }
    ConfigUriText scheme() const override {
        return uri_->scheme();
    }
    ConfigUriText absolute_uri() const override {
        return uri_->absolute_uri();
    }
};
}
std::shared_ptr<ConfigCookieUri> config_cookie_uri_components(std::shared_ptr<ConfigUriComponents> uri) {
    return uri ? std::make_shared<Components>(std::move(uri)) : nullptr;
}
std::shared_ptr<ConfigCookieUri> config_cookie_uri_object(ConfigUriObjectPtr uri) {
    if (!uri)
        return {};
    if (auto http = uri->http_components())
        return config_cookie_uri_components(std::move(http));
    if (!config_generic_uri_owns(uri))
        throw std::invalid_argument("Cookie URI adapter requires an absolute native URI object");
    return std::make_shared<Object>(std::move(uri));
}
}
