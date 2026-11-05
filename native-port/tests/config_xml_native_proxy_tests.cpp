#include "opensim/framework/config_xml_native_proxy.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
using namespace opensim::framework;
struct Stop : std::exception {};
struct Credential : ConfigXmlCredentials {
    int id;
    explicit Credential(int value) : id(value) {
    }
};
struct Proxy : ConfigHttpProxy {
    std::string mode;
    ConfigUriObjectPtr uri;
    std::shared_ptr<ConfigXmlCredentials> value;
    mutable int getters = 0, bypass = 0, get = 0;
    std::shared_ptr<ConfigXmlCredentials> credentials() const override {
        ++getters;
        if (mode == "cthrow")
            throw Stop{};
        if (mode == "alternate" || mode == "bypass-alternate")
            return getters == 1 ? config_http_default_network_credentials() : value;
        if (mode == "default" || mode == "bypass-default")
            return config_http_default_network_credentials();
        return mode == "null" ? nullptr : value;
    }
    void credentials(std::shared_ptr<ConfigXmlCredentials> c) override {
        value = std::move(c);
    }
    bool is_bypassed(ConfigUriObjectPtr) const override {
        ++bypass;
        if (mode == "bthrow")
            throw Stop{};
        return mode == "bypass" || mode == "bypass-default" || mode == "bypass-alternate";
    }
    ConfigUriObjectPtr get_proxy(ConfigUriObjectPtr) const override {
        ++get;
        if (mode == "gthrow")
            throw Stop{};
        return mode == "noproxy" ? nullptr : uri;
    }
};
struct State {
    std::string endpoint;
    std::shared_ptr<ConfigXmlCredentials> captured;
    bool defaults = false;
};
struct ObservePlan : ConfigXmlHttpProxyRoutePlan {
    std::shared_ptr<ConfigXmlHttpProxyRoutePlan> inner;
    State& state;
    ObservePlan(std::shared_ptr<ConfigXmlHttpProxyRoutePlan> p, State& s) : inner(std::move(p)), state(s) {
    }
    std::optional<ConfigXmlHttpRoute> current() override {
        auto route = inner->current();
        if (route &&
            (route->credentials != state.captured || route->default_credentials_used != state.defaults))
            throw std::logic_error("lost captured credential");
        state.endpoint = route ? route->host + ":" + std::to_string(route->port) : "destination.example:8123";
        throw Stop{};
    }
    bool advance() override {
        return inner->advance();
    }
};
struct Observe : ConfigXmlHttpProxyProvider {
    std::shared_ptr<ConfigXmlHttpProxyProvider> inner;
    State& state;
    Observe(std::shared_ptr<ConfigXmlHttpProxyProvider> p, State& s) : inner(std::move(p)), state(s) {
    }
    std::shared_ptr<ConfigXmlCredentials> credentials() const override {
        return inner->credentials();
    }
    bool default_credentials_used() const override {
        return inner->default_credentials_used();
    }
    std::shared_ptr<ConfigXmlHttpProxyRoutePlan> routes(const std::shared_ptr<ConfigXmlUri>& uri) override {
        state.captured = inner->credentials();
        state.defaults = inner->default_credentials_used();
        return std::make_shared<ObservePlan>(inner->routes(uri), state);
    }
};
std::string hex(std::u16string_view s) {
    std::string t;
    for (auto c : s)
        for (int n = 12; n >= 0; n -= 4)
            t += "0123456789abcdef"[(c >> n) & 15];
    return t;
}
int id(const std::shared_ptr<ConfigXmlCredentials>& value) {
    if (value == config_http_default_network_credentials())
        return -1;
    auto tag = std::dynamic_pointer_cast<Credential>(value);
    return tag ? tag->id : 0;
}
int main() {
    auto uris = *native_config_uri_object_backend();
    std::string mode;
    while (std::getline(std::cin, mode)) {
        ConfigXmlUriObjects objects;
        State state;
        auto ca = std::make_shared<Credential>(1), cb = std::make_shared<Credential>(2),
             cc = std::make_shared<Credential>(3);
        auto a = std::make_shared<Proxy>(), b = std::make_shared<Proxy>(), c = std::make_shared<Proxy>();
        a->mode = mode;
        b->mode = "normal";
        c->mode = mode == "default" ? "bypass-default" : mode == "alternate" ? "bypass-alternate" : "bypass";
        if (mode == "alternate")
            c->value = cc;
        a->value = ca;
        b->value = cb;
        a->uri = uris.create(nini_writer_text(mode == "relative" ? u"relative"
                                              : mode == "ftp"    ? u"ftp://proxy-a.example:8080/"
                                                                 : u"http://proxy-a.example:8080/"),
                             ConfigUriObjectKind::RelativeOrAbsolute);
        b->uri =
            uris.create(nini_writer_text(u"http://proxy-b.example:8081/"), ConfigUriObjectKind::Absolute);
        auto selector = std::make_shared<ConfigHttpDefaultProxy>(ConfigHttpDefaultProxyOptions{});
        selector->set(a);
        ConfigXmlNativeHttpServices services;
        services.error = [](auto, int) { return std::make_exception_ptr(Stop{}); };
        services = config_xml_bind_native_proxy(std::move(services), objects, {selector, cc});
        auto factory = services.proxy_provider;
        services.proxy_provider = [&](auto proxy, bool explicit_proxy) {
            return std::make_shared<Observe>(factory(proxy, explicit_proxy), state);
        };
        services = config_xml_bind_typed_http(std::move(services), objects, uris,
                                              [] { return native_invariant_config_http_cookies_backend(); });
        auto backend = config_xml_native_http_backend(services);
        auto client = backend.new_client(backend.new_handler());
        auto dormant = backend.new_client(backend.new_handler());
        auto specific = backend.new_handler();
        specific->proxy(c);
        auto explicit_client = backend.new_client(specific);
        auto target = objects.wrap(uris.create(nini_writer_text(u"http://destination.example:8123/path"),
                                               ConfigUriObjectKind::Absolute));
        auto send = [&](auto use) {
            state = {};
            std::string error;
            try {
                use->get_stream(target);
            } catch (const Stop&) {
                error = "Stop";
            } catch (const ConfigUriComponentsError& e) {
                error = e.source_type + ":" + std::to_string(static_cast<std::int32_t>(e.hresult)) + ":" +
                        hex(e.message);
            }
            std::cout << (state.endpoint.empty() ? "E:" + error : "P:" + state.endpoint) << ":C"
                      << id(state.captured) << ":F" << state.defaults << "|";
        };
        send(client);
        a->value = cb;
        selector->set(b);
        send(client);
        send(dormant);
        send(explicit_client);
        std::cout << a->getters << "," << b->getters << "," << c->getters << ":" << a->bypass << ","
                  << b->bypass << "," << c->bypass << ":" << a->get << "," << b->get << "," << c->get << "\n";
    }
}
