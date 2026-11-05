// Copyright (c) Contributors, http://opensimulator.org/
// Platform-loading closure ported from dotnet/runtime v8.0.31 TimeZoneInfo.Win32.cs.
// .NET Foundation, MIT license; see third_party/dotnet_collation/LICENSE.
#include "invariant_datetime_context_platform.hpp"
#include "opensim/framework/time_zone.hpp"
#include "opensim/framework/text.hpp"
#include <sstream>
#include <locale>
#include <iomanip>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <optional>
#include <stdexcept>
namespace opensim::framework::invariant_datetime_platform {
namespace {
constexpr wchar_t hive[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones";
constexpr std::int64_t minute = 600000000ll, day = 864000000000ll;
struct MissingOrInvalid : std::runtime_error {
    enum class Reason { Invalid, Missing };
    Reason reason;
    std::shared_ptr<const NativeTimeZoneError> source;
    explicit MissingOrInvalid(std::string m, Reason r = Reason::Invalid,
                              std::shared_ptr<const NativeTimeZoneError> e = {})
        : std::runtime_error(std::move(m)), reason(r), source(std::move(e)) {};
};
MissingOrInvalid argument(std::u16string message, std::optional<std::u16string> parameter = {}) {
    if (parameter)
        message += u" (Parameter '" + *parameter + u"')";
    return MissingOrInvalid("Invalid timezone registry data", MissingOrInvalid::Reason::Invalid,
                            std::make_shared<NativeTimeZoneError>("System.ArgumentOutOfRangeException",
                                                                  0x80131502u, std::move(message),
                                                                  std::move(parameter)));
}
std::u16string span_string(std::int64_t ticks) {
    bool neg = ticks < 0;
    auto n = static_cast<std::uint64_t>(neg ? -ticks : ticks) / 10000000;
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    if (neg)
        stream << '-';
    auto days = n / 86400;
    if (days)
        stream << days << '.';
    stream << std::setfill('0') << std::setw(2) << n / 3600 % 24 << ':' << std::setw(2) << n / 60 % 60 << ':'
           << std::setw(2) << n % 60;
    return utf16_replacing(stream.str());
}

struct Key {
    HKEY key = nullptr;
    Key() = default;
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    ~Key() {
        if (key)
            RegCloseKey(key);
    }
    bool open(HKEY root, const std::wstring& name) {
        auto e = RegOpenKeyExW(root, name.c_str(), 0, KEY_READ, &key);
        if (e == ERROR_SUCCESS)
            return true;
        if (e == ERROR_ACCESS_DENIED || e == ERROR_BAD_IMPERSONATION_LEVEL)
            throw NativeTimeZoneError("System.Security.SecurityException", 0x8013150au,
                                      u"Requested registry access is not allowed.");
        return false;
    }
};
struct Tzi {
    LONG Bias, StandardBias, DaylightBias;
    SYSTEMTIME StandardDate, DaylightDate;
};
static_assert(sizeof(Tzi) == 44);
std::optional<std::vector<std::byte>> value(Key& key, const std::wstring& name, DWORD& type) {
    DWORD n = 0;
    auto e = RegQueryValueExW(key.key, name.c_str(), nullptr, &type, nullptr, &n);
    if (e != ERROR_SUCCESS)
        return {};
    if (n > 2147483647u)
        throw std::runtime_error("Registry timezone value exceeds 2GB");
    std::vector<std::byte> out(n);
    for (;;) {
        DWORD len = static_cast<DWORD>(out.size());
        e = RegQueryValueExW(key.key, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(out.data()),
                             &len);
        if (e == ERROR_MORE_DATA) {
            out.resize(len);
            continue;
        }
        if (e != ERROR_SUCCESS)
            return {};
        out.resize(len);
        return out;
    }
}
Tzi tzi(Key& key, const std::wstring& name) {
    DWORD type = 0;
    auto bytes = value(key, name, type);
    if (!bytes ||
        (type != REG_NONE && type != REG_BINARY && type != REG_DWORD_BIG_ENDIAN && type != REG_DWORD &&
         type != REG_QWORD) ||
        bytes->size() != sizeof(Tzi))
        throw MissingOrInvalid("Invalid timezone registry TZI");
    Tzi out;
    std::memcpy(&out, bytes->data(), sizeof out);
    return out;
}
int integer(Key& key, const wchar_t* name) {
    DWORD type = 0;
    auto bytes = value(key, name, type);
    if (!bytes)
        return -1;
    if ((type == REG_DWORD || type == REG_QWORD) && bytes->size() == 4) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, bytes->data(), bytes->size());
        return std::bit_cast<std::int32_t>(bits);
    }
    std::u16string from;
    if (type == REG_SZ || type == REG_EXPAND_SZ)
        from = u"System.String";
    else if (type == REG_MULTI_SZ)
        from = u"System.String[]";
    else if ((type == REG_DWORD || type == REG_QWORD) && bytes->size() == 8)
        from = u"System.Int64";
    else if (type == REG_BINARY || type == REG_NONE || type == REG_DWORD_BIG_ENDIAN || type == REG_DWORD ||
             type == REG_QWORD)
        from = u"System.Byte[]";
    else
        return -1;
    throw MissingOrInvalid("Invalid timezone registry integer", MissingOrInvalid::Reason::Invalid,
                           std::make_shared<NativeTimeZoneError>("System.InvalidCastException", 0x80004002u,
                                                                 u"Unable to cast object of type '" + from +
                                                                     u"' to type 'System.Int32'."));
}
std::optional<std::wstring> string_value(Key& key, const wchar_t* name) {
    DWORD type = 0;
    auto bytes = value(key, name, type);
    if (!bytes)
        return std::wstring();
    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return {};
    if (bytes->size() % 2)
        bytes->push_back(std::byte{});
    std::wstring out(reinterpret_cast<const wchar_t*>(bytes->data()), bytes->size() / sizeof(wchar_t));
    if (!out.empty() && out.back() == 0)
        out.pop_back();
    if (type == REG_EXPAND_SZ) {
        auto n = ExpandEnvironmentStringsW(out.c_str(), nullptr, 0);
        if (!n)
            throw std::runtime_error("Registry timezone string expansion failed");
        std::wstring expanded(n, 0);
        if (!ExpandEnvironmentStringsW(out.c_str(), expanded.data(), n))
            throw std::runtime_error("Registry timezone string expansion failed");
        if (!expanded.empty() && expanded.back() == 0)
            expanded.pop_back();
        out = std::move(expanded);
    }
    return out;
}
std::int64_t date(int y, int m, int d) {
    using namespace std::chrono;
    if (y < 1 || y > 9999)
        throw argument(u"Year, Month, and Day parameters describe an un-representable DateTime.");
    return (sys_days{year(y) / static_cast<unsigned>(m) / static_cast<unsigned>(d)} -
            sys_days{year(1) / 1 / 1})
               .count() *
           day;
}
std::int64_t negate32(std::int32_t n) {
    return std::bit_cast<std::int32_t>(0u - static_cast<std::uint32_t>(n));
}
std::int64_t subtract32(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
bool same(const SYSTEMTIME& a, const SYSTEMTIME& b) {
    return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDayOfWeek == b.wDayOfWeek && a.wDay == b.wDay &&
           a.wHour == b.wHour && a.wMinute == b.wMinute && a.wSecond == b.wSecond &&
           a.wMilliseconds == b.wMilliseconds;
}
InvariantTimeZoneTransition transition(const SYSTEMTIME& t) {
    InvariantTimeZoneTransition out;
    out.time_of_day_ticks =
        (std::int64_t(t.wHour) * 3600 + t.wMinute * 60 + t.wSecond) * 10000000ll + t.wMilliseconds * 10000ll;
    out.month = t.wMonth;
    out.fixed_date = t.wYear != 0;
    out.day = out.fixed_date ? t.wDay : 1;
    out.week = out.fixed_date ? 1 : t.wDay;
    out.day_of_week = out.fixed_date ? 0 : t.wDayOfWeek;
    if (t.wHour >= 24 || t.wMinute >= 60 || t.wSecond >= 60)
        throw argument(u"Hour, Minute, and Second parameters describe an un-representable DateTime.");
    if (t.wMilliseconds >= 1000)
        throw argument(u"Valid values are between 0 and 999, inclusive.", u"millisecond");
    if (out.month < 1 || out.month > 12)
        throw argument(u"The Month parameter must be in the range 1 through 12.", u"month");
    if (out.day < 1 || out.day > 31)
        throw argument(u"The Day parameter must be in the range 1 through 31.", u"day");
    if (out.week < 1 || out.week > 5)
        throw argument(u"The Week parameter must be in the range 1 through 5.", u"week");
    if (out.day_of_week < 0 || out.day_of_week > 6)
        throw argument(u"The DayOfWeek enumeration must be in the range 0 through 6.", u"dayOfWeek");
    return out;
}
bool same(const InvariantTimeZoneTransition& a, const InvariantTimeZoneTransition& b) {
    return a.time_of_day_ticks == b.time_of_day_ticks && a.month == b.month && a.fixed_date == b.fixed_date &&
           (a.fixed_date ? a.day == b.day : a.week == b.week && a.day_of_week == b.day_of_week);
}
void append(ZoneModel& model, const Tzi& t, std::int64_t start, std::int64_t end, int base_bias) {
    InvariantTimeZoneRule r;
    r.start = AssetDateTime(start, 0);
    r.end = AssetDateTime(end, 0);
    r.base_offset_delta_ticks = subtract32(base_bias, t.Bias) * minute;
    if (t.StandardDate.wMonth == 0) {
        if (t.Bias == base_bias)
            return;
        r.daylight_start = {0, 1, 1, 1, 0, true};
        r.daylight_end = {10000, 1, 1, 1, 0, true};
    } else {
        r.daylight_start = transition(t.DaylightDate);
        r.daylight_end = transition(t.StandardDate);
        if (same(r.daylight_start, r.daylight_end))
            return;
        r.daylight_delta_ticks = negate32(t.DaylightBias) * minute;
    }
    // AdjustmentRule internal factory normalizes a single 24h range crossing.
    if (r.daylight_delta_ticks > 12 * 60 * minute) {
        r.daylight_delta_ticks -= day;
        r.base_offset_delta_ticks += day;
    } else if (r.daylight_delta_ticks < -12 * 60 * minute) {
        r.daylight_delta_ticks += day;
        r.base_offset_delta_ticks -= day;
    }
    if (r.daylight_delta_ticks < -23 * 60 * minute || r.daylight_delta_ticks > 14 * 60 * minute)
        throw MissingOrInvalid(
            "Invalid timezone daylight delta", MissingOrInvalid::Reason::Invalid,
            std::make_shared<NativeTimeZoneError>(
                "System.ArgumentOutOfRangeException", 0x80131502u,
                u"The TimeSpan parameter must be within plus or minus 14.0 hours. (Parameter 'daylightDelta')\r\nActual value was " +
                    span_string(r.daylight_delta_ticks) + u".",
                u"daylightDelta"));
    model.rules.push_back(r);
}
void validate(const ZoneModel& model) {
    if (model.base_offset < -14 * 60 * minute || model.base_offset > 14 * 60 * minute ||
        model.base_offset % minute)
        throw argument(u"The TimeSpan parameter must be within plus or minus 14.0 hours.", u"baseUtcOffset");
    const InvariantTimeZoneRule* prev = nullptr;
    for (auto& r : model.rules) {
        bool dst = r.daylight_delta_ticks != 0 || r.daylight_start.time_of_day_ticks != 0 ||
                   r.daylight_end.time_of_day_ticks != 10000;
        auto offset = model.base_offset + r.base_offset_delta_ticks + (dst ? r.daylight_delta_ticks : 0);
        if (offset < -14 * 60 * minute || offset > 14 * 60 * minute ||
            (prev && r.start.ticks() <= prev->end.ticks()))
            throw MissingOrInvalid(
                "Invalid timezone adjustment rules", MissingOrInvalid::Reason::Invalid,
                std::make_shared<NativeTimeZoneError>(
                    "System.InvalidTimeZoneException", 0x80131500u,
                    u"The sum of the BaseUtcOffset and DaylightDelta properties must within plus or minus 14.0 hours."));
        prev = &r;
    }
}
ZoneModel from_current(const TIME_ZONE_INFORMATION& z, bool disabled) {
    ZoneModel m;
    m.base_offset = negate32(z.Bias) * minute;
    if (!disabled) {
        Tzi t{z.Bias, z.StandardBias, z.DaylightBias, z.StandardDate, z.DaylightDate};
        append(m, t, 0, date(9999, 12, 31), z.Bias);
    }
    validate(m);
    return m;
}
ZoneModel current_fallback(const TIME_ZONE_INFORMATION& z, bool disabled) {
    try {
        return from_current(z, disabled);
    } catch (const MissingOrInvalid&) {
    }
    if (!disabled)
        try {
            return from_current(z, true);
        } catch (const MissingOrInvalid&) {
        }
    return {};
}
std::optional<std::wstring> find_id(const TIME_ZONE_INFORMATION& z) {
    Key root;
    if (!root.open(HKEY_LOCAL_MACHINE, hive))
        return {};
    for (DWORD i = 0;; ++i) {
        DWORD size = 256;
        std::wstring name(size, 0);
        FILETIME write{};
        auto e = RegEnumKeyExW(root.key, i, name.data(), &size, nullptr, nullptr, nullptr, &write);
        if (e == ERROR_NO_MORE_ITEMS)
            return {};
        if (e == ERROR_MORE_DATA) {
            throw std::runtime_error("Timezone registry key exceeds Windows key-name limit");
        }
        if (e)
            throw std::runtime_error("Timezone registry enumeration failed");
        name.resize(size);
        Key key;
        if (!key.open(root.key, name))
            continue;
        Tzi t;
        try {
            t = tzi(key, L"TZI");
        } catch (const MissingOrInvalid&) {
            continue;
        }
        if (z.Bias != t.Bias || z.StandardBias != t.StandardBias || !same(z.StandardDate, t.StandardDate))
            continue;
        if (!same(z.DaylightDate, z.StandardDate) &&
            (z.DaylightBias != t.DaylightBias || !same(z.DaylightDate, t.DaylightDate)))
            continue;
        auto standard = string_value(key, L"Std");
        if (standard && *standard == std::wstring(z.StandardName))
            return name;
    }
}
}
ZoneModel windows_registry_zone(const std::wstring& name, bool disabled) {
    Key key;
    if (!key.open(HKEY_LOCAL_MACHINE, std::wstring(hive) + L"\\" + name))
        throw MissingOrInvalid("Timezone registry key not found", MissingOrInvalid::Reason::Missing);
    auto base = tzi(key, L"TZI");
    ZoneModel model;
    model.base_offset = negate32(base.Bias) * minute;
    Key dynamic;
    if (!dynamic.open(HKEY_LOCAL_MACHINE, std::wstring(hive) + L"\\" + name + L"\\Dynamic DST"))
        append(model, base, 0, date(9999, 12, 31), base.Bias);
    else {
        int first = integer(dynamic, L"FirstEntry"), last = integer(dynamic, L"LastEntry");
        if (first == -1 || last == -1 || first > last)
            throw MissingOrInvalid("Invalid dynamic timezone bounds");
        auto first_tzi = tzi(dynamic, std::to_wstring(first));
        if (first == last)
            append(model, first_tzi, 0, date(9999, 12, 31), base.Bias);
        else {
            append(model, first_tzi, 0, date(first, 12, 31), base.Bias);
            for (int y = first + 1; y < last; ++y)
                append(model, tzi(dynamic, std::to_wstring(y)), date(y, 1, 1), date(y, 12, 31), base.Bias);
            append(model, tzi(dynamic, std::to_wstring(last)), date(last, 1, 1), date(9999, 12, 31),
                   base.Bias);
        }
    }
    validate(model);
    if (disabled && !model.rules.empty())
        model.rules.clear();
    return model;
}
std::optional<ZoneModel> windows_registry_zone_lookup(const std::wstring& name) {
    try {
        return windows_registry_zone(name, false);
    } catch (const MissingOrInvalid& e) {
        if (e.reason == MissingOrInvalid::Reason::Missing)
            return {};
        if (e.source)
            throw *e.source;
        throw NativeTimeZoneError("System.NullReferenceException", 0x80004003u,
                                  u"Object reference not set to an instance of an object.");
    }
}
std::optional<std::u16string> windows_system_iana_to_windows(std::u16string_view input) {
    if (input.find_first_of(u"\\\n\r") != input.npos)
        return {};
    struct Icu {
        HMODULE module = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Convert = std::int32_t (*)(const char16_t*, std::int32_t, char16_t*, std::int32_t,
                                         std::int32_t*);
        Convert convert = module
                              ? reinterpret_cast<Convert>(GetProcAddress(module, "ucal_getWindowsTimeZoneID"))
                              : nullptr; /* The source ICU loader retains its module for process lifetime. */
    };
    static Icu icu;
    if (!icu.convert)
        return {};
    std::u16string terminated(input);
    std::array<char16_t, 100> buffer{};
    std::int32_t status = 0;
    auto length =
        icu.convert(terminated.c_str(), -1, buffer.data(), static_cast<std::int32_t>(buffer.size()), &status);
    if (status > 0 || length <= 0 || length > static_cast<int>(buffer.size()))
        return {};
    return std::u16string(buffer.data(), static_cast<std::size_t>(length));
}
ZoneModel windows_local_zone(bool invariant, bool use_nls,
                             std::function<std::optional<std::u16string>(std::u16string_view)> converter) {
    auto lookup = [&](const std::wstring& id, bool disabled) -> std::optional<ZoneModel> {
        try {
            return windows_registry_zone(id, disabled);
        } catch (const MissingOrInvalid&) {
        }
        if (invariant || use_nls)
            return {};
        auto input = std::u16string_view(reinterpret_cast<const char16_t*>(id.data()), id.size());
        if (input.find_first_of(u"\\\n\r") != input.npos)
            return {};
        auto alternative = converter ? converter(input) : windows_system_iana_to_windows(input);
        if (!alternative)
            return {};
        try {
            return windows_registry_zone(std::wstring(alternative->begin(), alternative->end()), disabled);
        } catch (const MissingOrInvalid&) {
            return {};
        }
    };
    DYNAMIC_TIME_ZONE_INFORMATION d{};
    if (GetDynamicTimeZoneInformation(&d) == TIME_ZONE_ID_INVALID)
        return {};
    if (d.TimeZoneKeyName[0])
        if (auto found = lookup(d.TimeZoneKeyName, d.DynamicDaylightTimeDisabled != 0))
            return *found;
    TIME_ZONE_INFORMATION z{};
    z.Bias = d.Bias;
    z.StandardBias = d.StandardBias;
    z.DaylightBias = d.DaylightBias;
    z.StandardDate = d.StandardDate;
    z.DaylightDate = d.DaylightDate;
    std::copy(std::begin(d.StandardName), std::end(d.StandardName), std::begin(z.StandardName));
    std::copy(std::begin(d.DaylightName), std::end(d.DaylightName), std::begin(z.DaylightName));
    if (auto id = find_id(z))
        if (auto found = lookup(*id, false))
            return *found;
    return current_fallback(z, false);
}
ZoneModel windows_current_year_zone() {
    TIME_ZONE_INFORMATION z{};
    return GetTimeZoneInformation(&z) == TIME_ZONE_ID_INVALID ? ZoneModel{} : current_fallback(z, false);
}
}
#endif
