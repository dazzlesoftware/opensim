#include "opensim/framework/config_http_windows_proxy.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
namespace opensim::framework {
namespace {
bool secure(ConfigUriObjectPtr uri) {
    if (!uri)
        throw ConfigUriComponentsError("System.NullReferenceException",
                                       u"Object reference not set to an instance of an object.", 0x80004003);
    auto scheme = uri->scheme();
    return *scheme == u"https" || *scheme == u"wss";
}
}
std::shared_ptr<ConfigHttpWindowsProxy>
ConfigHttpWindowsProxy::try_create(ConfigHttpWindowsProxyOptions options) {
    if (!options.read_settings)
        throw ConfigHttpProxyProviderError("Windows proxy requires current-user settings provider");
    auto helper = std::make_shared<ConfigHttpWinInetProxyHelper>(options.read_settings(), options.ticks);
    if (!helper->manual_settings_only() && !helper->auto_settings_used())
        return {};
    std::shared_ptr<ConfigHttpWinHttpSession> session;
    if (helper->auto_settings_used()) {
        if (!options.open_session)
            throw ConfigHttpProxyProviderError("Windows automatic proxy requires session factory");
        session = options.open_session();
        if (!session)
            return {};
    }
    return std::shared_ptr<ConfigHttpWindowsProxy>(
        new ConfigHttpWindowsProxy(std::move(helper), std::move(session), std::move(options)));
}
ConfigHttpWindowsProxy::ConfigHttpWindowsProxy(std::shared_ptr<ConfigHttpWinInetProxyHelper> helper,
                                               std::shared_ptr<ConfigHttpWinHttpSession> session,
                                               ConfigHttpWindowsProxyOptions options)
    : helper_(std::move(helper)), session_(std::move(session)), uris_(std::move(options.uris)),
      cache_(std::make_shared<ConfigHttpFailedProxyCache>()) {
    if (!uris_.try_create)
        uris_ = *native_config_uri_object_backend();
    if (!helper_->manual_settings_used())
        return;
    secure_ = ConfigHttpMultiProxyCandidates::parse(helper_->settings().proxy, true, uris_);
    insecure_ = ConfigHttpMultiProxyCandidates::parse(helper_->settings().proxy, false, uris_);
    bypass_ = std::make_unique<ConfigHttpWindowsProxyBypass>(
        helper_->settings().proxy_bypass,
        options.bypass ? std::move(*options.bypass) : native_config_http_windows_proxy_bypass_backend());
}
ConfigHttpMultiProxy ConfigHttpWindowsProxy::empty() const {
    return {ConfigHttpMultiProxyCandidates::parse({}, false, uris_), cache_};
}
ConfigHttpMultiProxy ConfigHttpWindowsProxy::get_multi_proxy(ConfigUriObjectPtr uri) const {
    if (helper_->auto_settings_used() && !helper_->recent_auto_detection_failure()) {
        auto session = session_.load();
        if (!session && helper_->settings().use_proxy) {
            if (uri)
                uri->absolute_uri();
            throw ConfigUriComponentsError("System.NullReferenceException",
                                           u"Object reference not set to an instance of an object.",
                                           0x80004003);
        }
        auto result = helper_->get_proxy_for_url(std::move(session), uri);
        if (!result.success)
            return empty();
        if (!result.info.proxy_bypass) {
            if (result.info.proxy)
                return {ConfigHttpMultiProxyCandidates::lazy(result.info.proxy, secure(uri), uris_), cache_};
            return empty();
        }
    }
    if (helper_->manual_settings_used()) {
        if (bypass_ && bypass_->bypasses(uri))
            return empty();
        return {secure(uri) ? secure_ : insecure_, cache_};
    }
    return empty();
}
ConfigUriObjectPtr ConfigHttpWindowsProxy::get_proxy(ConfigUriObjectPtr uri) const {
    auto proxies = get_multi_proxy(std::move(uri));
    return proxies.read_next().uri;
}
void ConfigHttpWindowsProxy::dispose() {
    if (!disposed_.exchange(true))
        session_.store({});
}
std::shared_ptr<ConfigHttpProxy> native_config_http_windows_proxy() {
    return ConfigHttpWindowsProxy::try_create();
}
}
