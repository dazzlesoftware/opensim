#include "opensim/framework/config_http_wininet_proxy_helper.hpp"
#include <bit>
#include "opensim/framework/config_uri_components.hpp"
namespace opensim::framework {
namespace {
bool nonempty(ConfigUriText text) { return text && !text->empty(); }
}
ConfigHttpWinInetProxyHelper::ConfigHttpWinInetProxyHelper(ConfigHttpWinInetSettings settings,
                                                           std::function<std::int32_t()> ticks)
    : settings_(std::move(settings)), ticks_(std::move(ticks)) {
    if (!ticks_)
        throw ConfigHttpProxyProviderError("WinInet proxy policy requires TickCount provider");
}
bool ConfigHttpWinInetProxyHelper::auto_settings_used() const {
    return settings_.auto_detect || nonempty(settings_.auto_config_url);
}
bool ConfigHttpWinInetProxyHelper::manual_settings_used() const { return nonempty(settings_.proxy); }
bool ConfigHttpWinInetProxyHelper::recent_auto_detection_failure() const {
    if (!auto_failed_.load())
        return false;
    auto elapsed = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(ticks_()) -
                                               std::bit_cast<std::uint32_t>(last_failure_.load()));
    return elapsed <= 120000;
}
ConfigHttpWinHttpProxyResult
ConfigHttpWinInetProxyHelper::get_proxy_for_url(std::shared_ptr<ConfigHttpWinHttpSession> session,
                                                ConfigUriObjectPtr uri) {
    ConfigHttpWinHttpProxyResult result;
    if (!settings_.use_proxy)
        return result;
    if (!session)
        throw ConfigHttpProxyProviderError("WinInet automatic lookup requires a WinHTTP session");
    ConfigHttpWinHttpAutoOptions options{settings_.auto_config_url,
                                         (settings_.auto_detect ? 1u : 0u) |
                                             (nonempty(settings_.auto_config_url) ? 2u : 0u),
                                         settings_.auto_detect ? 3u : 0u, false};
    if (!uri)
        throw ConfigUriComponentsError("System.NullReferenceException",
                                       u"Object reference not set to an instance of an object.", 0x80004003);
    auto url = uri->absolute_uri();
    auto scheme = uri->scheme();
    if (*scheme == u"wss")
        url = nini_writer_text(u"https" + url->substr(3));
    else if (*scheme == u"ws")
        url = nini_writer_text(u"http" + url->substr(2));
    bool retried = false;
    do {
        auto_failed_.store(false);
        result = session->get_proxy_for_url(url, options);
        if (result.success)
            break;
        if (result.error == 12015) {
            if (retried)
                break;
            retried = true;
            options.auto_login = true;
            continue;
        }
        if (result.error == 12180) {
            auto_failed_.store(true);
            last_failure_.store(ticks_());
        }
        break;
    } while (retried);
    if (!result.success && nonempty(settings_.proxy)) {
        result.success = true;
        result.info = {3, settings_.proxy,
                       nonempty(settings_.proxy_bypass) ? settings_.proxy_bypass : ConfigUriText{}};
    }
    return result;
}
}
