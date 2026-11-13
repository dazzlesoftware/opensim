// Source SystemProxyInfo/Lazy<IWebProxy> and HttpClient.DefaultProxy, .NET8.0.31.
#pragma once
#include "opensim/framework/atomic_shared_ptr.hpp"
#include "opensim/framework/config_http_environment_proxy_native.hpp"
#include <mutex>
namespace opensim::framework {
enum class ConfigHttpSystemProxyPlatform { Windows, Linux, MacOS };
class ConfigHttpNoProxy final : public ConfigHttpProxy {
    opensim::framework::AtomicSharedPtr<ConfigXmlCredentials> credentials_;

  public:
    ConfigUriObjectPtr get_proxy(ConfigUriObjectPtr) const override {
        return {};
    }
    bool is_bypassed(ConfigUriObjectPtr) const override {
        return true;
    }
    std::shared_ptr<ConfigXmlCredentials> credentials() const override {
        return credentials_.load();
    }
    void credentials(std::shared_ptr<ConfigXmlCredentials> value) override {
        credentials_.store(std::move(value));
    }
};
struct ConfigHttpDefaultProxyOptions {
    std::shared_ptr<ConfigHttpEnvironmentProxyBackend> environment;
    ConfigHttpSystemProxyPlatform platform =
#ifdef _WIN32
        ConfigHttpSystemProxyPlatform::Windows;
#elif defined(__APPLE__)
        ConfigHttpSystemProxyPlatform::MacOS;
#else
        ConfigHttpSystemProxyPlatform::Linux;
#endif
    // Optional OS override. Native Windows provider is used after environment
    // misses; macOS still requires its explicit OS provider.
    std::function<std::shared_ptr<ConfigHttpProxy>()> system_proxy;
};
std::shared_ptr<ConfigHttpProxy> config_http_construct_default_proxy(const ConfigHttpDefaultProxyOptions&);
class ConfigHttpDefaultProxy final {
    ConfigHttpDefaultProxyOptions options_;
    std::mutex mutex_;
    opensim::framework::AtomicSharedPtr<ConfigHttpProxy> value_;
    std::shared_ptr<ConfigHttpProxy> system_value_;
    bool attempted_ = false;
    std::exception_ptr failure_;

  public:
    explicit ConfigHttpDefaultProxy(ConfigHttpDefaultProxyOptions);
    std::shared_ptr<ConfigHttpProxy> get();
    void set(std::shared_ptr<ConfigHttpProxy>);
};
std::shared_ptr<ConfigHttpDefaultProxy> native_config_http_default_proxy_selector();

}
