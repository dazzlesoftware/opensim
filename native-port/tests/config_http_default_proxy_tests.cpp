#include "opensim/framework/config_http_default_proxy.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include <iostream>
#include <future>
#include <thread>
#include <vector>
using namespace opensim::framework;
int main() {
    ConfigHttpEnvironmentProxyNativeOptions native;
    int calls = 0;
    native.environment = [&](auto) {
        ++calls;
        return ConfigUriText{};
    };
    ConfigHttpDefaultProxyOptions options;
    options.environment = native_config_http_environment_proxy_backend(native);
    options.platform = ConfigHttpSystemProxyPlatform::Linux;
    ConfigHttpDefaultProxy cache(options);
    auto first = cache.get();
    if (!first || !std::dynamic_pointer_cast<ConfigHttpNoProxy>(first) || !first->is_bypassed({}) ||
        first->get_proxy({}) || first->credentials())
        return 1;
    int count = calls;
    if (cache.get() != first || calls != count)
        return 2;
    auto credential = config_http_default_network_credentials();
    first->credentials(credential);
    if (first->credentials() != credential)
        return 3;
    auto replacement = std::make_shared<ConfigHttpNoProxy>();
    cache.set(replacement);
    if (cache.get() != replacement)
        return 4;
    bool null = false;
    try {
        cache.set({});
    } catch (const ConfigUriComponentsError& e) {
        null = e.source_type == "System.ArgumentNullException" &&
               e.parameter == std::optional<std::u16string>(u"value") && e.hresult == 0x80004003;
    }
    if (!null || cache.get() != replacement)
        return 5;
    options.platform = ConfigHttpSystemProxyPlatform::Windows;
    int system = 0;
    options.system_proxy = [&]() -> std::shared_ptr<ConfigHttpProxy> {
        ++system;
        throw std::runtime_error("OS proxy failure");
    };
    ConfigHttpDefaultProxy failed(options);
    for (int i = 0; i < 2; ++i) {
        try {
            failed.get();
            return 6;
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()) != "OS proxy failure")
                return 7;
        }
    }
    if (system != 1)
        return 8;
    failed.set(replacement);
    if (failed.get() != replacement)
        return 9;
    options.system_proxy = [&] {
        ++system;
        return std::shared_ptr<ConfigHttpProxy>{};
    };
    if (!std::dynamic_pointer_cast<ConfigHttpNoProxy>(config_http_construct_default_proxy(options)))
        return 10;
    native.environment = [](auto name) -> ConfigUriText {
        if (*name == u"HTTP_PROXY")
            return nini_writer_text(u"proxy:8080");
        return {};
    };
    options.environment = native_config_http_environment_proxy_backend(native);
    options.system_proxy = [&]() -> std::shared_ptr<ConfigHttpProxy> {
        throw std::logic_error("Environment did not win");
    };
    if (!std::dynamic_pointer_cast<ConfigHttpEnvironmentProxy>(config_http_construct_default_proxy(options)))
        return 11;
    // A setter during lazy initialization wins publication, without waiting for
    // the system factory to finish (LazyInitializer.EnsureInitialized semantics).
    native.environment = [](auto) -> ConfigUriText { return {}; };
    options.environment = native_config_http_environment_proxy_backend(native);
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    options.system_proxy = [&] {
        entered.set_value();
        released.wait();
        return first;
    };
    ConfigHttpDefaultProxy concurrent(options);
    auto pending = std::async(std::launch::async, [&] { return concurrent.get(); });
    entered.get_future().wait();
    concurrent.set(replacement);
    release.set_value();
    if (pending.get() != replacement || concurrent.get() != replacement)
        return 12;
    // Multiple first readers share the system Lazy's single result.
    std::atomic<int> factories = 0;
    options.system_proxy = [&] {
        ++factories;
        return first;
    };
    ConfigHttpDefaultProxy readers(options);
    std::vector<std::future<std::shared_ptr<ConfigHttpProxy>>> pending_readers;
    for (int i = 0; i < 8; ++i)
        pending_readers.push_back(std::async(std::launch::async, [&] { return readers.get(); }));
    for (auto& reader : pending_readers)
        if (reader.get() != first)
            return 14;
    if (factories != 1)
        return 15;
    options.platform = ConfigHttpSystemProxyPlatform::Linux;
    options.system_proxy = []() -> std::shared_ptr<ConfigHttpProxy> {
        throw std::logic_error("Linux queried OS proxy");
    };
    if (!std::dynamic_pointer_cast<ConfigHttpNoProxy>(config_http_construct_default_proxy(options)))
        return 16;
    options.platform = ConfigHttpSystemProxyPlatform::MacOS;
    options.system_proxy = [&] { return replacement; };
    if (config_http_construct_default_proxy(options) != replacement)
        return 17;
    options.system_proxy = {};
    bool missing = false;
    try {
        config_http_construct_default_proxy(options);
    } catch (const ConfigHttpProxyProviderError&) {
        missing = true;
    }
    if (!missing)
        return 18;
    first->credentials({});
    if (first->credentials())
        return 19;
    for (auto text : {u"http://example.com/", u"https://example.com/", u"socks5://localhost/", u"relative"}) {
        auto uri = native_config_uri_object_backend()->create(nini_writer_text(text),
                                                              ConfigUriObjectKind::RelativeOrAbsolute);
        if (first->get_proxy(uri) || !first->is_bypassed(uri))
            return 13;
    }
    std::cout << "PASS HttpNoProxy and HttpClient.DefaultProxy value semantics\n";
}
