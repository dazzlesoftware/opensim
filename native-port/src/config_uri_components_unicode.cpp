// Native selected ICU IDN closure of DomainNameHelper.IdnEquivalent and
// SslStream TargetHostNameHelper. UIDNA policy: runtime v8.0.31 pal_idna.c,
// https://github.com/dotnet/runtime/blob/v8.0.31/src/native/libs/System.Globalization.Native/pal_idna.c
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/invariant_config_culture.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/file_system_name.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace opensim::framework {
namespace {
struct IdnaInfo {
    std::int16_t size = 16;
    std::int8_t different = 0, reserved = 0;
    std::uint32_t errors = 0;
    std::int32_t reserved2 = 0, reserved3 = 0;
};
struct Idna {
    using Open = void* (*)(std::uint32_t, std::int32_t*);
    using Close = void (*)(void*);
    using Convert = std::int32_t (*)(const void*, const char16_t*, std::int32_t, char16_t*, std::int32_t,
                                     IdnaInfo*, std::int32_t*);
    Open open = nullptr;
    Close close = nullptr;
    Convert convert = nullptr;
#ifdef _WIN32
    HMODULE module = nullptr;
    void* symbol(const std::string& s) {
        return reinterpret_cast<void*>(GetProcAddress(module, s.c_str()));
    }
#else
    void* module = nullptr;
    void* symbol(const std::string& s) {
        return dlsym(module, s.c_str());
    }
#endif
    Idna() {
#ifdef _WIN32
        module = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#elif defined(__APPLE__)
        module = dlopen("/usr/lib/libicucore.A.dylib", RTLD_LAZY | RTLD_LOCAL);
#else
        module = dlopen("libicuuc.so", RTLD_LAZY | RTLD_LOCAL);
        for (int i = 120; !module && i >= 40; --i)
            module = dlopen(("libicuuc.so." + std::to_string(i)).c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
        if (!module)
            return;
        std::string suffix;
        if (!symbol("uidna_openUTS46"))
            for (int i = 120; i >= 40; --i)
                if (symbol("uidna_openUTS46_" + std::to_string(i))) {
                    suffix = "_" + std::to_string(i);
                    break;
                }
        open = reinterpret_cast<Open>(symbol("uidna_openUTS46" + suffix));
        close = reinterpret_cast<Close>(symbol("uidna_close" + suffix));
        convert = reinterpret_cast<Convert>(symbol("uidna_nameToASCII" + suffix));
    }
    // ICU's normalization services own process-wide caches. Keep the selected
    // module resident, as the runtime globalization shim does; unloading its
    // globals would orphan those caches while other providers may still use it.
    std::u16string tls_ascii(std::u16string input, bool ascii_fallback = true) {
        auto safe = [](std::u16string_view s) {
            for (auto c : s)
                if (!((c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') ||
                      c == u'-' || c == u'.' || c == u'_'))
                    return false;
            return true;
        };
        auto invalid = [](std::u16string message) -> void {
            throw ConfigUriComponentsError("System.ArgumentException",
                                           std::move(message) + u" (Parameter 'unicode')", 0x80070057,
                                           u"unicode");
        };
        try {
            if (input.empty())
                invalid(u"IDN labels must be between 1 and 63 characters long.");
            if (input.back() == 0) {
                auto position = std::to_string(input.size() - 1);
                invalid(u"Invalid Unicode code point found at index " +
                        std::u16string(position.begin(), position.end()) + u".");
            }
            for (auto c : input)
                if (c <= 31 || c == 127)
                    invalid(u"Decoded string is not a valid IDN name.");
            if (!open || !close || !convert)
                throw std::runtime_error("Selected ICU UIDNA dependency is unavailable");
            std::int32_t error = 0;
            auto idna = open(8 | 16, &error);
            if (!idna || error > 0)
                throw std::runtime_error("Selected ICU UIDNA initialization failed");
            struct Guard {
                Close close;
                void* value;
                ~Guard() {
                    close(value);
                }
            } guard{close, idna};
            IdnaInfo info;
            std::u16string result(512, u'\0');
            auto count = convert(idna, input.data(), static_cast<std::int32_t>(input.size()), result.data(),
                                 static_cast<std::int32_t>(result.size()), &info, &error);
            if (error == 15) {
                error = 0;
                info = IdnaInfo{};
                result.resize(count);
                count = convert(idna, input.data(), static_cast<std::int32_t>(input.size()), result.data(),
                                count, &info, &error);
            }
            info.errors &= ~0x20u;
            if (error > 0 || info.errors || count <= 0)
                invalid(u"Decoded string is not a valid IDN name.");
            result.resize(count);
            const auto& casing = file_system_name::pinned_case_backend();
            if (input.size() == result.size() && casing.ordinal_ignore_case_ends_with(input, result))
                return input;
            return result;
        } catch (const ConfigUriComponentsError& e) {
            if (ascii_fallback && e.source_type == "System.ArgumentException" && safe(input))
                return input;
            throw;
        }
    }
    ConfigUriText ascii(ConfigUriText input) {
        bool ascii = true;
        for (auto c : *input)
            ascii &= c < 128;
        if (ascii)
            return invariant_config_lower(input);
        if (!open || !close || !convert)
            throw std::runtime_error("Selected ICU UIDNA dependency is unavailable");
        std::int32_t error = 0;
        auto idna = open(8 | 16, &error);
        if (!idna || error > 0)
            throw std::runtime_error("Selected ICU UIDNA initialization failed");
        IdnaInfo info;
        std::u16string result(512, u'\0');
        auto count = convert(idna, input->data(), static_cast<std::int32_t>(input->size()), result.data(),
                             static_cast<std::int32_t>(result.size()), &info, &error);
        if (error == 15) {
            error = 0;
            info = IdnaInfo{};
            result.resize(count);
            count = convert(idna, input->data(), static_cast<std::int32_t>(input->size()), result.data(),
                            count, &info, &error);
        }
        close(idna);
        info.errors &= ~0x20u;
        if (error > 0 || info.errors || count <= 0)
            throw ConfigUriComponentsError(
                "System.UriFormatException",
                u"An invalid Unicode character by IDN standards was specified in the host.", 0x80131537);
        result.resize(count);
        for (auto c : result)
            if (std::u16string_view(u"\\/?@#:[]").find(c) != std::u16string_view::npos)
                throw ConfigUriComponentsError(
                    "System.UriFormatException",
                    u"An invalid Unicode character by IDN standards was specified in the host.", 0x80131537);
        return std::make_shared<const std::u16string>(std::move(result));
    }
};
}
std::shared_ptr<ConfigUriComponentsBackend> native_config_uri_components_backend() {
    auto b = std::make_shared<ConfigUriComponentsBackend>();
    b->unicode = native_config_uri_unicode_backend();
    b->lower_invariant = invariant_config_lower;
    b->idn_ascii = [](auto s) {
        static Idna idna;
        return idna.ascii(std::move(s));
    };
    return b;
}
std::u16string config_http_normalize_tls_host(std::optional<std::u16string_view> input) {
    if (!input || input->empty())
        return {};
    std::u16string value(*input);
    while (!value.empty() && value.back() == u'.')
        value.pop_back();
    static Idna idna;
    return idna.tls_ascii(std::move(value));
}
std::u16string config_http_certificate_idn_ascii(std::u16string_view input) {
    static Idna idna;
    return idna.tls_ascii(std::u16string(input), false);
}
}
