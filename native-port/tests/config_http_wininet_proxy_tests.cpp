#include "opensim/framework/config_http_wininet_proxy_helper.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include <iostream>
#include <sstream>
#include <limits>
using namespace opensim::framework;
ConfigUriText decode(std::string s) {
    if (s == "null")
        return {};
    if (s == "-")
        return nini_writer_text(u"");
    std::u16string t;
    for (std::size_t i = 0; i < s.size(); i += 4)
        t += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return nini_writer_text(std::move(t));
}
std::string hex(ConfigUriText s) {
    if (!s)
        return "null";
    if (s->empty())
        return "-";
    std::string t;
    for (auto c : *s)
        for (int n = 12; n >= 0; n -= 4)
            t += "0123456789abcdef"[(c >> n) & 15];
    return t;
}
struct Session : ConfigHttpWinHttpSession {
    struct Call {
        ConfigUriText uri;
        ConfigHttpWinHttpAutoOptions options;
    };
    std::vector<Call> calls;
    std::vector<int> errors;
    ConfigUriText proxy, bypass;
    std::size_t index = 0;
    ConfigHttpWinHttpProxyResult get_proxy_for_url(ConfigUriText uri,
                                                   const ConfigHttpWinHttpAutoOptions& options) override {
        calls.push_back({uri, options});
        int code = errors.at(std::min(index++, errors.size() - 1));
        if (code)
            return {false, code, {}};
        return {true, 0, {proxy ? 3u : 1u, proxy, bypass}};
    }
};
int unit() {
    auto session = std::make_shared<Session>();
    session->errors = {12180};
    auto uri = native_config_uri_object_backend()->create(nini_writer_text(u"http://destination/"),
                                                          ConfigUriObjectKind::Absolute);
    std::int32_t clock = 1000;
    ConfigHttpWinInetSettings settings;
    settings.use_proxy = true;
    settings.auto_detect = true;
    ConfigHttpWinInetProxyHelper helper(settings, [&] { return clock; });
    helper.get_proxy_for_url(session, uri);
    if (!helper.recent_auto_detection_failure())
        return 1;
    clock = 121000;
    if (!helper.recent_auto_detection_failure())
        return 2;
    clock = 121001;
    if (helper.recent_auto_detection_failure())
        return 3;
    clock = std::numeric_limits<std::int32_t>::max() - 10;
    helper.get_proxy_for_url(session, uri);
    clock = std::numeric_limits<std::int32_t>::min() + 10;
    if (!helper.recent_auto_detection_failure())
        return 4;
    return 0;
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--unit")
        return unit();
    if (argc == 2 && std::string(argv[1]) == "--native") {
        auto s = native_config_http_wininet_settings();
        auto session = native_config_http_winhttp_session();
        std::cout << s.auto_detect << ":" << s.use_proxy << ":" << hex(s.auto_config_url) << ":"
                  << hex(s.proxy) << ":" << hex(s.proxy_bypass) << ":" << bool(session) << "\n";
        return 0;
    }
    if (argc == 6 && std::string(argv[1]) == "--live") {
        ConfigHttpWinInetSettings settings;
        settings.use_proxy = true;
        settings.auto_config_url =
            nini_writer_text(std::u16string(argv[2], argv[2] + std::char_traits<char>::length(argv[2])));
        settings.proxy = decode(argv[3]);
        settings.proxy_bypass = decode(argv[4]);
        ConfigHttpWinInetProxyHelper helper(settings);
        auto result = helper.get_proxy_for_url(
            native_config_http_winhttp_session(),
            native_config_uri_object_backend()->create(
                nini_writer_text(std::u16string(argv[5], argv[5] + std::char_traits<char>::length(argv[5]))),
                ConfigUriObjectKind::Absolute));
        std::cerr << "LAST:" << result.error << "\n";
        std::cout << result.success << ":" << result.info.access_type << ":" << hex(result.info.proxy) << ":"
                  << hex(result.info.proxy_bypass) << "|" << helper.recent_auto_detection_failure() << "\n";
        return 0;
    }
    auto uris = native_config_uri_object_backend();
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream in(row);
        int detect, use;
        std::string auto_url, proxy, bypass, uri, errors, pac, pac_bypass;
        in >> detect >> use >> auto_url >> proxy >> bypass >> uri >> errors >> pac >> pac_bypass;
        auto session = std::make_shared<Session>();
        std::istringstream sequence(errors);
        std::string code;
        while (std::getline(sequence, code, ','))
            session->errors.push_back(std::stoi(code));
        session->proxy = decode(pac);
        session->bypass = decode(pac_bypass);
        ConfigHttpWinInetSettings settings{decode(auto_url), decode(proxy), decode(bypass), bool(detect),
                                           bool(use)};
        ConfigHttpWinInetProxyHelper helper(settings, [] { return std::int32_t{1000}; });
        auto result =
            helper.get_proxy_for_url(session, uris->create(decode(uri), ConfigUriObjectKind::Absolute));
        std::cout << helper.auto_settings_used() << ":" << helper.manual_settings_used() << ":"
                  << helper.manual_settings_only() << "|" << result.success << ":" << result.info.access_type
                  << ":" << hex(result.info.proxy) << ":" << hex(result.info.proxy_bypass) << "|"
                  << helper.recent_auto_detection_failure() << "|";
        for (auto& call : session->calls)
            std::cout << hex(call.uri) << "," << call.options.flags << "," << call.options.auto_detect_flags
                      << "," << call.options.auto_login << "," << hex(call.options.auto_config_url) << ";";
        std::cout << "\n";
    }
}
