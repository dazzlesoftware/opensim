#include "opensim/framework/config_http_wininet_proxy_helper.hpp"
#include <bit>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
namespace opensim::framework {
namespace {
ConfigUriText copy(const wchar_t* value) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return value ? nini_writer_text(std::u16string(reinterpret_cast<const char16_t*>(value)))
                 : ConfigUriText{};
}
struct Buffers {
    wchar_t *a = nullptr, *b = nullptr, *c = nullptr;
    ~Buffers() {
        if (a)
            LocalFree(a);
        if (b)
            LocalFree(b);
        if (c)
            LocalFree(c);
    }
};
class Session final : public ConfigHttpWinHttpSession {
    HINTERNET handle_;

  public:
    explicit Session(HINTERNET h) : handle_(h) {
    }
    ~Session() override {
        WinHttpCloseHandle(handle_);
    }
    ConfigHttpWinHttpProxyResult get_proxy_for_url(ConfigUriText uri,
                                                   const ConfigHttpWinHttpAutoOptions& options) override {
        std::wstring url(uri->begin(), uri->end()), config;
        if (options.auto_config_url)
            config.assign(options.auto_config_url->begin(), options.auto_config_url->end());
        WINHTTP_AUTOPROXY_OPTIONS native{};
        native.dwFlags = options.flags;
        native.dwAutoDetectFlags = options.auto_detect_flags;
        native.lpszAutoConfigUrl = options.auto_config_url ? config.c_str() : nullptr;
        native.fAutoLogonIfChallenged = options.auto_login;
        WINHTTP_PROXY_INFO info{WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr};
        BOOL success = WinHttpGetProxyForUrl(handle_, url.c_str(), &native, &info);
        int error = success ? 0 : static_cast<int>(GetLastError());
        Buffers release{info.lpszProxy, info.lpszProxyBypass};
        return {
            success != FALSE, error, {info.dwAccessType, copy(info.lpszProxy), copy(info.lpszProxyBypass)}};
    }
};
}
std::int32_t config_http_wininet_tick_count() {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(GetTickCount()));
}
ConfigHttpWinInetSettings native_config_http_wininet_settings() {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config{};
    BOOL success = WinHttpGetIEProxyConfigForCurrentUser(&config);
    Buffers release{config.lpszAutoConfigUrl, config.lpszProxy, config.lpszProxyBypass};
    if (!success)
        return {};
    return {copy(config.lpszAutoConfigUrl), copy(config.lpszProxy), copy(config.lpszProxyBypass),
            config.fAutoDetect != FALSE, true};
}
std::shared_ptr<ConfigHttpWinHttpSession> native_config_http_winhttp_session() {
    auto handle = WinHttpOpen(nullptr, WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, WINHTTP_FLAG_ASYNC);
    if (!handle)
        return {};
    try {
        return std::make_shared<Session>(handle);
    } catch (...) {
        WinHttpCloseHandle(handle);
        throw;
    }
}
}
#else
namespace opensim::framework {
std::int32_t config_http_wininet_tick_count() {
    auto value = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now().time_since_epoch())
                     .count();
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value));
}
ConfigHttpWinInetSettings native_config_http_wininet_settings() {
    throw ConfigHttpProxyProviderError("WinHTTP settings provider is Windows-only");
}
std::shared_ptr<ConfigHttpWinHttpSession> native_config_http_winhttp_session() {
    throw ConfigHttpProxyProviderError("WinHTTP session provider is Windows-only");
}
}
#endif
