#include "opensim/framework/config_http_windows_proxy.hpp"
#include "opensim/framework/config_http_default_proxy.hpp"
#include "opensim/framework/config_http_environment_proxy_native.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include <iostream>
#include <sstream>
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
struct Trace {
    int opens = 0, closes = 0, calls = 0;
};
struct Session : ConfigHttpWinHttpSession {
    std::shared_ptr<Trace> trace;
    std::vector<int> errors;
    ConfigUriText proxy, bypass;
    std::size_t current = 0;
    ~Session() {
        ++trace->closes;
    }
    ConfigHttpWinHttpProxyResult get_proxy_for_url(ConfigUriText,
                                                   const ConfigHttpWinHttpAutoOptions&) override {
        ++trace->calls;
        int code = errors[std::min(current++, errors.size() - 1)];
        if (code)
            return {false, code, {}};
        return {true, 0, {proxy ? 3u : 1u, proxy, bypass}};
    }
};
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--default") {
        ConfigHttpDefaultProxy selector({});
        auto proxy = selector.get();
        const char* kind = std::dynamic_pointer_cast<ConfigHttpWindowsProxy>(proxy)       ? "windows"
                           : std::dynamic_pointer_cast<ConfigHttpEnvironmentProxy>(proxy) ? "environment"
                           : std::dynamic_pointer_cast<ConfigHttpNoProxy>(proxy)          ? "none"
                                                                                          : "unknown";
        std::cout << kind << ":" << bool(proxy->credentials()) << "\n";
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--native") {
        auto proxy = ConfigHttpWindowsProxy::try_create();
        std::cout << bool(proxy) << "\n";
        if (proxy)
            proxy->dispose();
        return 0;
    }
    auto uris = *native_config_uri_object_backend();
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream in(row);
        std::string mode, url, manual, bypass, errors, pac, pacb, target;
        int success, detect, valid;
        in >> mode >> success >> detect >> url >> manual >> bypass >> valid >> errors >> pac >> pacb >>
            target;
        auto trace = std::make_shared<Trace>();
        ConfigHttpWindowsProxyOptions options;
        options.uris = uris;
        options.read_settings = [=] {
            return success ? ConfigHttpWinInetSettings{decode(url), decode(manual), decode(bypass),
                                                       bool(detect), true}
                           : ConfigHttpWinInetSettings{};
        };
        options.ticks = [] { return std::int32_t{1000}; };
        options.open_session = [=]() -> std::shared_ptr<ConfigHttpWinHttpSession> {
            ++trace->opens;
            if (!valid)
                return {};
            auto session = std::make_shared<Session>();
            session->trace = trace;
            session->proxy = decode(pac);
            session->bypass = decode(pacb);
            std::istringstream sequence(errors);
            std::string code;
            while (std::getline(sequence, code, ','))
                session->errors.push_back(std::stoi(code));
            return session;
        };
        auto native = native_config_http_windows_proxy_bypass_backend();
        native.local_addresses = [] {
            return std::vector<IpAddress>{*IpAddress::parse(std::string("192.0.2.10"))};
        };
        options.bypass = std::move(native);
        auto proxy = ConfigHttpWindowsProxy::try_create(options);
        if (!proxy) {
            std::cout << "NONE";
        } else {
            auto uri = target == "null"
                           ? ConfigUriObjectPtr{}
                           : uris.create(decode(target), ConfigUriObjectKind::RelativeOrAbsolute);
            bool initial = !proxy->credentials();
            auto credential = config_http_default_network_credentials();
            proxy->credentials(credential);
            if (mode == "D")
                proxy->dispose();
            std::cout << proxy->is_bypassed(uri) << ":" << initial << "|";
            for (int query = 0; query < 2; ++query) {
                if (query)
                    std::cout << "/";
                try {
                    if (mode == "M") {
                        auto list = proxy->get_multi_proxy(uri);
                        for (int i = 0; i < 100; ++i) {
                            auto next = list.read_next();
                            if (i)
                                std::cout << ",";
                            std::cout << next.found << ":" << next.is_final << ":" << list.position() << ":"
                                      << hex(next.uri ? next.uri->absolute_uri() : ConfigUriText{});
                            if (!next.found)
                                break;
                        }
                    } else {
                        auto result = proxy->get_proxy(uri);
                        std::cout << hex(result ? result->absolute_uri() : ConfigUriText{});
                    }
                } catch (const ConfigUriComponentsError& e) {
                    std::cout << "E:" << e.source_type << ":" << static_cast<std::int32_t>(e.hresult) << ":"
                              << hex(nini_writer_text(e.message));
                }
            }
            bool retained = proxy->credentials() == credential;
            proxy->credentials({});
            proxy->dispose();
            std::cout << "|" << retained << ":" << (!proxy->credentials());
        }
        std::cout << "#" << trace->opens << ":" << trace->closes << ":" << trace->calls << "\n";
    }
}
