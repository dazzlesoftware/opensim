// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/unicode_regex.hpp"
#include "opensim/framework/text.hpp"
#include <cstdint>
#include <stdexcept>
#include <mutex>
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
struct IcuRegex {
    using Open = void* (*)(const char16_t*, int32_t, uint32_t, void*, int32_t*);
    using Close = void (*)(void*);
    using Text = void (*)(void*, const char16_t*, int32_t, int32_t*);
    using Find = int8_t (*)(void*, int32_t, int32_t*);
    using Count = int32_t (*)(void*, int32_t*);
    using Index = int32_t (*)(void*, int32_t, int32_t*);
    Open open = nullptr;
    Close close = nullptr;
    Text text = nullptr;
    Find find = nullptr;
    Count count = nullptr;
    Index start = nullptr, end = nullptr;
#ifdef _WIN32
    HMODULE module = nullptr;
    void* symbol(const std::string& name) {
        return reinterpret_cast<void*>(GetProcAddress(module, name.c_str()));
    }
#else
    void* module = nullptr;
    void* symbol(const std::string& name) {
        return dlsym(module, name.c_str());
    }
#endif
    IcuRegex() {
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
        if (!symbol("uregex_open"))
            for (int n = 120; n >= 40; --n)
                if (symbol("uregex_open_" + std::to_string(n))) {
                    suffix = "_" + std::to_string(n);
                    break;
                }
        open = reinterpret_cast<Open>(symbol("uregex_open" + suffix));
        close = reinterpret_cast<Close>(symbol("uregex_close" + suffix));
        text = reinterpret_cast<Text>(symbol("uregex_setText" + suffix));
        find = reinterpret_cast<Find>(symbol("uregex_find" + suffix));
        count = reinterpret_cast<Count>(symbol("uregex_groupCount" + suffix));
        start = reinterpret_cast<Index>(symbol("uregex_start" + suffix));
        end = reinterpret_cast<Index>(symbol("uregex_end" + suffix));
    }
    ~IcuRegex() {
#ifdef _WIN32
        if (module)
            FreeLibrary(module);
#else
        if (module)
            dlclose(module);
#endif
    }
};
IcuRegex& api() {
    static IcuRegex value;
    return value;
}
void checked(int32_t error) {
    if (error > 0)
        throw std::runtime_error("ICU regular expression failed");
}
}
struct UnicodeRegex::State {
    void* expression = nullptr;
    std::shared_ptr<std::u16string> text;
    std::mutex mutex;
    ~State() {
        if (expression)
            api().close(expression);
    }
};
UnicodeRegex::UnicodeRegex(const std::string& pattern) : state_(std::make_unique<State>()) {
    auto& p = api();
    if (!p.open || !p.close || !p.text || !p.find || !p.count || !p.start || !p.end)
        throw std::runtime_error("ICU regular expressions unavailable");
    auto units = utf16_replacing(pattern);
    int32_t error = 0;
    state_->expression = p.open(units.data(), static_cast<int32_t>(units.size()), 0, nullptr, &error);
    checked(error);
    if (!state_->expression)
        throw std::invalid_argument("Invalid regular expression");
}
UnicodeRegex::~UnicodeRegex() = default;
std::optional<std::vector<std::string>> UnicodeRegex::match(const std::string& text) {
    std::lock_guard lock(state_->mutex);
    auto& p = api();
    auto units = std::make_shared<std::u16string>(utf16_replacing(text));
    int32_t error = 0;
    p.text(state_->expression, units->data(), static_cast<int32_t>(units->size()), &error);
    state_->text = units;
    checked(error);
    auto found = p.find(state_->expression, 0, &error);
    checked(error);
    if (!found)
        return {};
    auto count = p.count(state_->expression, &error);
    checked(error);
    std::vector<std::string> result;
    for (int32_t i = 0; i <= count; ++i) {
        auto a = p.start(state_->expression, i, &error), b = p.end(state_->expression, i, &error);
        checked(error);
        result.push_back(a < 0 ? ""
                               : utf8(std::u16string_view(*units).substr(static_cast<size_t>(a),
                                                                         static_cast<size_t>(b - a))));
    }
    return result;
}
std::string stream_reader_text(const std::string& bytes) {
    if (bytes.starts_with(std::string("\xff\xfe\0\0", 4)) ||
        bytes.starts_with(std::string("\0\0\xfe\xff", 4))) {
        bool little = uint8_t(bytes[0]) == 255;
        std::u16string units;
        size_t i = 4;
        for (; i + 3 < bytes.size(); i += 4) {
            uint32_t point = 0;
            for (int b = 0; b < 4; ++b)
                point |= uint32_t(uint8_t(bytes[i + b])) << ((little ? b : 3 - b) * 8);
            if (point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
                point = 0xfffd;
            if (point <= 65535)
                units += char16_t(point);
            else {
                point -= 0x10000;
                units += char16_t(0xd800 + (point >> 10));
                units += char16_t(0xdc00 + (point & 1023));
            }
        }
        if (i != bytes.size())
            units += char16_t(0xfffd);
        return utf8(units);
    }
    if (bytes.starts_with("\xef\xbb\xbf"))
        return utf8(utf16_replacing(std::string_view(bytes).substr(3)));
    if (bytes.starts_with("\xff\xfe") || bytes.starts_with("\xfe\xff")) {
        bool little = bytes.starts_with("\xff\xfe");
        std::u16string units;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            auto a = uint8_t(bytes[i]), b = uint8_t(bytes[i + 1]);
            units += char16_t(little ? a | (b << 8) : (a << 8) | b);
        }
        if (bytes.size() % 2)
            units += char16_t(0xfffd);
        return utf8(units);
    }
    return utf8(utf16_replacing(bytes));
}
}
