#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/config_http_multi_proxy.hpp"
#include "opensim/framework/config_http_environment_proxy_native.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include <iostream>
#include <sstream>
#include <limits>
#include <algorithm>
namespace opensim::framework {
struct ConfigHttpFailedProxyCacheFixture {
    static void seed(ConfigHttpFailedProxyCache& c, ConfigUriObjectPtr uri, std::int64_t ticks) {
        std::lock_guard lock(c.mutex_);
        for (auto& e : c.entries_)
            if (c.backend_.equal(e.uri, uri)) {
                e.ticks = ticks;
                return;
            }
        c.entries_.push_back({uri, ticks});
    }
    static auto entries(ConfigHttpFailedProxyCache& c) {
        std::lock_guard lock(c.mutex_);
        return c.entries_;
    }
    static void flush_at(ConfigHttpFailedProxyCache& c, std::int64_t tick) {
        c.next_flush_.store(tick);
    }
    static auto flush_at(ConfigHttpFailedProxyCache& c) {
        return c.next_flush_.load();
    }
};
}
using namespace opensim::framework;
std::u16string decode(std::string s) {
    std::u16string t;
    for (std::size_t i = 0; i < s.size(); i += 4)
        t += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return t;
}
std::string hex(std::u16string_view s) {
    std::string t;
    for (auto c : s)
        for (int n = 12; n >= 0; n -= 4)
            t += "0123456789abcdef"[(c >> n) & 15];
    return t;
}
auto maximum = std::numeric_limits<std::int64_t>::max();
int unit() {
    auto backend = *native_config_uri_object_backend();
    auto uri = backend.create(nini_writer_text(u"http://proxy/"), ConfigUriObjectKind::Absolute);
    std::int64_t clock = 1000;
    auto options = native_config_http_failed_proxy_cache_backend();
    options.ticks = [&] { return clock; };
    ConfigHttpFailedProxyCache cache(options);
    if (cache.get_proxy_renew_ticks(uri) != 0)
        return 1;
    cache.set_proxy_failed(uri);
    if (cache.get_proxy_renew_ticks(uri) != 1801000)
        return 2;
    if (cache.try_renew_proxy(uri, 1800999))
        return 3;
    if (!cache.try_renew_proxy(uri, 1801000) || cache.try_renew_proxy(uri, 1801000))
        return 4;
    cache.set_proxy_failed(uri);
    clock = 1801000;
    if (cache.get_proxy_renew_ticks(uri) != 0)
        return 5;
    for (int i = 0; i < 8; ++i) {
        auto text = std::string("http://p") + std::to_string(i) + "/";
        ConfigHttpFailedProxyCacheFixture::seed(
            cache,
            backend.create(nini_writer_text(std::u16string(text.begin(), text.end())),
                           ConfigUriObjectKind::Absolute),
            1);
    }
    ConfigHttpFailedProxyCacheFixture::flush_at(cache, 0);
    if (cache.get_proxy_renew_ticks(uri) != 0 ||
        ConfigHttpFailedProxyCacheFixture::entries(cache).size() != 8)
        return 6;
    cache.set_proxy_failed(uri);
    if (ConfigHttpFailedProxyCacheFixture::entries(cache).size() != 1 ||
        ConfigHttpFailedProxyCacheFixture::flush_at(cache) != clock + 300000)
        return 7;
    for (int method = 0; method < 3; ++method) {
        bool rejected = false;
        try {
            if (method == 0)
                cache.get_proxy_renew_ticks({});
            else if (method == 1)
                cache.set_proxy_failed({});
            else
                cache.try_renew_proxy({}, 0);
        } catch (const ConfigUriComponentsError& e) {
            rejected = e.source_type == "System.ArgumentNullException" &&
                       e.parameter == std::optional<std::u16string>(method == 2 ? u"item" : u"key") &&
                       e.hresult == 0x80004003;
        }
        if (!rejected)
            return 8;
    }
    auto alias =
        backend.create(nini_writer_text(u"http://PROXY:80/#fragment"), ConfigUriObjectKind::Absolute);
    cache.set_proxy_failed(alias);
    if (ConfigHttpFailedProxyCacheFixture::entries(cache).size() != 1)
        return 9;
    auto old = cache.get_proxy_renew_ticks(uri);
    ConfigHttpFailedProxyCacheFixture::seed(cache, uri, old + 1);
    if (cache.try_renew_proxy(uri, old) || cache.get_proxy_renew_ticks(uri) != old + 1)
        return 10;
    std::cout
        << "PASS FailedProxyCache lifetime, conditional renewal, cleanup threshold, URI equality and null keys\n";
    return 0;
}
int main(int argc, char**) {
    if (argc > 1)
        return unit();
    auto backend = *native_config_uri_object_backend();
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream in(row);
        int lazy, secure, mode;
        std::string encoded;
        in >> lazy >> secure >> mode >> encoded;
        ConfigUriText text =
            encoded == "null" ? ConfigUriText{} : nini_writer_text(encoded == "-" ? u"" : decode(encoded));
        auto options = native_config_http_failed_proxy_cache_backend();
        options.ticks = [] { return std::int64_t{1000}; };
        auto cache = std::make_shared<ConfigHttpFailedProxyCache>(options);
        ConfigHttpFailedProxyCacheFixture::flush_at(*cache, maximum);
        auto seed = ConfigHttpMultiProxyCandidates::parse(text, secure, backend);
        for (int i = 0;; ++i) {
            auto next = seed.next();
            if (!next.found)
                break;
            if (mode == 1 && i != 0)
                continue;
            if (mode) {
                auto tick = mode == 4 ? 1 : mode == 5 ? maximum : maximum - 1000 + (mode == 3 ? 0 : i);
                ConfigHttpFailedProxyCacheFixture::seed(*cache, next.uri, tick);
            }
        }
        ConfigHttpMultiProxy plan(lazy ? ConfigHttpMultiProxyCandidates::lazy(text, secure, backend)
                                       : ConfigHttpMultiProxyCandidates::parse(text, secure, backend),
                                  cache);
        for (int i = 0; i < 1024; ++i) {
            auto next = plan.read_next();
            if (i)
                std::cout << "|";
            std::cout << (next.found ? "1" : "0") << ":" << (next.is_final ? "1" : "0") << ":"
                      << plan.position() << ":" << (next.uri ? hex(*next.uri->absolute_uri()) : "-");
            if (!next.found)
                break;
        }
        auto entries = ConfigHttpFailedProxyCacheFixture::entries(*cache);
        std::sort(entries.begin(), entries.end(),
                  [](auto& a, auto& b) { return *a.uri->absolute_uri() < *b.uri->absolute_uri(); });
        std::cout << "#";
        for (auto& e : entries) {
            std::cout << hex(*e.uri->absolute_uri()) << ":";
            if (e.ticks >= maximum - 1000)
                std::cout << "S" << (maximum - e.ticks);
            else if (e.ticks == 1)
                std::cout << "E";
            else if (e.ticks == 1801000)
                std::cout << "F";
            else
                std::cout << "BAD" << e.ticks;
            std::cout << ",";
        }
        std::cout << "\n";
    }
}
