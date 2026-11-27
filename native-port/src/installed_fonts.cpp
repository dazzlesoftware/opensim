// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/installed_fonts.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define OPENSIM_FONT_CALL __stdcall
#else
#include <dlfcn.h>
#define OPENSIM_FONT_CALL
#endif

namespace opensim::framework {
namespace {
using Status = std::int32_t;
using Handle = void *;

struct StartupInput {
    std::uint32_t version = 1;
    void *debug_callback = nullptr;
    std::int32_t suppress_background_thread = 0;
    std::int32_t suppress_external_codecs = 0;
};

void check(Status status, std::string_view operation) {
    if (status != 0)
        throw std::runtime_error(std::string(operation) + " failed (GDI+ status " + std::to_string(status) + ")");
}

class Library {
#ifdef _WIN32
    HMODULE handle_ = nullptr;
#else
    void *handle_ = nullptr;
#endif

  public:
    Library() {
#ifdef _WIN32
        handle_ = LoadLibraryExW(L"gdiplus.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!handle_)
            throw std::runtime_error("Installed font provider could not load gdiplus.dll (Windows error " +
                                     std::to_string(GetLastError()) + ")");
#else
#ifdef __APPLE__
        constexpr const char *names[]{"libgdiplus.0.dylib", "libgdiplus.dylib"};
#else
        constexpr const char *names[]{"libgdiplus.so.0", "libgdiplus.so"};
#endif
        std::string error;
        for (const auto *name : names) {
            handle_ = dlopen(name, RTLD_NOW | RTLD_LOCAL);
            if (handle_)
                break;
            if (const auto *message = dlerror())
                error = message;
        }
        if (!handle_)
            throw std::runtime_error("Installed font provider could not load libgdiplus: " + error);
#endif
    }

    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;

    ~Library() {
#ifdef _WIN32
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
    }

    template <class Function> Function load(const char *name) const {
#ifdef _WIN32
        const auto address = GetProcAddress(handle_, name);
#else
        const auto address = dlsym(handle_, name);
#endif
        if (!address)
            throw std::runtime_error(std::string("Installed font provider is missing ") + name);
        return reinterpret_cast<Function>(address);
    }
};

class Backend {
    Library library_;
    using Startup = Status(OPENSIM_FONT_CALL *)(std::uintptr_t *, const StartupInput *, void *);
    using Shutdown = void(OPENSIM_FONT_CALL *)(std::uintptr_t);
    using NewCollection = Status(OPENSIM_FONT_CALL *)(Handle *);
    using FamilyCount = Status(OPENSIM_FONT_CALL *)(Handle, std::int32_t *);
    using FamilyList = Status(OPENSIM_FONT_CALL *)(Handle, std::int32_t, Handle *, std::int32_t *);
    using CloneFamily = Status(OPENSIM_FONT_CALL *)(Handle, Handle *);
    using DeleteFamily = Status(OPENSIM_FONT_CALL *)(Handle);
    using FamilyName = Status(OPENSIM_FONT_CALL *)(Handle, char16_t *, std::uint16_t);
    Shutdown shutdown_;
    NewCollection new_collection_;
    FamilyCount family_count_;
    FamilyList family_list_;
    CloneFamily clone_family_;
    DeleteFamily delete_family_;
    FamilyName family_name_;
    std::uintptr_t token_ = 0;
    std::mutex mutex_;

  public:
    Backend()
        : shutdown_(library_.load<Shutdown>("GdiplusShutdown")),
          new_collection_(library_.load<NewCollection>("GdipNewInstalledFontCollection")),
          family_count_(library_.load<FamilyCount>("GdipGetFontCollectionFamilyCount")),
          family_list_(library_.load<FamilyList>("GdipGetFontCollectionFamilyList")),
          clone_family_(library_.load<CloneFamily>("GdipCloneFontFamily")),
          delete_family_(library_.load<DeleteFamily>("GdipDeleteFontFamily")),
          family_name_(library_.load<FamilyName>("GdipGetFamilyName")) {
        const auto startup = library_.load<Startup>("GdiplusStartup");
        const StartupInput input;
        check(startup(&token_, &input, nullptr), "GdiplusStartup");
    }

    Backend(const Backend &) = delete;
    Backend &operator=(const Backend &) = delete;

    ~Backend() {
        shutdown_(token_);
    }

    std::vector<std::string> enumerate(std::uint16_t language) {
        // libgdiplus builds its shared installed collection lazily. Serialize
        // our access without caching names or changing the backend's order.
        const std::lock_guard lock(mutex_);
        Handle collection = nullptr;
        check(new_collection_(&collection), "GdipNewInstalledFontCollection");
        if (!collection)
            throw std::runtime_error("GdipNewInstalledFontCollection returned a null collection");
        // The Windows SDK InstalledFontCollection and base FontCollection
        // destructors are empty. This handle is not a PrivateFontCollection;
        // GdipDeletePrivateFontCollection must not be used to dispose it.
        std::int32_t count = 0;
        check(family_count_(collection, &count), "GdipGetFontCollectionFamilyCount");
        if (count < 0)
            throw std::runtime_error("GdipGetFontCollectionFamilyCount returned a negative count");
        if (!count)
            return {};
        std::vector<Handle> borrowed(static_cast<std::size_t>(count));
        std::int32_t found = 0;
        check(family_list_(collection, count, borrowed.data(), &found), "GdipGetFontCollectionFamilyList");
        if (found < 0 || found > count)
            throw std::runtime_error("GdipGetFontCollectionFamilyList returned an invalid count");
        std::vector<std::string> names;
        names.reserve(static_cast<std::size_t>(found));
        for (std::int32_t i = 0; i < found; ++i) {
            // FontCollection::GetFamilies clones the borrowed collection
            // entries. Only our clones are owned and deleted here.
            Handle family = nullptr;
            check(clone_family_(borrowed[static_cast<std::size_t>(i)], &family), "GdipCloneFontFamily");
            if (!family)
                throw std::runtime_error("GdipCloneFontFamily returned a null family");

            struct FamilyOwner {
                Handle family;
                DeleteFamily destroy;

                ~FamilyOwner() {
                    destroy(family);
                }
            } owner{family, delete_family_};

            std::array<char16_t, 32> buffer{}; // SDK LF_FACESIZE, in UTF16 units.
            check(family_name_(family, buffer.data(), language), "GdipGetFamilyName");
            const auto end = std::find(buffer.begin(), buffer.end(), char16_t{});
            if (end == buffer.end())
                throw std::runtime_error("GdipGetFamilyName returned an unterminated family name");
            names.push_back(utf8(std::u16string_view(buffer.data(), static_cast<std::size_t>(end - buffer.begin()))));
        }
        return names;
    }
};
} // namespace

std::uint16_t installed_font_ui_language() {
#ifdef _WIN32
    return GetThreadUILanguage();
#else
    return 127;
#endif
}

std::vector<std::string> installed_font_families(std::uint16_t ui_language_id) {
    static Backend backend;
    return backend.enumerate(ui_language_id);
}
} // namespace opensim::framework

#undef OPENSIM_FONT_CALL
