#include "opensim/framework/config_http_environment_proxy.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/nini_writer.hpp"
#include <algorithm>
namespace opensim::framework {
namespace {
using Text = ConfigUriText;
using Object = ConfigUriObjectPtr;
using Backend = std::shared_ptr<ConfigHttpEnvironmentProxyBackend>;
using Credential = std::shared_ptr<ConfigXmlCredentials>;
Text t(std::u16string value) {
    return nini_writer_text(std::move(value));
}
[[noreturn]] void nr() {
    throw ConfigUriComponentsError("System.NullReferenceException",
                                   u"Object reference not set to an instance of an object.", 0x80004003);
}
void require(const Backend& b) {
    if (!b || !b->environment || !b->new_builder || !b->absolute_uri || !b->unescape_data ||
        !b->escape_data || !b->user_info || !b->host || !b->uri_equal || !b->ordinal_equal ||
        !b->ordinal_ends_with || !b->white_space || !b->is_digit)
        throw ConfigHttpProxyProviderError(
            "HTTP environment proxy requires complete environment, URI, data encoding and character providers");
}
bool empty(const Text& text) {
    return !text || text->empty();
}
bool starts(std::u16string_view value, std::u16string_view prefix, const Backend& b) {
    return value.size() >= prefix.size() && b->ordinal_equal(value.substr(0, prefix.size()), prefix);
}
Credential parse_credential(Text value, const Backend& b) {
    if (empty(value) || std::all_of(value->begin(), value->end(), b->white_space))
        return {};
    if (*value == u":") {
        if (!b->default_network_credentials)
            throw ConfigHttpProxyProviderError("HTTP proxy default NetworkCredential provider is required");
        auto result = b->default_network_credentials();
        if (!result)
            throw ConfigHttpProxyProviderError("Default NetworkCredential provider returned null");
        return result;
    }
    value = b->unescape_data(value);
    if (!value)
        throw ConfigHttpProxyProviderError("UnescapeDataString provider returned null");
    Text password = t(u""), domain;
    auto colon = value->find(u':');
    if (colon != std::u16string::npos) {
        password = t(value->substr(colon + 1));
        value = t(value->substr(0, colon));
    }
    auto slash = value->find(u'\\');
    if (slash != std::u16string::npos) {
        domain = t(value->substr(0, slash));
        value = t(value->substr(slash + 1));
    }
    if (!b->network_credential)
        throw ConfigHttpProxyProviderError("HTTP proxy NetworkCredential provider is required");
    auto result = b->network_credential(value, password, domain);
    if (!result)
        throw ConfigHttpProxyProviderError("NetworkCredential constructor provider returned null");
    return result;
}
}
ConfigUriObjectPtr config_http_environment_proxy_uri(Text input, Backend b) {
    require(b);
    if (empty(input))
        return {};
    auto value = *input;
    std::size_t prefix = 0;
    Text scheme = t(u"http");
    std::uint16_t port = 80;
    if (starts(value, u"http://", b))
        prefix = 7;
    else if (starts(value, u"https://", b)) {
        prefix = 8;
        scheme = t(u"https");
        port = 443;
    } else if (starts(value, u"socks4://", b)) {
        prefix = 9;
        scheme = t(u"socks4");
    } else if (starts(value, u"socks5://", b)) {
        prefix = 9;
        scheme = t(u"socks5");
    } else if (starts(value, u"socks4a://", b)) {
        prefix = 10;
        scheme = t(u"socks4a");
    }
    if (prefix)
        value.erase(0, prefix);
    Text user, password;
    auto at = value.rfind(u'@');
    if (at != std::u16string::npos) {
        auto info = t(value.substr(0, at));
        try {
            info = b->unescape_data(info);
        } catch (const ConfigHttpProxyProviderError&) {
            throw;
        } catch (...) {
        }
        if (!info)
            throw ConfigHttpProxyProviderError("UnescapeDataString provider returned null");
        value.erase(0, at + 1);
        auto colon = info->find(u':');
        if (colon == std::u16string::npos)
            user = info;
        else {
            user = t(info->substr(0, colon));
            password = t(info->substr(colon + 1));
        }
    }
    auto close = value.find(u']'), colon = value.rfind(u':');
    Text host;
    if (colon == std::u16string::npos || (close != std::u16string::npos && colon < close))
        host = t(value);
    else {
        host = t(value.substr(0, colon));
        std::size_t i = colon + 1;
        while (i < value.size() && b->is_digit(value[i]))
            ++i;
        if (i == colon + 1)
            return {};
        unsigned parsed = 0;
        for (auto p = colon + 1; p < i; ++p) {
            auto c = value[p];
            if (c < u'0' || c > u'9' || parsed > (65535u - (c - u'0')) / 10u)
                return {};
            parsed = parsed * 10u + (c - u'0');
        }
        port = static_cast<std::uint16_t>(parsed);
    }
    try {
        auto builder = b->new_builder(scheme, host, port);
        if (!builder)
            throw ConfigHttpProxyProviderError("HTTP proxy UriBuilder provider returned null");
        if (user)
            builder->user_name(b->escape_data(user));
        if (password)
            builder->password(b->escape_data(password));
        auto uri = builder->uri();
        if (!uri)
            throw ConfigHttpProxyProviderError("HTTP proxy UriBuilder returned null URI");
        if (user && password && user->empty() && password->empty()) {
            auto display = uri->to_string();
            auto first = display->find(u'/');
            auto second = first == std::u16string::npos ? first : display->find(u'/', first + 1);
            if (second != std::u16string::npos) {
                uri = b->absolute_uri(t(display->substr(0, first) + u"//:@" + display->substr(second + 1)));
                if (!uri)
                    throw ConfigHttpProxyProviderError("Absolute URI constructor provider returned null");
            }
        }
        return uri;
    } catch (const ConfigHttpProxyProviderError&) {
        throw;
    } catch (...) {
    }
    return {};
}
ConfigHttpEnvironmentProxyCredentials::ConfigHttpEnvironmentProxyCredentials(Object http, Credential hc,
                                                                             Object https, Credential sc,
                                                                             Backend b)
    : http_(std::move(http)), https_(std::move(https)), http_credential_(std::move(hc)),
      https_credential_(std::move(sc)), backend_(std::move(b)) {
    require(backend_);
}
std::shared_ptr<ConfigXmlCredentials> ConfigHttpEnvironmentProxyCredentials::get_credential(Object uri,
                                                                                            Text) const {
    if (!uri)
        return {};
    if (backend_->uri_equal(uri, http_))
        return http_credential_;
    if (backend_->uri_equal(uri, https_))
        return https_credential_;
    return {};
}
std::shared_ptr<ConfigHttpEnvironmentProxyCredentials>
ConfigHttpEnvironmentProxyCredentials::try_create(Object http, Object https, Backend b) {
    require(b);
    Credential hc, sc;
    if (http)
        hc = parse_credential(b->user_info(http), b);
    if (https)
        sc = parse_credential(b->user_info(https), b);
    if (!hc && !sc)
        return {};
    return std::make_shared<ConfigHttpEnvironmentProxyCredentials>(http, hc, https, sc, b);
}
ConfigHttpEnvironmentProxy::ConfigHttpEnvironmentProxy(Object http, Object https, Text bypass, Backend b)
    : http_(std::move(http)), https_(std::move(https)), backend_(std::move(b)) {
    credentials_.store(ConfigHttpEnvironmentProxyCredentials::try_create(http_, https_, backend_));
    if (bypass) {
        bypass_.emplace();
        for (std::size_t start = 0; start <= bypass->size();) {
            auto end = bypass->find(u',', start);
            if (end == std::u16string::npos)
                end = bypass->size();
            auto left = start, right = end;
            while (left < right && backend_->white_space((*bypass)[left]))
                ++left;
            while (right > left && backend_->white_space((*bypass)[right - 1]))
                --right;
            if (right > left)
                bypass_->push_back(t(bypass->substr(left, right - left)));
            if (end == bypass->size())
                break;
            start = end + 1;
        }
    }
}
std::shared_ptr<ConfigHttpEnvironmentProxy>
ConfigHttpEnvironmentProxy::try_create(Backend b, ConfigHttpEnvironmentProxyPlatform platform) {
    require(b);
    Object http, https;
    auto env = [&](const char16_t* name) { return b->environment(t(name)); };
    auto parse = [&](const char16_t* name) { return config_http_environment_proxy_uri(env(name), b); };
    if (platform == ConfigHttpEnvironmentProxyPlatform::Unix) {
        http = parse(u"http_proxy");
        if (!http && !env(u"GATEWAY_INTERFACE"))
            http = parse(u"HTTP_PROXY");
        https = parse(u"https_proxy");
        if (!https)
            https = parse(u"HTTPS_PROXY");
    } else {
        if (!env(u"GATEWAY_INTERFACE"))
            http = parse(u"HTTP_PROXY");
        https = parse(u"HTTPS_PROXY");
    }
    if (!http || !https) {
        Object all;
        if (platform == ConfigHttpEnvironmentProxyPlatform::Unix)
            all = parse(u"all_proxy");
        if (!all)
            all = parse(u"ALL_PROXY");
        if (!http)
            http = all;
        if (!https)
            https = all;
    }
    if (!http && !https)
        return {};
    Text bypass;
    if (platform == ConfigHttpEnvironmentProxyPlatform::Unix)
        bypass = env(u"no_proxy");
    if (!bypass)
        bypass = env(u"NO_PROXY");
    return std::shared_ptr<ConfigHttpEnvironmentProxy>(
        new ConfigHttpEnvironmentProxy(http, https, bypass, b));
}
ConfigUriObjectPtr ConfigHttpEnvironmentProxy::get_proxy(Object uri) const {
    if (!uri)
        nr();
    auto scheme = uri->scheme();
    if (scheme && (backend_->ordinal_equal(*scheme, u"http") || backend_->ordinal_equal(*scheme, u"ws")))
        return http_;
    return https_;
}
bool ConfigHttpEnvironmentProxy::is_bypassed(Object uri) const {
    if (!get_proxy(uri))
        return true;
    if (bypass_)
        for (auto entry : *bypass_) {
            if ((*entry)[0] == u'.') {
                if (entry->size() - 1 == backend_->host(uri)->size()) {
                    auto host = backend_->host(uri);
                    auto length = backend_->host(uri)->size();
                    if (backend_->ordinal_equal(std::u16string_view(*entry).substr(1, length),
                                                std::u16string_view(*host).substr(0, length)))
                        return true;
                }
                if (backend_->ordinal_ends_with(*backend_->host(uri), *entry))
                    return true;
            } else if (backend_->ordinal_equal(*entry, *backend_->host(uri)))
                return true;
        }
    return false;
}
}
