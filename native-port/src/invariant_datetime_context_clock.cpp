// Copyright (c) Contributors, http://opensimulator.org/
// Ported from dotnet/runtime v8.0.31 DateTime.Windows.cs, DateTime.Unix.cs,
// CalendarData.Windows.cs / Unix.cs and System.Native/pal_datetime.c.
// .NET Foundation, MIT license; see third_party/dotnet_collation/LICENSE.
#include "invariant_datetime_context_platform.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#include <sys/time.h>
#endif
namespace opensim::framework::invariant_datetime_platform {
namespace {
[[maybe_unused]] constexpr std::uint64_t epoch = 621355968000000000ull, utc_kind = 0x4000000000000000ull;
#ifdef _WIN32
constexpr std::uint64_t file_epoch = 504911232000000000ull, second = 10000000ull, day = 864000000000ull,
                        window = 300 * second;
struct Cache {
    std::uint64_t file_start = 0, date_start = 0;
};
using Getter = void(WINAPI*)(FILETIME*);
struct Clock {
    bool leap = false;
    Getter getter = nullptr;
    std::atomic<std::shared_ptr<const Cache>> cache{std::make_shared<Cache>()};
    Clock() {
        struct LeapInfo {
            BYTE enabled;
            DWORD flags;
        } info{};
        using Query = LONG(NTAPI*)(int, void*, ULONG, ULONG*);
        auto ntdll = GetModuleHandleW(L"ntdll.dll");
        auto query = reinterpret_cast<Query>(GetProcAddress(ntdll, "NtQuerySystemInformation"));
        leap = query && query(206, &info, sizeof info, nullptr) == 0 && info.enabled != 0;
        auto kernel = GetModuleHandleW(L"kernel32.dll");
        getter = reinterpret_cast<Getter>(GetProcAddress(kernel, "GetSystemTimeAsFileTime"));
        auto precise = reinterpret_cast<Getter>(GetProcAddress(kernel, "GetSystemTimePreciseAsFileTime"));
        if (precise)
            for (int i = 0; i < 10; ++i) {
                FILETIME a{}, b{};
                getter(&a);
                precise(&b);
                auto normal = bits(a), p = bits(b);
                auto difference = normal > p ? normal - p : p - normal;
                if (difference <= 100 * 10000ull) {
                    getter = precise;
                    break;
                }
            }
    }
    static std::uint64_t bits(FILETIME t) {
        return std::uint64_t(t.dwLowDateTime) | (std::uint64_t(t.dwHighDateTime) << 32);
    }
    static FILETIME file(std::uint64_t bits) {
        return {static_cast<DWORD>(bits), static_cast<DWORD>(bits >> 32)};
    }
    std::uint64_t read() {
        FILETIME f;
        getter(&f);
        return bits(f);
    }
    static AssetDateTime create(const SYSTEMTIME& t, std::uint64_t fraction) {
        using namespace std::chrono;
        auto ticks =
            std::uint64_t((sys_days{year(t.wYear) / t.wMonth / t.wDay} - sys_days{year(1) / 1 / 1}).count()) *
                day +
            (t.wHour * 3600ull + t.wMinute * 60ull) * second;
        if (t.wSecond <= 59)
            ticks += t.wSecond * second + t.wMilliseconds * 10000ull + fraction;
        else
            ticks += 60 * second - 1;
        return AssetDateTime::from_serialized(ticks | utc_kind);
    }
    static AssetDateTime fallback() {
        SYSTEMTIME st;
        GetSystemTime(&st);
        return create(st, 0);
    }
    AssetDateTime update() {
        auto now = read();
        auto f = file(now);
        SYSTEMTIME st{};
        if (!FileTimeToSystemTime(&f, &st))
            return fallback();
        auto fraction = now % 10000ull;
        if (st.wSecond >= 60)
            return create(st, fraction);
        auto endfile = file(now + window);
        SYSTEMTIME end{};
        if (!FileTimeToSystemTime(&endfile, &end))
            return fallback();
        std::uint64_t file_start, date_start;
        if (end.wSecond == st.wSecond) {
            file_start = now;
            date_start = create(st, fraction).serialized_data();
        } else {
            auto beginning = st;
            beginning.wHour = beginning.wMinute = beginning.wSecond = beginning.wMilliseconds = 0;
            FILETIME beginfile{};
            if (!SystemTimeToFileTime(&beginning, &beginfile))
                return fallback();
            file_start = bits(beginfile) + (day - second) - window;
            if (now - file_start >= window)
                return create(st, fraction);
            date_start = create(beginning, 0).serialized_data() + (day - second) - window;
        }
        cache.store(std::make_shared<Cache>(Cache{file_start, date_start}));
        return AssetDateTime::from_serialized(date_start + now - file_start);
    }
    AssetDateTime now() {
        auto filetime = read();
        if (!leap)
            return AssetDateTime::from_serialized(filetime + (file_epoch | utc_kind));
        auto c = cache.load();
        auto since = filetime - c->file_start;
        if (since < window)
            return AssetDateTime::from_serialized(c->date_start + since);
        return update();
    }
};
Clock& clock() {
    static Clock value;
    return value;
}
#endif
}
AssetDateTime native_utc_now() {
#ifdef _WIN32
    return clock().now();
#else
    std::int64_t ticks = 0;
#if defined(CLOCK_REALTIME)
    timespec t{};
    if (::clock_gettime(CLOCK_REALTIME, &t) == 0)
        ticks = std::int64_t(t.tv_sec) * 10000000ll + t.tv_nsec / 100;
#else
    timeval t{};
    if (::gettimeofday(&t, nullptr) == 0)
        ticks = std::int64_t(t.tv_sec) * 10000000ll + t.tv_usec * 10ll;
#endif
    return AssetDateTime::from_serialized(static_cast<std::uint64_t>(ticks + epoch) | utc_kind);
#endif
}

AssetDateTime native_from_file_time_utc(std::int64_t file_time) {
#ifdef _WIN32
    if (clock().leap) {
        auto file = Clock::file(static_cast<std::uint64_t>(file_time));
        SYSTEMTIME time{};
        if (!FileTimeToSystemTime(&file, &time))
            throw NativeDateTimeError(
                "Ticks must be between DateTime.MinValue.Ticks and DateTime.MaxValue.Ticks. (Parameter 'fileTime')",
                "fileTime");
        return Clock::create(time, static_cast<std::uint64_t>(file_time) % 10000ull);
    }
#endif
    return AssetDateTime(file_time + 504911232000000000ll, 1);
}
std::int64_t native_to_file_time_utc(std::int64_t ticks) {
#ifdef _WIN32
    if (clock().leap) {
        using namespace std::chrono;
        auto date = year_month_day{sys_days{year(1) / 1 / 1} + days(ticks / 864000000000ll)};
        SYSTEMTIME time{};
        time.wYear = static_cast<WORD>(int(date.year()));
        time.wMonth = static_cast<WORD>(unsigned(date.month()));
        time.wDay = static_cast<WORD>(unsigned(date.day()));
        time.wHour = static_cast<WORD>(ticks / 36000000000ll % 24);
        time.wMinute = static_cast<WORD>(ticks / 600000000ll % 60);
        time.wSecond = static_cast<WORD>(ticks / 10000000ll % 60);
        FILETIME file{};
        if (!SystemTimeToFileTime(&time, &file))
            throw NativeDateTimeError("Not a valid Win32 FileTime.");
        return static_cast<std::int64_t>(Clock::bits(file)) + ticks % 10000000ll;
    }
#endif
    auto file = ticks - 504911232000000000ll;
    if (file < 0)
        throw NativeDateTimeError("Not a valid Win32 FileTime.");
    return file;
}

bool native_valid_leap_second(int y, int m, int d, int h, int minute) {
#ifdef _WIN32
    if (!clock().leap)
        return false;
    SYSTEMTIME value{static_cast<WORD>(y), static_cast<WORD>(m),      0,  static_cast<WORD>(d),
                     static_cast<WORD>(h), static_cast<WORD>(minute), 60, 0},
        utc{};
    if (TzSpecificLocalTimeToSystemTime(nullptr, &value, &utc))
        return true;
    FILETIME file{};
    return SystemTimeToFileTime(&value, &file) != FALSE;
#else
    (void)y;
    (void)m;
    (void)d;
    (void)h;
    (void)minute;
    return false;
#endif
}
int native_two_digit_year_max(bool invariant) {
#ifdef _WIN32
    if (invariant)
        return 2049;
    DWORD year = 0;
    return GetCalendarInfoEx(nullptr, CAL_GREGORIAN, nullptr, CAL_ITWODIGITYEARMAX | CAL_RETURN_NUMBER,
                             nullptr, 0, &year) &&
                   static_cast<std::int32_t>(year) >= 0
               ? static_cast<int>(year)
               : 2049;
#else
    (void)invariant;
    return 2049;
#endif
}
}
