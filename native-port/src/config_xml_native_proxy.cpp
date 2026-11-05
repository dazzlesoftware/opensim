#include "opensim/framework/config_xml_native_proxy.hpp"
#include "opensim/framework/config_xml_http_multi_proxy.hpp"
#include "opensim/framework/config_http_proxy_resolution.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::framework {
namespace {
class Single final : public ConfigXmlHttpProxyRoutePlan {
    ConfigUriObjectPtr uri_;

  public:
    explicit Single(ConfigUriObjectPtr uri = {}) : uri_(std::move(uri)) {
    }
    std::optional<ConfigXmlHttpRoute> current() override {
        if (!uri_)
            return {};
        auto endpoint = config_http_proxy_endpoint(uri_);
        return ConfigXmlHttpRoute{utf8(*endpoint.host), static_cast<std::uint16_t>(endpoint.port), {}, uri_};
    }
    bool advance() override {
        return false;
    }
};
class Credentials final : public ConfigXmlHttpProxyRoutePlan {
    std::shared_ptr<ConfigXmlHttpProxyRoutePlan> inner_;
    std::shared_ptr<ConfigXmlCredentials> credentials_;
    bool defaults_;

  public:
    Credentials(std::shared_ptr<ConfigXmlHttpProxyRoutePlan> inner,
                std::shared_ptr<ConfigXmlCredentials> credentials, bool defaults)
        : inner_(std::move(inner)), credentials_(std::move(credentials)), defaults_(defaults) {
    }
    std::optional<ConfigXmlHttpRoute> current() override {
        auto route = inner_->current();
        if (route) {
            route->credentials = credentials_;
            route->default_credentials_used = defaults_;
        }
        return route;
    }
    bool advance() override {
        return inner_->advance();
    }
};
class Provider final : public ConfigXmlHttpProxyProvider {
    ConfigXmlUriObjects objects_;
    std::shared_ptr<ConfigHttpProxy> proxy_;
    std::shared_ptr<ConfigXmlCredentials> credentials_;
    ConfigXmlNativeProxyOptions options_;
    bool defaults_ = false;

  public:
    Provider(ConfigXmlUriObjects objects, std::shared_ptr<ConfigXmlProxy> proxy,
             ConfigXmlNativeProxyOptions options, bool explicit_proxy)
        : objects_(std::move(objects)), proxy_(std::dynamic_pointer_cast<ConfigHttpProxy>(proxy)),
          options_(std::move(options)) {
        if (proxy && !proxy_)
            throw ConfigHttpProxyProviderError(
                "Native XML proxy binding requires the consumed IWebProxy adapter");
        if (proxy_ && explicit_proxy) {
            auto normalized = proxy_->credentials();
            auto defaults = config_http_default_network_credentials();
            defaults_ = normalized == defaults || options_.default_proxy_credentials == defaults;
        }
        if (proxy_) {
            credentials_ = proxy_->credentials();
            if (!credentials_)
                credentials_ = options_.default_proxy_credentials;
        }
    }
    std::shared_ptr<ConfigXmlCredentials> credentials() const override {
        return credentials_;
    }
    bool default_credentials_used() const override {
        return defaults_;
    }
    std::shared_ptr<ConfigXmlHttpProxyRoutePlan> routes(const std::shared_ptr<ConfigXmlUri>& uri) override {
        auto target = objects_.get(uri);
        std::shared_ptr<ConfigXmlHttpProxyRoutePlan> plan;
        if (auto multi = std::dynamic_pointer_cast<ConfigHttpMultiWebProxy>(proxy_)) {
            try {
                if (!multi->is_bypassed(target))
                    plan = config_xml_http_multi_proxy_plan(multi->get_multi_proxy(target));
            } catch (const ConfigHttpProxyProviderError&) {
                throw;
            } catch (...) {
                plan = {};
            }
        } else {
            auto resolved = config_http_resolve_proxy(target, proxy_);
            if (resolved)
                plan = std::make_shared<Single>(resolved);
        }
        if (!plan)
            plan = std::make_shared<Single>();
        return std::make_shared<Credentials>(std::move(plan), credentials_, defaults_);
    }
};
}
ConfigXmlNativeHttpServices config_xml_bind_native_proxy(ConfigXmlNativeHttpServices services,
                                                         ConfigXmlUriObjects objects,
                                                         ConfigXmlNativeProxyOptions options) {
    if (options.default_proxy || !services.default_proxy) {
        auto selector =
            options.default_proxy ? options.default_proxy : native_config_http_default_proxy_selector();
        services.default_proxy = [selector]() { return selector->get(); };
    }
    services.proxy_provider = [objects, options](const std::shared_ptr<ConfigXmlProxy>& proxy,
                                                 bool explicit_proxy) {
        return std::make_shared<Provider>(objects, proxy, options, explicit_proxy);
    };
    return services;
}
}
