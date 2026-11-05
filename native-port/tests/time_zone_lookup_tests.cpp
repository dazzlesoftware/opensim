// Source-derived named lookup gate; MIT .NET source: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/time_zone.hpp"
#include <iostream>
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/framework/text.hpp"
#include <sstream>
#include <iomanip>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace opensim::framework;
struct RegistryOverride {
#ifdef _WIN32
    HKEY key = nullptr;
    bool active = false;
#endif
    RegistryOverride(int argc, char** argv) {
#ifdef _WIN32
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--registry") {
                if (++i >= argc)
                    throw std::invalid_argument("Missing fixture root");
                std::string root = argv[i], prefix = "Software\\OpenSimNativeTimeZoneTests\\";
                if (!root.starts_with(prefix) || root.size() != prefix.size() + 32 ||
                    root.substr(prefix.size()).find_first_not_of("0123456789abcdef") != root.npos)
                    throw std::invalid_argument("Unowned fixture registry root");
                auto raw = utf16_replacing(root);
                auto status = RegOpenKeyExW(HKEY_CURRENT_USER, reinterpret_cast<const wchar_t*>(raw.c_str()),
                                            0, KEY_READ, &key);
                if (status)
                    throw std::runtime_error("Open fixture root");
                status = RegOverridePredefKey(HKEY_LOCAL_MACHINE, key);
                if (status) {
                    RegCloseKey(key);
                    key = nullptr;
                    throw std::runtime_error("Override fixture root");
                }
                active = true;
            }
#else
        (void)argc;
        (void)argv;
#endif
    }
    ~RegistryOverride() {
#ifdef _WIN32
        if (active)
            RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
        if (key)
            RegCloseKey(key);
#endif
    }
};
std::string h(const std::u16string& s) {
    if (s.empty())
        return ".";
    std::ostringstream o;
    for (auto c : s)
        o << std::hex << std::setfill('0') << std::setw(4) << int(c);
    return o.str();
}
std::string e(const NativeTimeZoneError& x) {
    std::ostringstream o;
    o << x.source_type << ";" << std::hex << std::setfill('0') << std::setw(8) << x.hresult << ";"
      << h(x.message) << ";" << (x.parameter ? h(*x.parameter) : "-") << ";"
      << (x.file_name ? h(*x.file_name) : "-") << ";[" << (x.inner ? e(*x.inner) : "-") << "]";
    return o.str();
}
int run_lookup_probe(int argc, char** argv) {
    RegistryOverride registry_override(argc, argv);
    NativeInvariantDateTimeContextOptions options;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--invariant")
            options.globalization_invariant_mode = true;
        if (std::string(argv[i]) == "--nls")
            options.use_nls = true;
    }
    TimeZoneDatabase db(std::make_shared<NativeInvariantDateTimeContext>(options));
    std::string l;
    while (std::getline(std::cin, l)) {
        if (l == "!") {
            db.clear();
            std::cout << "CLEAR\n";
            continue;
        }
        try {
            std::int64_t ticks = 638554752000000000LL;
            auto split = l.find('#');
            if (split != l.npos) {
                ticks = std::stoll(l.substr(split + 1));
                l.resize(split);
            }
            std::optional<std::u16string> id;
            if (l != "-") {
                id = std::u16string{};
                if (l != ".")
                    for (size_t i = 0; i < l.size(); i += 4)
                        id->push_back(char16_t(std::stoul(l.substr(i, 4), nullptr, 16)));
            }
            auto input = id ? std::make_shared<const std::u16string>(*id) : TimeZoneText{};
            auto z = db.find_text(input);
            std::u16string out = z->id_utf16();
            std::cout << "OK;" << h(out) << ";" << z->daylight_at_ticks(ticks) << ";"
                      << (z->id_text() == input ? 1 : 0) << "\n";
        } catch (const NativeTimeZoneError& x) {
            std::cout << e(x) << "\n";
        } catch (const std::exception& x) {
            std::cout << "UNEXPECTED " << x.what() << "\n";
        }
    }
    return 0;
}
#include "opensim/framework/time_zone.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include <iostream>
#include "opensim/framework/file_system_name.hpp"
using namespace opensim::framework;
int run_lookup_provider_units() {
    try {
        int calls = 0;
        std::u16string seen;
        NativeTimeZoneLookupOptions options;
        options.windows_to_iana = [&](std::u16string_view id) -> std::optional<std::u16string> {
            ++calls;
            seen = id;
            return u"Valid";
        };
        options.ordinal_ignore_case_equal = [](auto a, auto b) {
            return a.size() == b.size() &&
                   file_system_name::pinned_case_backend(file_system_name::CasingMode::Normal)
                       .ordinal_ignore_case_ends_with(a, b);
        };
        NativeInvariantDateTimeContextOptions owner_options;
#ifdef _WIN32
        owner_options.iana_to_windows = options.windows_to_iana;
#endif
        auto owner = std::make_shared<NativeInvariantDateTimeContext>(owner_options);
        TimeZoneDatabase db(owner, options);
        std::u16string first{u'a', char16_t(0xd800)}, other{u'a', char16_t(0xd801)},
            upper{u'A', char16_t(0xd800)};
        auto a = db.find_utf16(first);
        auto b = db.find_utf16(other);
        auto c = db.find_utf16(upper);
        if (a == b || a != c || a->id_utf16() != first || b->id_utf16() != other || calls != 2 ||
            seen != other)
            return 1;
        db.clear();
        if (db.find_utf16(first) == a || calls != 3)
            return 2;
        int before = calls;
        try {
            db.find_utf16(std::nullopt);
            return 3;
        } catch (const NativeTimeZoneError& e) {
            if (e.source_type != "System.ArgumentNullException")
                return 4;
        }
        if (calls != before)
            return 5;
        for (bool nls : {false, true}) {
#ifndef _WIN32
            if (nls)
                continue;
#endif
            NativeInvariantDateTimeContextOptions flags;
            if (nls)
                flags.use_nls = true;
            else
                flags.globalization_invariant_mode = true;
#ifdef _WIN32
            flags.iana_to_windows = options.windows_to_iana;
#endif
            TimeZoneDatabase isolated(std::make_shared<NativeInvariantDateTimeContext>(flags), options);
            try {
                isolated.find_utf16(first);
                return 6;
            } catch (const NativeTimeZoneError& e) {
                if (e.source_type != "System.TimeZoneNotFoundException")
                    return 7;
            }
            if (calls != before)
                return 8;
        }
        options.windows_to_iana = [](auto) -> std::optional<std::u16string> {
            throw std::logic_error("converter fault");
        };
        NativeInvariantDateTimeContextOptions fault_flags;
#ifdef _WIN32
        fault_flags.iana_to_windows = options.windows_to_iana;
#endif
        TimeZoneDatabase fault(std::make_shared<NativeInvariantDateTimeContext>(fault_flags), options);
        try {
            fault.find_utf16(first);
            return 9;
        } catch (const std::logic_error& e) {
            if (std::string(e.what()) != "converter fault")
                return 10;
        }
        auto upper_scalar = db.find_utf16(std::u16string(u"x\U00010400"));
        auto lower = db.find_utf16(std::u16string(u"X\U00010428"));
        if (upper_scalar != lower)
            return 11;
        TimeZoneDatabase* current = nullptr;
        bool clear_once = true;
        int reentry_calls = 0;
        NativeTimeZoneLookupOptions reentry;
        reentry.windows_to_iana = [&](auto) -> std::optional<std::u16string> {
            ++reentry_calls;
            if (clear_once) {
                clear_once = false;
                current->clear();
            }
            return u"Valid";
        };
        NativeInvariantDateTimeContextOptions reentry_flags;
#ifdef _WIN32
        reentry_flags.iana_to_windows = reentry.windows_to_iana;
#endif
        TimeZoneDatabase reentrant(std::make_shared<NativeInvariantDateTimeContext>(reentry_flags), reentry);
        current = &reentrant;
        auto old = reentrant.find_utf16(first);
        auto fresh = reentrant.find_utf16(first);
        if (old == fresh || reentry_calls != 2 || reentrant.find_utf16(first) != fresh)
            return 12;
        std::cout << "PASS raw ID/cache/selected converter/fault/gating/reentrant epoch units\n";
    } catch (const std::exception& e) {
        std::cerr << e.what();
        return 20;
    }
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--probe")
            return run_lookup_probe(argc, argv);
        if (std::string(argv[i]) == "--provider-units") {
            RegistryOverride registry(argc, argv);
            return run_lookup_provider_units();
        }
    }
    try {
        TimeZoneDatabase db;
        auto source_literal = time_zone_utc_id_text();
        auto a = db.find_text(source_literal);
        if (a->id_text() != source_literal || a->daylight_at_ticks(0) ||
            a->daylight_at_ticks(3155378975999999999LL))
            return 1;
        auto dynamic = std::make_shared<const std::u16string>(u"uTc");
        if (db.find_text(dynamic) != a || a->id_text() == dynamic)
            return 2;
        db.clear();
        if (db.find("UTC") != a)
            return 3;
        try {
            db.find_text({});
            return 4;
        } catch (const NativeTimeZoneError& e) {
            if (e.source_type != "System.ArgumentNullException" || e.hresult != 0x80004003u ||
                e.parameter != std::optional<std::u16string>(u"id"))
                return 5;
        }
        try {
            db.find_utf16(std::u16string(256, u'X') + u"\u00e9");
            return 6;
        } catch (const std::exception& e) {
            if (std::string(e.what()).find("\xc3\xa9") == std::string::npos)
                return 7;
        }
        try {
            db.find_utf16(std::u16string(u"x\0y", 3));
            return 8;
        } catch (const NativeTimeZoneError& e) {
            if (e.message.find(char16_t(0)) == e.message.npos || e.inner)
                return 9;
        }
        std::cout << "PASS typed lookup UTC/raw validation/identity/generic what checks\n";
        return 0;
    } catch (...) {
        return 10;
    }
}
