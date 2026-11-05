// .NET8 pal_collation.c SimpleAffix_Iterators; MIT source license:
// native-port/third_party/dotnet_collation/LICENSE.
#include "opensim/framework/configuration_culture.hpp"
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace opensim::framework {
namespace {
struct Icu {
    using Open = void* (*)(const char*, std::int32_t*);
    using Close = void (*)(void*);
    using Elements = void* (*)(const void*, const char16_t*, std::int32_t, std::int32_t*);
    using Next = std::int32_t (*)(void*, std::int32_t*);
    Open open = nullptr;
    Close close = nullptr, close_elements = nullptr;
    Elements elements = nullptr;
    Next next = nullptr;
#ifdef _WIN32
    HMODULE module = nullptr;
    void* symbol(const std::string& n) {
        return reinterpret_cast<void*>(GetProcAddress(module, n.c_str()));
    }
#else
    void* module = nullptr;
    void* symbol(const std::string& n) {
        return dlsym(module, n.c_str());
    }
#endif
    Icu() {
#ifdef _WIN32
        module = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#elif defined(__APPLE__)
        module = dlopen("/usr/lib/libicucore.A.dylib", RTLD_LAZY | RTLD_LOCAL);
#else
        module = dlopen("libicui18n.so", RTLD_LAZY | RTLD_LOCAL);
        for (int n = 120; !module && n >= 40; --n)
            module = dlopen(("libicui18n.so." + std::to_string(n)).c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
        if (!module)
            return;
        std::string suffix;
        if (!symbol("ucol_open"))
            for (int n = 120; n >= 40; --n)
                if (symbol("ucol_open_" + std::to_string(n))) {
                    suffix = "_" + std::to_string(n);
                    break;
                }
        open = reinterpret_cast<Open>(symbol("ucol_open" + suffix));
        close = reinterpret_cast<Close>(symbol("ucol_close" + suffix));
        elements = reinterpret_cast<Elements>(symbol("ucol_openElements" + suffix));
        close_elements = reinterpret_cast<Close>(symbol("ucol_closeElements" + suffix));
        next = reinterpret_cast<Next>(symbol("ucol_next" + suffix));
    }
    ~Icu() {
#ifdef _WIN32
        if (module)
            FreeLibrary(module);
#else
        if (module)
            dlclose(module);
#endif
    }
};
Icu& icu() {
    static Icu a;
    return a;
}
struct Collator {
    void* value = nullptr;
    Collator() {
        auto& a = icu();
        if (!a.open || !a.close || !a.elements || !a.close_elements || !a.next)
            throw std::runtime_error("Selected ICU is required for invariant configuration Include- prefix");
        std::int32_t status = 0;
        value = a.open("", &status);
        if (status > 0 || !value) {
            if (value)
                a.close(value);
            throw std::runtime_error("Cannot create invariant configuration collator");
        }
    }
    ~Collator() {
        icu().close(value);
    }
};
struct Iterator {
    void* value = nullptr;
    Iterator(void* collator, const char16_t* s, std::int32_t n) {
        std::int32_t status = 0;
        value = icu().elements(collator, s, n, &status);
        if (status > 0 || !value) {
            if (value)
                icu().close_elements(value);
            throw std::runtime_error("Cannot create invariant configuration collation iterator");
        }
    }
    ~Iterator() {
        icu().close_elements(value);
    }
    std::int32_t next() {
        std::int32_t status = 0;
        auto n = icu().next(value, &status);
        if (status > 0)
            throw std::runtime_error("Invariant configuration collation iteration failed");
        return n;
    }
};
}
bool startup_invariant_configuration_include_key(ConfigurationCultureText text) {
    if (!text)
        throw ConfigurationCultureNullError();
    if (text->empty())
        return false;
    if (text->size() > std::size_t(std::numeric_limits<std::int32_t>::max()))
        throw std::length_error("Configuration Include- key exceeds Int32 length");
    thread_local Collator collator;
    static constexpr char16_t prefix[] = u"Include-";
    Iterator pattern(collator.value, prefix, 8),
        source(collator.value, text->data(), std::int32_t(text->size()));
    bool move_pattern = true, move_source = true;
    std::int32_t p = 0, s = 0;
    for (;;) {
        if (move_pattern)
            p = pattern.next();
        if (move_source)
            s = source.next();
        move_pattern = move_source = true;
        if (p == -1) {
            if (s == -1 || s == 0)
                return true;
            if ((std::uint32_t(s) & 0xffff0000u) == 0 && (std::uint32_t(s) & 0xff00u) != 0)
                return false;
            return true;
        }
        if (p == 0)
            move_source = false;
        else if (s == 0)
            move_pattern = false;
        else if (p != s)
            return false;
    }
}
}
