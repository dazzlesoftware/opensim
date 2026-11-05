// Test-process-only WinHTTP response injector; never installed as a system DLL.
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <optional>
struct Options {
    DWORD flags, detect;
    LPCWSTR config;
    LPVOID reserved1;
    DWORD reserved2;
    BOOL login;
};
struct Info {
    DWORD access;
    LPWSTR proxy, bypass;
};
struct Call {
    std::wstring url;
    std::optional<std::wstring> config;
    DWORD flags, detect;
    BOOL login;
};
static bool settings_ok = true, detect = false, open_ok = true;
static int opens = 0, closes = 0;
static std::optional<std::wstring> auto_url, manual, manual_bypass;
static std::vector<Call> calls;
static int sequence[3]{}, index = 0;
static std::optional<std::wstring> proxy, bypass;
static LPWSTR allocate(const std::optional<std::wstring>& text) {
    if (!text)
        return nullptr;
    auto bytes = (text->size() + 1) * sizeof(wchar_t);
    auto out = static_cast<LPWSTR>(LocalAlloc(LMEM_FIXED, bytes));
    if (out)
        CopyMemory(out, text->c_str(), bytes);
    return out;
}
extern "C" __declspec(dllexport) void WINAPI Configure(int a, int b, int c, LPCWSTR p, LPCWSTR q) {
    calls.clear();
    index = 0;
    sequence[0] = a;
    sequence[1] = b;
    sequence[2] = c;
    proxy = p ? std::optional<std::wstring>(p) : std::nullopt;
    bypass = q ? std::optional<std::wstring>(q) : std::nullopt;
}
extern "C" __declspec(dllexport) LPVOID WINAPI WinHttpOpen(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) {
    ++opens;
    return open_ok ? reinterpret_cast<LPVOID>(1) : nullptr;
}
extern "C" __declspec(dllexport) BOOL WINAPI WinHttpCloseHandle(LPVOID) {
    ++closes;
    return TRUE;
}
extern "C" __declspec(dllexport) BOOL WINAPI WinHttpGetProxyForUrl(LPVOID, LPCWSTR url, Options* options,
                                                                   Info* info) {
    calls.push_back({url, options->config ? std::optional<std::wstring>(options->config) : std::nullopt,
                     options->flags, options->detect, options->login});
    int code = sequence[index < 3 ? index++ : 2];
    *info = {1, nullptr, nullptr};
    if (!code) {
        info->access = proxy ? 3 : 1;
        info->proxy = allocate(proxy);
        info->bypass = allocate(bypass);
    }
    SetLastError(code);
    return code == 0;
}
extern "C" __declspec(dllexport) int WINAPI CallCount() {
    return static_cast<int>(calls.size());
}
extern "C" __declspec(dllexport) unsigned WINAPI CallNumber(int i, int field) {
    return field == 0 ? calls.at(i).flags : field == 1 ? calls.at(i).detect : calls.at(i).login;
}
extern "C" __declspec(dllexport) LPCWSTR WINAPI CallText(int i, int field) {
    auto& c = calls.at(i);
    return field == 0 ? c.url.c_str() : c.config ? c.config->c_str() : nullptr;
}

struct Settings {
    BOOL detect;
    LPWSTR url, proxy, bypass;
};
extern "C" __declspec(dllexport) void WINAPI ConfigureSettings(int success, int autodetect, LPCWSTR url,
                                                               LPCWSTR proxy, LPCWSTR bypass, int valid) {
    settings_ok = success != 0;
    detect = autodetect != 0;
    auto_url = url ? std::optional<std::wstring>(url) : std::nullopt;
    manual = proxy ? std::optional<std::wstring>(proxy) : std::nullopt;
    manual_bypass = bypass ? std::optional<std::wstring>(bypass) : std::nullopt;
    open_ok = valid != 0;
    opens = closes = 0;
}
extern "C" __declspec(dllexport) BOOL WINAPI WinHttpGetIEProxyConfigForCurrentUser(Settings* info) {
    *info = {};
    if (!settings_ok) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    info->detect = detect;
    info->url = allocate(auto_url);
    info->proxy = allocate(manual);
    info->bypass = allocate(manual_bypass);
    return TRUE;
}
extern "C" __declspec(dllexport) int WINAPI OpenCount() {
    return opens;
}
extern "C" __declspec(dllexport) int WINAPI CloseCount() {
    return closes;
}
