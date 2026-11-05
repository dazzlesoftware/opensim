#include "opensim/framework/config_http_default_proxy.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/config_http_windows_proxy.hpp"
namespace opensim::framework {
std::shared_ptr<ConfigHttpProxy>
config_http_construct_default_proxy(const ConfigHttpDefaultProxyOptions &options) {
    auto env = options.environment ? options.environment : native_config_http_environment_proxy_backend();
    auto platform = options.platform == ConfigHttpSystemProxyPlatform::Windows
                        ? ConfigHttpEnvironmentProxyPlatform::Windows
                        : ConfigHttpEnvironmentProxyPlatform::Unix;
    if (auto proxy = ConfigHttpEnvironmentProxy::try_create(env, platform))
        return proxy;
    if (options.platform != ConfigHttpSystemProxyPlatform::Linux) {
        if (options.system_proxy) {
            if (auto proxy = options.system_proxy())
                return proxy;
        } else {
#ifdef _WIN32
            if (options.platform == ConfigHttpSystemProxyPlatform::Windows) {
                if (auto proxy = native_config_http_windows_proxy())
                    return proxy;
            } else
#endif
                throw ConfigHttpProxyProviderError(
                    "Default proxy requires the native OS proxy/PAC provider on this platform");
        }
    }
    return std::make_shared<ConfigHttpNoProxy>();
}
ConfigHttpDefaultProxy::ConfigHttpDefaultProxy(ConfigHttpDefaultProxyOptions options)
    : options_(std::move(options)) {}
std::shared_ptr<ConfigHttpProxy> ConfigHttpDefaultProxy::get() {
    if (auto value = value_.load())
        return value;
    std::shared_ptr<ConfigHttpProxy> candidate;
    {
        // SystemProxyInfo owns a separate execution-and-publication Lazy. The public
        // HttpClient setter must remain usable while its factory is running.
        std::lock_guard lock(mutex_);
        if (failure_)
            std::rethrow_exception(failure_);
        if (!attempted_) {
            attempted_ = true;
            try {
                system_value_ = config_http_construct_default_proxy(options_);
            } catch (...) {
                failure_ = std::current_exception();
                throw;
            }
        }
        candidate = system_value_;
    }
    std::shared_ptr<ConfigHttpProxy> empty;
    if (value_.compare_exchange_strong(empty, candidate))
        return candidate;
    return empty;
}
void ConfigHttpDefaultProxy::set(std::shared_ptr<ConfigHttpProxy> value) {
    if (!value)
        throw ConfigUriComponentsError("System.ArgumentNullException",
                                       u"Value cannot be null. (Parameter 'value')", 0x80004003, u"value");
    value_.store(std::move(value));
}
std::shared_ptr<ConfigHttpDefaultProxy> native_config_http_default_proxy_selector() {
    static auto selector = std::make_shared<ConfigHttpDefaultProxy>(ConfigHttpDefaultProxyOptions{});
    return selector;
}

}
