// Copyright (c) Contributors, http://opensimulator.org/
// Portions ported from dotnet/runtime v8.0.31, .NET Foundation, MIT license.
// Source: System.Private.CoreLib/System/Globalization/DateTimeParse.cs,
// DateTimeFormatInfo.cs and HebrewNumber.cs. Retain the MIT notice; see
// third_party/dotnet_collation/LICENSE. This module is invariant/stylesNone.
#include "opensim/framework/invariant_datetime_parse.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/viewer_unicode_data.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include <tuple>
namespace opensim::framework {
namespace {
constexpr std::int64_t maximum_ticks = 3155378975999999999ll;
constexpr std::int64_t day_ticks = 864000000000ll, second_ticks = 10000000ll;
bool letter(char16_t c) {
    for (const auto& range : viewer_unicode::letters) {
        if (c < range[0])
            return false;
        if (c <= range[1])
            return true;
    }
    return false;
}
char16_t lower(char16_t c) {
    auto& pairs = viewer_unicode::lower;
    auto it = std::lower_bound(pairs.begin(), pairs.end(), static_cast<std::uint32_t>(c),
                               [](const auto& pair, auto value) { return pair[0] < value; });
    return it != pairs.end() && (*it)[0] == c ? static_cast<char16_t>((*it)[1]) : c;
}
struct Text {
    std::u16string_view value;
    int Length = 0;
    constexpr Text() = default;
    constexpr Text(std::u16string_view v) : value(v), Length(static_cast<int>(v.size())) {
    }
    template <std::size_t N> constexpr Text(const char16_t (&v)[N]) : Text(std::u16string_view(v, N - 1)) {
    }
    char16_t operator[](int i) const {
        return value.at(static_cast<std::size_t>(i));
    }
    Text Slice(int start) const {
        return Text(value.substr(static_cast<std::size_t>(start)));
    }
    Text Slice(int start, int length) const {
        return Text(value.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(length)));
    }
    bool StartsWith(Text other) const {
        return value.starts_with(other.value);
    }
    constexpr Text(const char16_t* v) : Text(std::u16string_view(v)) {
    }
    bool ContainsAnyExcept(char16_t c) const {
        return std::any_of(value.begin(), value.end(), [c](auto v) { return v != c; });
    }
    bool Equals(Text other, int = 0) const {
        return value == other.value;
    }
};
bool operator==(Text a, Text b) {
    return a.value == b.value;
}
namespace DK {
constexpr int Unspecified = 0, Utc = 1, Local = 2;
}
struct DateTime {
    std::int64_t Ticks = 0;
    int Kind = 0;
    bool ambiguous = false;
    DateTime() = default;
    DateTime(std::int64_t ticks, int kind = 0, bool dst = false) : Ticks(ticks), Kind(kind), ambiguous(dst) {
    }
    explicit DateTime(const AssetDateTime& date)
        : Ticks(date.ticks()), Kind(date.kind()), ambiguous((date.serialized_data() >> 62) == 3) {
    }
    bool TryAddTicks(std::int64_t ticks, DateTime& result) const {
        if (ticks > maximum_ticks - Ticks || ticks < -Ticks) {
            result = {};
            return false;
        }
        result = DateTime(Ticks + ticks, Kind, ambiguous);
        return true;
    }
};
struct TimeSpan {
    std::int64_t Ticks = 0;
    TimeSpan() = default;
    TimeSpan(int h, int m, int s)
        : Ticks((std::int64_t(h) * 3600 + std::int64_t(m) * 60 + s) * second_ticks) {
    }
    explicit TimeSpan(std::int64_t ticks) : Ticks(ticks) {
    }
    TimeSpan Negate() const {
        return TimeSpan(-Ticks);
    }
};
struct Gregorian {
    const InvariantDateTimeParseContext& ctx;
    int ID = 1;
    static constexpr std::int64_t TicksPerSecond = second_ticks;
    bool leap(int year) const {
        return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    }
    int days_in_month(int year, int month) const {
        static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return month == 2 && leap(year) ? 29 : days[month - 1];
    }
    bool IsValidDay(int year, int month, int day, int era) const {
        return (era == 0 || era == 1) && year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 &&
               day <= days_in_month(year, month);
    }
    std::int64_t date_ticks(int year, int month, int day) const {
        static constexpr int before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
        auto y = std::int64_t(year - 1);
        return (y * 365 + y / 4 - y / 100 + y / 400 + before[month - 1] +
                ((month > 2 && leap(year)) ? 1 : 0) + day - 1) *
               day_ticks;
    }
    bool TryToDateTime(int year, int month, int day, int hour, int minute, int second, int ms, int era,
                       DateTime& output) const {
        output = {};
        if (!IsValidDay(year, month, day, era) || hour < 0 || hour >= 24 || minute < 0 || minute >= 60 ||
            second < 0 || second > 60 || ms < 0 || ms >= 1000)
            return false;
        if (second == 60) {
            if (!ctx.valid_leap_second(year, month, day, hour, minute))
                return false;
            second = 59;
            ms = 999;
        }
        output = DateTime(date_ticks(year, month, day) +
                          (std::int64_t(hour) * 3600 + minute * 60 + second) * second_ticks + ms * 10000ll);
        return true;
    }
    bool TryCreate(int y, int m, int d, int h, int minute, int sec, int ms, DateTime& result) const {
        return TryToDateTime(y, m, d, h, minute, sec, ms, 0, result);
    }
    int ToFourDigitYear(int year) const {
        if (year < 0 || year > 9999)
            throw std::out_of_range("Gregorian year");
        if (year < 100) {
            int max = ctx.two_digit_year_max();
            year += (max / 100 - (year > max % 100 ? 1 : 0)) * 100;
        }
        return year;
    }
    std::array<int, 3> parts(DateTime date) const {
        auto days = date.Ticks / day_ticks;
        int year = static_cast<int>(days / 366) + 1;
        while (date_ticks(year + 1, 1, 1) <= date.Ticks)
            ++year;
        int month = 1;
        auto current = days - date_ticks(year, 1, 1) / day_ticks;
        while (month < 12 && current >= days_in_month(year, month)) {
            current -= days_in_month(year, month);
            ++month;
        }
        return {year, month, static_cast<int>(current) + 1};
    }
    int GetYear(DateTime d) const {
        return parts(d)[0];
    }
    int GetMonth(DateTime d) const {
        return parts(d)[1];
    }
    int GetDayOfMonth(DateTime d) const {
        return parts(d)[2];
    }
    int GetDayOfWeek(DateTime d) const {
        return static_cast<int>((d.Ticks / day_ticks + 1) % 7);
    }
};
std::int64_t round_even(double value) {
    auto whole = std::floor(value);
    auto fraction = value - whole;
    if (fraction > .5 || (fraction == .5 && std::fmod(whole, 2.) != 0))
        ++whole;
    return static_cast<std::int64_t>(whole);
}

enum class DTT : int {
    End = 0,
    NumEnd = 1,
    NumAmpm = 2,
    NumSpace = 3,
    NumDatesep = 4,
    NumTimesep = 5,
    MonthEnd = 6,
    MonthSpace = 7,
    MonthDatesep = 8,
    NumDatesuff = 9,
    NumTimesuff = 10,
    DayOfWeek = 11,
    YearSpace = 12,
    YearDateSep = 13,
    YearEnd = 14,
    TimeZone = 15,
    Era = 16,
    NumUTCTimeMark = 17,

    Unk = 18,
    NumLocalTimeMark = 19,
    Max = 20,
};
enum class TM : int {
    NotSet = -1,
    AM = 0,
    PM = 1,
};
enum class DS : int {
    BEGIN = 0,
    N = 1,
    NN = 2,

    D_Nd = 3,
    D_NN = 4,
    D_NNd = 5,

    D_M = 6,
    D_MN = 7,
    D_NM = 8,
    D_MNd = 9,
    D_NDS = 10,

    D_Y = 11,
    D_YN = 12,
    D_YNd = 13,
    D_YM = 14,
    D_YMd = 15,
    D_S = 16,
    T_S = 17,

    T_Nt = 18,
    T_NNt = 19,

    ERROR = 20,

    DX_NN = 21,
    DX_NNN = 22,
    DX_MN = 23,
    DX_NM = 24,
    DX_MNN = 25,
    DX_DS = 26,
    DX_DSN = 27,
    DX_NDS = 28,
    DX_NNDS = 29,

    DX_YNN = 30,
    DX_YMN = 31,
    DX_YN = 32,
    DX_YM = 33,
    TX_N = 34,
    TX_NN = 35,
    TX_NNN = 36,
    TX_TS = 37,
    DX_NNY = 38,
};
namespace TK {
enum Values : int {

    NumberToken = 1,
    YearNumberToken = 2,
    Am = 3,
    Pm = 4,
    MonthToken = 5,
    EndOfString = 6,
    DayOfWeekToken = 7,
    TimeZoneToken = 8,
    EraToken = 9,
    DateWordToken = 10,
    UnknownToken = 11,
    HebrewNumber = 12,
    JapaneseEraToken = 13,
    TEraToken = 14,
    IgnorableSymbol = 15,

    SEP_Unk = 0x100,
    SEP_End = 0x200,
    SEP_Space = 0x300,
    SEP_Am = 0x400,
    SEP_Pm = 0x500,
    SEP_Date = 0x600,
    SEP_Time = 0x700,
    SEP_YearSuff = 0x800,
    SEP_MonthSuff = 0x900,
    SEP_DaySuff = 0xa00,
    SEP_HourSuff = 0xb00,
    SEP_MinuteSuff = 0xc00,
    SEP_SecondSuff = 0xd00,
    SEP_LocalTimeMark = 0xe00,
    SEP_DateOrOffset = 0xf00,

    RegularTokenMask = 0x00ff,
    SeparatorTokenMask = 0xff00,
};
}
namespace PF {
enum Values : int {
    HaveYear = 0x00000001,
    HaveMonth = 0x00000002,
    HaveDay = 0x00000004,
    HaveHour = 0x00000008,
    HaveMinute = 0x00000010,
    HaveSecond = 0x00000020,
    HaveTime = 0x00000040,
    HaveDate = 0x00000080,
    TimeZoneUsed = 0x00000100,
    TimeZoneUtc = 0x00000200,
    ParsedMonthName = 0x00000400,
    CaptureOffset = 0x00000800,
    YearDefault = 0x00001000,
    Rfc1123Pattern = 0x00002000,
    UtcSortPattern = 0x00004000,
};
}
using TokenType = int;
using ParseFlags = int;
using DateTimeStyles = int;
namespace ST {
enum Values : int {
    None = 0,
    AllowLeadingWhite = 1,
    AllowTrailingWhite = 2,
    AllowInnerWhite = 4,
    NoCurrentDateDefault = 8,
    AdjustToUniversal = 16,
    AssumeLocal = 32,
    AssumeUniversal = 64,
    RoundtripKind = 128
};
}
static constexpr DS s_dateParsingStates[20][18] = {

    {DS::BEGIN, DS::ERROR, DS::TX_N, DS::N, DS::D_Nd, DS::T_Nt, DS::ERROR, DS::D_M, DS::D_M, DS::D_S, DS::T_S,
     DS::BEGIN, DS::D_Y, DS::D_Y, DS::ERROR, DS::BEGIN, DS::BEGIN, DS::ERROR},

    {DS::ERROR, DS::DX_NN, DS::TX_NN, DS::NN, DS::D_NNd, DS::ERROR, DS::DX_NM, DS::D_NM, DS::D_MNd, DS::D_NDS,
     DS::ERROR, DS::N, DS::D_YN, DS::D_YNd, DS::DX_YN, DS::N, DS::N, DS::ERROR},

    {DS::DX_NN, DS::DX_NNN, DS::TX_NNN, DS::DX_NNN, DS::ERROR, DS::T_Nt, DS::DX_MNN, DS::DX_MNN, DS::ERROR,
     DS::ERROR, DS::T_S, DS::NN, DS::DX_NNY, DS::ERROR, DS::DX_NNY, DS::NN, DS::NN, DS::ERROR},

    {DS::ERROR, DS::DX_NN, DS::ERROR, DS::D_NN, DS::D_NNd, DS::ERROR, DS::DX_NM, DS::D_MN, DS::D_MNd,
     DS::ERROR, DS::ERROR, DS::D_Nd, DS::D_YN, DS::D_YNd, DS::DX_YN, DS::ERROR, DS::D_Nd, DS::ERROR},

    {DS::DX_NN, DS::DX_NNN, DS::TX_N, DS::DX_NNN, DS::ERROR, DS::T_Nt, DS::DX_MNN, DS::DX_MNN, DS::ERROR,
     DS::DX_DS, DS::T_S, DS::D_NN, DS::DX_NNY, DS::ERROR, DS::DX_NNY, DS::ERROR, DS::D_NN, DS::ERROR},

    {DS::ERROR, DS::DX_NNN, DS::DX_NNN, DS::DX_NNN, DS::ERROR, DS::ERROR, DS::DX_MNN, DS::DX_MNN, DS::ERROR,
     DS::DX_DS, DS::ERROR, DS::D_NNd, DS::DX_NNY, DS::ERROR, DS::DX_NNY, DS::ERROR, DS::D_NNd, DS::ERROR},

    {DS::ERROR, DS::DX_MN, DS::ERROR, DS::D_MN, DS::D_MNd, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR,
     DS::ERROR, DS::ERROR, DS::D_M, DS::D_YM, DS::D_YMd, DS::DX_YM, DS::ERROR, DS::D_M, DS::ERROR},

    {DS::DX_MN, DS::DX_MNN, DS::DX_MNN, DS::DX_MNN, DS::ERROR, DS::T_Nt, DS::ERROR, DS::ERROR, DS::ERROR,
     DS::DX_DS, DS::T_S, DS::D_MN, DS::DX_YMN, DS::ERROR, DS::DX_YMN, DS::ERROR, DS::D_MN, DS::ERROR},

    {DS::DX_NM, DS::DX_MNN, DS::DX_MNN, DS::DX_MNN, DS::ERROR, DS::T_Nt, DS::ERROR, DS::ERROR, DS::ERROR,
     DS::DX_DS, DS::T_S, DS::D_NM, DS::DX_YMN, DS::ERROR, DS::DX_YMN, DS::ERROR, DS::D_NM, DS::ERROR},

    {DS::ERROR, DS::DX_MNN, DS::ERROR, DS::DX_MNN, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR,
     DS::ERROR, DS::ERROR, DS::D_MNd, DS::DX_YMN, DS::ERROR, DS::DX_YMN, DS::ERROR, DS::D_MNd, DS::ERROR},

    {DS::DX_NDS, DS::DX_NNDS, DS::DX_NNDS, DS::DX_NNDS, DS::ERROR, DS::T_Nt, DS::ERROR, DS::ERROR, DS::ERROR,
     DS::D_NDS, DS::T_S, DS::D_NDS, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::D_NDS, DS::ERROR},

    {DS::ERROR, DS::DX_YN, DS::ERROR, DS::D_YN, DS::D_YNd, DS::ERROR, DS::DX_YM, DS::D_YM, DS::D_YMd,
     DS::D_YM, DS::ERROR, DS::D_Y, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::D_Y, DS::ERROR},

    {DS::DX_YN, DS::DX_YNN, DS::DX_YNN, DS::DX_YNN, DS::ERROR, DS::ERROR, DS::DX_YMN, DS::DX_YMN, DS::ERROR,
     DS::ERROR, DS::ERROR, DS::D_YN, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::D_YN, DS::ERROR},

    {DS::ERROR, DS::DX_YNN, DS::DX_YNN, DS::DX_YNN, DS::ERROR, DS::ERROR, DS::DX_YMN, DS::DX_YMN, DS::ERROR,
     DS::ERROR, DS::ERROR, DS::D_YN, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::D_YN, DS::ERROR},

    {DS::DX_YM, DS::DX_YMN, DS::DX_YMN, DS::DX_YMN, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR,
     DS::ERROR, DS::ERROR, DS::D_YM, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::D_YM, DS::ERROR},

    {DS::ERROR, DS::DX_YMN, DS::DX_YMN, DS::DX_YMN, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR,
     DS::ERROR, DS::ERROR, DS::D_YM, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::D_YM, DS::ERROR},

    {DS::DX_DS, DS::DX_DSN, DS::TX_N, DS::T_Nt, DS::ERROR, DS::T_Nt, DS::ERROR, DS::ERROR, DS::ERROR, DS::D_S,
     DS::T_S, DS::D_S, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::D_S, DS::ERROR},

    {DS::TX_TS, DS::TX_TS, DS::TX_TS, DS::T_Nt, DS::D_Nd, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::D_S,
     DS::T_S, DS::T_S, DS::ERROR, DS::ERROR, DS::ERROR, DS::T_S, DS::T_S, DS::ERROR},

    {DS::ERROR, DS::TX_NN, DS::TX_NN, DS::TX_NN, DS::ERROR, DS::T_NNt, DS::DX_NM, DS::D_NM, DS::ERROR,
     DS::ERROR, DS::T_S, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::T_Nt, DS::T_Nt, DS::TX_NN},

    {DS::ERROR, DS::TX_NNN, DS::TX_NNN, DS::TX_NNN, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR, DS::ERROR,
     DS::ERROR, DS::T_S, DS::T_NNt, DS::ERROR, DS::ERROR, DS::ERROR, DS::T_NNt, DS::T_NNt, DS::TX_NNN},
};
constexpr int ORDER_YMD = 0;
constexpr int ORDER_MDY = 1;
constexpr int ORDER_DMY = 2;
constexpr int ORDER_YDM = 3;
constexpr int ORDER_YM = 4;
constexpr int ORDER_MY = 5;
constexpr int ORDER_MD = 6;
constexpr int ORDER_DM = 7;
struct DateTimeResult {
    int Year = -1, Month = -1, Day = -1, Hour = 0, Minute = 0, Second = 0, era = 0;
    int flags = 0;
    bool failure = false;
    Gregorian* calendar = nullptr;
    DateTime parsedDate;
    TimeSpan timeZoneOffset;
    void SetDate(int y, int m, int d) {
        Year = y;
        Month = m;
        Day = d;
    }
    template <class... Args> void SetFailure(Args&&...) {
        failure = true;
    }
    void SetBadDateTimeFailure() {
        failure = true;
    }
};
struct DateTimeRawInfo {
    std::array<int, 3> numbers{};
    int numCount = 0, month = -1, year = -1, dayOfWeek = -1, era = -1;
    TM timeMark = TM::NotSet;
    double fraction = -1;
    bool hasSameDateAndTimeSeparators = false;
    void AddNumber(int value) {
        if (numCount < 3)
            numbers[static_cast<std::size_t>(numCount++)] = value;
    }
    int GetNumber(int index) const {
        return numbers[static_cast<std::size_t>(index)];
    }
};
struct DateTimeToken {
    DTT dtt = DTT::End;
    int suffix = TK::SEP_Unk, num = 0;
};
enum class DTSubStringType { Unknown, Invalid, Number, End, Other };
struct DTSubString {
    Text s;
    int index = 0, length = 0, value = 0;
    DTSubStringType type = DTSubStringType::Unknown;
    char16_t operator[](int relative) const {
        return s[index + relative];
    }
};
namespace HB {
enum class HebrewToken : int {
    Invalid = -1,
    Digit400 = 0,
    Digit200_300 = 1,
    Digit100 = 2,
    Digit10 = 3,
    Digit1 = 4,
    Digit6_7 = 5,
    Digit7 = 6,
    Digit9 = 7,
    SingleQuote = 8,
    DoubleQuote = 9,
};
enum class HS : int {
    _err = -1,
    Start = 0,
    S400 = 1,
    S400_400 = 2,
    S400_X00 = 3,
    S400_X0 = 4,
    X00_DQ = 5,
    S400_X00_X0 = 6,
    X0_DQ = 7,
    X = 8,
    X0 = 9,
    X00 = 10,
    S400_DQ = 11,
    S400_400_DQ = 12,
    S400_400_100 = 13,
    S9 = 14,
    X00_S9 = 15,
    S9_DQ = 16,
    END = 100,
};
struct Value {
    HebrewToken token;
    int value;
};
static constexpr Value values[] = {
    {HebrewToken::Digit1, 1},         {HebrewToken::Digit1, 2},         {HebrewToken::Digit1, 3},
    {HebrewToken::Digit1, 4},         {HebrewToken::Digit1, 5},         {HebrewToken::Digit6_7, 6},
    {HebrewToken::Digit6_7, 7},       {HebrewToken::Digit1, 8},         {HebrewToken::Digit9, 9},
    {HebrewToken::Digit10, 10},       {HebrewToken::Invalid, -1},       {HebrewToken::Digit10, 20},
    {HebrewToken::Digit10, 30},       {HebrewToken::Invalid, -1},       {HebrewToken::Digit10, 40},
    {HebrewToken::Invalid, -1},       {HebrewToken::Digit10, 50},       {HebrewToken::Digit10, 60},
    {HebrewToken::Digit10, 70},       {HebrewToken::Invalid, -1},       {HebrewToken::Digit10, 80},
    {HebrewToken::Invalid, -1},       {HebrewToken::Digit10, 90},       {HebrewToken::Digit100, 100},
    {HebrewToken::Digit200_300, 200}, {HebrewToken::Digit200_300, 300}, {HebrewToken::Digit400, 400}};
static constexpr HS transitions[] = {

    HS::S400,     HS::X00,      HS::X00,          HS::X0,          HS::X,
    HS::X,        HS::X,        HS::S9,           HS::_err,        HS::_err,

    HS::S400_400, HS::S400_X00, HS::S400_X00,     HS::S400_X0,     HS::_err,
    HS::_err,     HS::_err,     HS::X00_S9,       HS::END,         HS::S400_DQ,

    HS::_err,     HS::_err,     HS::S400_400_100, HS::S400_X0,     HS::_err,
    HS::_err,     HS::_err,     HS::X00_S9,       HS::_err,        HS::S400_400_DQ,

    HS::_err,     HS::_err,     HS::_err,         HS::S400_X00_X0, HS::_err,
    HS::_err,     HS::_err,     HS::X00_S9,       HS::_err,        HS::X00_DQ,

    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::_err,
    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::X0_DQ,

    HS::_err,     HS::_err,     HS::_err,         HS::END,         HS::END,
    HS::END,      HS::END,      HS::END,          HS::_err,        HS::_err,

    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::_err,
    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::X0_DQ,

    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::END,
    HS::END,      HS::END,      HS::END,          HS::_err,        HS::_err,

    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::_err,
    HS::_err,     HS::_err,     HS::_err,         HS::END,         HS::_err,

    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::_err,
    HS::_err,     HS::_err,     HS::_err,         HS::END,         HS::X0_DQ,

    HS::_err,     HS::_err,     HS::_err,         HS::S400_X0,     HS::_err,
    HS::_err,     HS::_err,     HS::X00_S9,       HS::END,         HS::X00_DQ,

    HS::END,      HS::END,      HS::END,          HS::END,         HS::END,
    HS::END,      HS::END,      HS::END,          HS::_err,        HS::_err,

    HS::_err,     HS::_err,     HS::END,          HS::END,         HS::END,
    HS::END,      HS::END,      HS::END,          HS::_err,        HS::_err,

    HS::_err,     HS::_err,     HS::_err,         HS::S400_X00_X0, HS::_err,
    HS::_err,     HS::_err,     HS::X00_S9,       HS::_err,        HS::X00_DQ,

    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::_err,
    HS::_err,     HS::_err,     HS::_err,         HS::END,         HS::S9_DQ,

    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::_err,
    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::S9_DQ,

    HS::_err,     HS::_err,     HS::_err,         HS::_err,        HS::_err,
    HS::END,      HS::END,      HS::_err,         HS::_err,        HS::_err};
enum class Status { Invalid, NotDigit, End, Continue };
struct Context {
    HS state = HS::Start;
    int result = 0;
};
bool digit(char16_t c) {
    return (c >= 0x5d0 && c <= 0x5ea && values[c - 0x5d0].value >= 0) || c == u'\'' || c == u'"';
}
Status parse(char16_t c, Context& context) {
    HebrewToken token;
    if (c == u'\'')
        token = HebrewToken::SingleQuote;
    else if (c == u'"')
        token = HebrewToken::DoubleQuote;
    else {
        int i = c - 0x5d0;
        if (i < 0 || i >= static_cast<int>(std::size(values)))
            return Status::NotDigit;
        token = values[i].token;
        if (token == HebrewToken::Invalid)
            return Status::NotDigit;
        context.result += values[i].value;
    }
    context.state = transitions[static_cast<int>(context.state) * 10 + static_cast<int>(token)];
    if (context.state == HS::_err)
        return Status::Invalid;
    if (context.state == HS::END)
        return Status::End;
    return Status::Continue;
}
}
struct CompareInfo {
    bool mode = false;
    bool equals(Text a, Text b) const {
        return mode ? ordinal_equals_ignore_case(utf8(a.value), utf8(b.value))
                    : invariant_equals_ignore_case(utf8(a.value), utf8(b.value));
    }
    int Compare(Text a, Text b, int = 0) const {
        return equals(a, b) ? 0 : 1;
    }
};
struct DTString;
struct DateTimeFormatInfo {
    const InvariantDateTimeParseContext& ctx;
    Gregorian* Calendar;
    CompareInfo CompareInfoValue;
    Text DateSeparator = u"/", TimeSeparator = u":", ShortDatePattern = u"MM/dd/yyyy",
         YearMonthPattern = u"yyyy MMMM", MonthDayPattern = u"MMMM dd", AMDesignator = u"AM",
         PMDesignator = u"PM";
    int FormatFlags = 0;
    bool HasYearMonthAdjustment = false;
    bool YearMonthAdjustment(int&, int&, bool) const {
        return true;
    }
    struct Token {
        std::u16string text;
        int type = 0, value = 0;
    };
    std::array<Token, 199> table;
    DateTimeFormatInfo(const InvariantDateTimeParseContext&, Gregorian*);
    bool Tokenize(int mask, int& token, int& value, DTString&);
    void insert(Text, int, int);
    void insert_at(Text, char16_t, int, int, int, int, int);
};
struct DTString {
    Text Value;
    int Index = -1, Length = 0;
    char16_t m_current = 0;
    CompareInfo CompareInfoValue;
    DTString(Text value, DateTimeFormatInfo& format)
        : Value(value), Length(value.Length), CompareInfoValue(format.CompareInfoValue) {
    }
    bool GetNext() {
        if (++Index < Length) {
            m_current = Value[Index];
            return true;
        }
        return false;
    }
    bool AtEnd() const {
        return Index >= Length;
    }
    bool Advance(int count) {
        Index += count;
        if (Index < Length) {
            m_current = Value[Index];
            return true;
        }
        return false;
    }
    void GetRegularToken(int& token, int& value, DateTimeFormatInfo& format) {
        value = 0;
        if (Index >= Length) {
            token = TK::EndOfString;
            return;
        }
        for (;;) {
            if (m_current >= u'0' && m_current <= u'9') {
                std::uint32_t number = m_current - u'0';
                int start = Index;
                while (++Index < Length) {
                    m_current = Value[Index];
                    auto digit = m_current - u'0';
                    if (digit < 0 || digit > 9)
                        break;
                    number = number * 10 + static_cast<std::uint32_t>(digit);
                }
                value = std::bit_cast<std::int32_t>(number);
                if (Index - start > 8) {
                    token = TK::NumberToken;
                    value = -1;
                } else
                    token = Index - start < 3 ? TK::NumberToken : TK::YearNumberToken;
                return;
            }
            if (unicode_whitespace(m_current)) {
                while (++Index < Length) {
                    m_current = Value[Index];
                    if (!unicode_whitespace(m_current))
                        break;
                }
                if (Index == Length) {
                    token = TK::EndOfString;
                    return;
                }
                continue;
            }
            format.Tokenize(TK::RegularTokenMask, token, value, *this);
            return;
        }
    }
    bool SkipWhiteSpaceAndRtlMarkCurrent() {
        if (Index >= Length)
            return false;
        while (unicode_whitespace(m_current) || m_current == 0x200f) {
            if (++Index >= Length)
                return false;
            m_current = Value[Index];
        }
        return true;
    }
    int GetSeparatorToken(DateTimeFormatInfo& format, int& before, char16_t& character) {
        before = Index;
        character = m_current;
        if (!SkipWhiteSpaceAndRtlMarkCurrent())
            return TK::SEP_End;
        int token, value;
        if (!(m_current >= u'0' && m_current <= u'9'))
            return format.Tokenize(TK::SeparatorTokenMask, token, value, *this) ? token : TK::SEP_Space;
        return TK::SEP_Space;
    }
    bool Match(char16_t character) {
        if (++Index >= Length)
            return false;
        if (Value[Index] == character ||
            (character == u' ' && (Value[Index] == 0xa0 || Value[Index] == 0x202f))) {
            m_current = character;
            return true;
        }
        --Index;
        return false;
    }
    bool GetNextDigit() {
        return ++Index < Length && Value[Index] >= u'0' && Value[Index] <= u'9';
    }
    int GetDigit() const {
        return Value[Index] - u'0';
    }
    char16_t GetChar() const {
        return Value[Index];
    }
    void SkipWhiteSpaces() {
        int i = Index + 1;
        while (i < Length && unicode_whitespace(Value[i]))
            ++i;
        Index = i - 1;
    }
    DTSubString GetSubString() {
        DTSubString result;
        result.index = Index;
        result.s = Value;
        while (Index + result.length < Length) {
            auto ch = Value[Index + result.length];
            auto type = ch >= u'0' && ch <= u'9' ? DTSubStringType::Number : DTSubStringType::Other;
            if (result.length == 0)
                result.type = type;
            else if (result.type != type)
                break;
            ++result.length;
            if (type == DTSubStringType::Number) {
                if (result.length > 8) {
                    result.type = DTSubStringType::Invalid;
                    return result;
                }
                result.value = result.value * 10 + ch - u'0';
            } else
                break;
        }
        if (result.length == 0)
            result.type = DTSubStringType::End;
        return result;
    }
    void ConsumeSubString(const DTSubString& value) {
        Index = value.index + value.length;
        if (Index < Length)
            m_current = Value[Index];
    }
};
void DateTimeFormatInfo::insert_at(Text text, char16_t ch, int type, int value, int pos, int hash,
                                   int probe) {
    auto previous = table[hash];
    table[hash] = {std::u16string(text.value), type, value};
    while (++pos < 199) {
        hash += probe;
        if (hash >= 199)
            hash -= 199;
        auto next = table[hash];
        if (!next.text.empty() && lower(next.text[0]) != ch)
            continue;
        table[hash] = std::move(previous);
        if (next.text.empty())
            return;
        previous = std::move(next);
    }
}
void DateTimeFormatInfo::insert(Text text, int type, int value) {
    while (text.Length && unicode_whitespace(text[0]))
        text = text.Slice(1);
    while (text.Length && unicode_whitespace(text[text.Length - 1]))
        text = text.Slice(0, text.Length - 1);
    if (!text.Length)
        return;
    auto ch = lower(text[0]);
    int hash = ch % 199, probe = 1 + ch % 197;
    for (int pos = 0; pos < 199; ++pos) {
        auto& current = table[hash];
        if (current.text.empty()) {
            current = {std::u16string(text.value), type, value};
            return;
        }
        int length = static_cast<int>(current.text.size());
        if (text.Length >= length && CompareInfoValue.equals(text.Slice(0, length), Text(current.text))) {
            if (text.Length > length) {
                insert_at(text, ch, type, value, pos, hash, probe);
                return;
            }
            if (((current.type & TK::RegularTokenMask) == 0 && (type & TK::RegularTokenMask) != 0) ||
                ((current.type & TK::SeparatorTokenMask) == 0 && (type & TK::SeparatorTokenMask) != 0)) {
                current.type |= type;
                if (value)
                    current.value = value;
            }
            return;
        }
        hash += probe;
        if (hash >= 199)
            hash -= 199;
    }
}
DateTimeFormatInfo::DateTimeFormatInfo(const InvariantDateTimeParseContext& context, Gregorian* calendar)
    : ctx(context), Calendar(calendar), CompareInfoValue{context.globalization_invariant_mode} {
    insert(u",", TK::IgnorableSymbol, 0);
    insert(u".", TK::IgnorableSymbol, 0);
    insert(u":", TK::SEP_Time, 0);
    insert(u"AM", TK::Am | TK::SEP_Am, 0);
    insert(u"PM", TK::Pm | TK::SEP_Pm, 1);
    for (auto suffix : {u"\u5e74", u"\ub144"})
        insert(suffix, TK::SEP_YearSuff, 0);
    for (auto suffix : {u"\u6708", u"\uc6d4"})
        insert(suffix, TK::SEP_MonthSuff, 0);
    for (auto suffix : {u"\u65e5", u"\uc77c"})
        insert(suffix, TK::SEP_DaySuff, 0);
    for (auto suffix : {u"\u6642", u"\u65f6"})
        insert(suffix, TK::SEP_HourSuff, 0);
    insert(u"\u5206", TK::SEP_MinuteSuff, 0);
    insert(u"\u79d2", TK::SEP_SecondSuff, 0);
    insert(u"-", TK::SEP_DateOrOffset, 0);
    insert(u"/", TK::SEP_Date, 0);
    static constexpr Text months[] = {u"January",   u"February", u"March",    u"April",
                                      u"May",       u"June",     u"July",     u"August",
                                      u"September", u"October",  u"November", u"December"};
    static constexpr Text abbreviated[] = {u"Jan", u"Feb", u"Mar", u"Apr", u"May", u"Jun",
                                           u"Jul", u"Aug", u"Sep", u"Oct", u"Nov", u"Dec"};
    for (int i = 0; i < 12; ++i) {
        insert(months[i], TK::MonthToken, i + 1);
        insert(abbreviated[i], TK::MonthToken, i + 1);
    }
    for (int i = 0; i < 12; ++i)
        insert(abbreviated[i], TK::MonthToken, i + 1);
    static constexpr Text days[] = {u"Sunday",   u"Monday", u"Tuesday", u"Wednesday",
                                    u"Thursday", u"Friday", u"Saturday"};
    static constexpr Text short_days[] = {u"Sun", u"Mon", u"Tue", u"Wed", u"Thu", u"Fri", u"Sat"};
    for (int i = 0; i < 7; ++i) {
        insert(days[i], TK::DayOfWeekToken, i);
        insert(short_days[i], TK::DayOfWeekToken, i);
    }
    insert(u"A.D.", TK::EraToken, 1);
    insert(u"AD", TK::EraToken, 1);
    insert(u"T", TK::SEP_LocalTimeMark, 0);
    insert(u"GMT", TK::TimeZoneToken, 0);
    insert(u"Z", TK::TimeZoneToken, 0);
    insert(u"/", TK::SEP_Date, 0);
    insert(u":", TK::SEP_Time, 0);
}
bool DateTimeFormatInfo::Tokenize(int mask, int& token, int& value, DTString& str) {
    token = TK::UnknownToken;
    value = 0;
    auto ch = str.m_current;
    bool is_letter = letter(ch);
    if (is_letter) {
        ch = lower(ch);
        if (!ctx.globalization_invariant_mode && ch >= 0x590 && ch <= 0x5ff && mask == TK::RegularTokenMask) {
            int index = str.Index;
            if (HB::digit(str.Value[index])) {
                HB::Context context;
                HB::Status state;
                do {
                    state = HB::parse(str.Value[index++], context);
                    if (state == HB::Status::Invalid || state == HB::Status::NotDigit)
                        break;
                } while (index < str.Length && state != HB::Status::End);
                if (state == HB::Status::End) {
                    str.Advance(index - str.Index);
                    value = context.result;
                    token = TK::HebrewNumber;
                    return true;
                }
            }
        }
    }
    int hash = ch % 199, probe = 1 + ch % 197, remaining = str.Length - str.Index;
    for (int i = 0; i < 199; ++i) {
        auto& current = table[hash];
        if (current.text.empty())
            break;
        int length = static_cast<int>(current.text.size());
        if ((current.type & mask) > 0 && length <= remaining) {
            bool compare = true;
            if (is_letter && str.Index + length < str.Length)
                compare = !letter(str.Value[str.Index + length]);
            if (compare &&
                ((length == 1 && str.Value[str.Index] == current.text[0]) ||
                 CompareInfoValue.equals(str.Value.Slice(str.Index, length), Text(current.text)))) {
                token = current.type & mask;
                value = current.value;
                str.Advance(length);
                return true;
            }
        }
        hash += probe;
        if (hash >= 199)
            hash -= 199;
    }
    return false;
}
namespace SC {
constexpr int Ordinal = 0;
}
namespace DF {
constexpr int UseHebrewRule = 8;
}
bool ascii_digit(char16_t c) {
    return c >= u'0' && c <= u'9';
}
class Parser {
    const InvariantDateTimeParseContext& ctx;
    Gregorian cal;
    DateTimeFormatInfo format;
    static constexpr char16_t TimeDelimiter = u':';
    static constexpr char16_t TimeFractionDelimiterDot = u'.', TimeFractionDelimiterComma = u',';
    static constexpr Text GMTName = u"GMT", ZuluName = u"Z";
    bool MatchWord(DTString& str, Text target) {

        if (target.Length > (str.Value.Length - str.Index)) {
            return false;
        }

        if (str.CompareInfoValue.Compare(str.Value.Slice(str.Index, target.Length), target, 0) != 0) {
            return false;
        }

        int nextCharIndex = str.Index + target.Length;

        if (nextCharIndex < str.Value.Length) {
            char16_t nextCh = str.Value[nextCharIndex];
            if (letter(nextCh)) {
                return false;
            }
        }
        str.Index = nextCharIndex;
        if (str.Index < str.Length) {
            str.m_current = str.Value[str.Index];
        }

        return true;
    }
    bool GetTimeZoneName(DTString& str) {

        if (MatchWord(str, GMTName)) {
            return true;
        }

        if (MatchWord(str, ZuluName)) {
            return true;
        }

        return false;
    }
    bool ParseFraction(DTString& str, double& result) {

        result = 0;
        double decimalBase = 0.1;
        int digits = 0;
        char16_t ch;
        while (str.GetNext() && ascii_digit(ch = str.m_current)) {
            result += (ch - u'0') * decimalBase;
            decimalBase *= 0.1;
            digits++;
        }
        return digits > 0;
    }
    bool ParseTimeZone(DTString& str, TimeSpan& result) {

        int hourOffset;
        int minuteOffset = 0;

        DTSubString sub = str.GetSubString();
        if (sub.length != 1) {
            return false;
        }
        char16_t offsetChar = sub[0];
        if (offsetChar != u'+' && offsetChar != u'-') {
            return false;
        }
        str.ConsumeSubString(sub);

        sub = str.GetSubString();
        if (sub.type != DTSubStringType::Number) {
            return false;
        }
        int value = sub.value;
        int length = sub.length;
        if (length == 1 || length == 2) {

            hourOffset = value;
            str.ConsumeSubString(sub);

            sub = str.GetSubString();
            if (sub.length == 1 && sub[0] == TimeDelimiter) {

                str.ConsumeSubString(sub);
                sub = str.GetSubString();
                if (sub.type != DTSubStringType::Number || sub.length < 1 || sub.length > 2) {
                    return false;
                }
                minuteOffset = sub.value;
                str.ConsumeSubString(sub);
            }
        } else if (length == 3 || length == 4) {

            hourOffset = value / 100;
            minuteOffset = value % 100;
            str.ConsumeSubString(sub);
        } else {

            return false;
        }

        if (minuteOffset < 0 || minuteOffset >= 60) {
            return false;
        }

        result = TimeSpan(hourOffset, minuteOffset, 0);
        if (offsetChar == u'-') {
            result = result.Negate();
        }
        return true;
    }
    bool HandleTimeZone(DTString& str, DateTimeResult& result) {

        if (str.Index < str.Length - 1) {
            char16_t nextCh = str.Value[str.Index];

            int whitespaceCount = 0;
            while (unicode_whitespace(nextCh) && str.Index + whitespaceCount < str.Length - 1) {
                whitespaceCount++;
                nextCh = str.Value[str.Index + whitespaceCount];
            }
            if (nextCh == u'+' || nextCh == u'-') {
                str.Index += whitespaceCount;
                if ((result.flags & PF::TimeZoneUsed) != 0) {

                    result.SetBadDateTimeFailure();
                    return false;
                }
                result.flags |= PF::TimeZoneUsed;
                if (!ParseTimeZone(str, result.timeZoneOffset)) {
                    result.SetBadDateTimeFailure();
                    return false;
                }
            }
        }
        return true;
    }
    bool Lex(DS dps, DTString& str, DateTimeToken& dtok, DateTimeRawInfo& raw, DateTimeResult& result,
             DateTimeFormatInfo& dtfi, DateTimeStyles styles) {
        TokenType tokenType{};
        int tokenValue{};

        int indexBeforeSeparator;
        char16_t charBeforeSeparator;

        TokenType sep;
        dtok.dtt = DTT::Unk;

        str.GetRegularToken(tokenType, tokenValue, dtfi);

        switch (tokenType) {
        case TK::NumberToken:
        case TK::YearNumberToken:
            if (raw.numCount == 3 || tokenValue == -1) {
                result.SetBadDateTimeFailure();

                return false;
            }

            if (dps == DS::T_NNt) {
                if (str.Index < str.Length - 1) {
                    char16_t nextCh = str.Value[str.Index];
                    if ((nextCh == TimeFractionDelimiterDot) || (nextCh == TimeFractionDelimiterComma)) {

                        ParseFraction(str, raw.fraction);
                    }
                }
            }
            if (dps == DS::T_NNt || dps == DS::T_Nt) {
                if (str.Index < str.Length - 1) {
                    if (!HandleTimeZone(str, result)) {

                        return false;
                    }
                }
            }

            dtok.num = tokenValue;
            if (tokenType == TK::YearNumberToken) {
                if (raw.year == -1) {
                    raw.year = tokenValue;

                    switch (sep = str.GetSeparatorToken(dtfi, indexBeforeSeparator, charBeforeSeparator)) {
                    case TK::SEP_End:
                        dtok.dtt = DTT::YearEnd;
                        break;
                    case TK::SEP_Am:
                    case TK::SEP_Pm:
                        if (raw.timeMark == TM::NotSet) {
                            raw.timeMark = (sep == TK::SEP_Am ? TM::AM : TM::PM);
                            dtok.dtt = DTT::YearSpace;
                        } else {
                            result.SetBadDateTimeFailure();
                        }
                        break;
                    case TK::SEP_Space:
                        dtok.dtt = DTT::YearSpace;
                        break;
                    case TK::SEP_Date:
                        dtok.dtt = DTT::YearDateSep;
                        break;
                    case TK::SEP_Time:
                        if (!raw.hasSameDateAndTimeSeparators) {
                            result.SetBadDateTimeFailure();

                            return false;
                        }

                        dtok.dtt = DTT::YearDateSep;
                        break;
                    case TK::SEP_DateOrOffset:

                        if ((s_dateParsingStates[(int)dps][(int)DTT::YearDateSep] == DS::ERROR) &&
                            (s_dateParsingStates[(int)dps][(int)DTT::YearSpace] > DS::ERROR)) {
                            str.Index = indexBeforeSeparator;
                            str.m_current = charBeforeSeparator;
                            dtok.dtt = DTT::YearSpace;
                        } else {
                            dtok.dtt = DTT::YearDateSep;
                        }
                        break;
                    case TK::SEP_YearSuff:
                    case TK::SEP_MonthSuff:
                    case TK::SEP_DaySuff:
                        dtok.dtt = DTT::NumDatesuff;
                        dtok.suffix = sep;
                        break;
                    case TK::SEP_HourSuff:
                    case TK::SEP_MinuteSuff:
                    case TK::SEP_SecondSuff:
                        dtok.dtt = DTT::NumTimesuff;
                        dtok.suffix = sep;
                        break;
                    default:

                        result.SetBadDateTimeFailure();

                        return false;
                    }

                    return true;
                }
                result.SetBadDateTimeFailure();

                return false;
            }
            switch (sep = str.GetSeparatorToken(dtfi, indexBeforeSeparator, charBeforeSeparator)) {

            case TK::SEP_End:
                dtok.dtt = DTT::NumEnd;
                raw.AddNumber(dtok.num);
                break;
            case TK::SEP_Am:
            case TK::SEP_Pm:
                if (raw.timeMark == TM::NotSet) {
                    raw.timeMark = (sep == TK::SEP_Am ? TM::AM : TM::PM);
                    dtok.dtt = DTT::NumAmpm;

                    if (dps == DS::D_NN) {
                        if (!ProcessTerminalState(DS::DX_NN, result, styles, raw, dtfi)) {
                            return false;
                        }
                    }

                    raw.AddNumber(dtok.num);
                } else {
                    result.SetBadDateTimeFailure();
                    break;
                }
                if (dps == DS::T_NNt || dps == DS::T_Nt) {
                    if (!HandleTimeZone(str, result)) {

                        return false;
                    }
                }
                break;
            case TK::SEP_Space:
                dtok.dtt = DTT::NumSpace;
                raw.AddNumber(dtok.num);
                break;
            case TK::SEP_Date:
                dtok.dtt = DTT::NumDatesep;
                raw.AddNumber(dtok.num);
                break;
            case TK::SEP_DateOrOffset:

                if ((s_dateParsingStates[(int)dps][(int)DTT::NumDatesep] == DS::ERROR) &&
                    (s_dateParsingStates[(int)dps][(int)DTT::NumSpace] > DS::ERROR)) {
                    str.Index = indexBeforeSeparator;
                    str.m_current = charBeforeSeparator;
                    dtok.dtt = DTT::NumSpace;
                } else {
                    dtok.dtt = DTT::NumDatesep;
                }
                raw.AddNumber(dtok.num);
                break;
            case TK::SEP_Time:
                if (raw.hasSameDateAndTimeSeparators &&
                    (dps == DS::D_Y || dps == DS::D_YN || dps == DS::D_YNd || dps == DS::D_YM ||
                     dps == DS::D_YMd)) {

                    dtok.dtt = DTT::NumDatesep;
                    raw.AddNumber(dtok.num);
                    break;
                }
                dtok.dtt = DTT::NumTimesep;
                raw.AddNumber(dtok.num);
                break;
            case TK::SEP_YearSuff:
                try {
                    dtok.num = dtfi.Calendar->ToFourDigitYear(tokenValue);
                } catch (const std::out_of_range&) {
                    result.SetBadDateTimeFailure();

                    return false;
                }
                dtok.dtt = DTT::NumDatesuff;
                dtok.suffix = sep;
                break;
            case TK::SEP_MonthSuff:
            case TK::SEP_DaySuff:
                dtok.dtt = DTT::NumDatesuff;
                dtok.suffix = sep;
                break;
            case TK::SEP_HourSuff:
            case TK::SEP_MinuteSuff:
            case TK::SEP_SecondSuff:
                dtok.dtt = DTT::NumTimesuff;
                dtok.suffix = sep;
                break;
            case TK::SEP_LocalTimeMark:
                dtok.dtt = DTT::NumLocalTimeMark;
                raw.AddNumber(dtok.num);
                break;
            default:

                result.SetBadDateTimeFailure();

                return false;
            }
            break;
        case TK::HebrewNumber:
            if (tokenValue >= 100) {

                if (raw.year == -1) {
                    raw.year = tokenValue;

                    switch (sep = str.GetSeparatorToken(dtfi, indexBeforeSeparator, charBeforeSeparator)) {
                    case TK::SEP_End:
                        dtok.dtt = DTT::YearEnd;
                        break;
                    case TK::SEP_Space:
                        dtok.dtt = DTT::YearSpace;
                        break;
                    case TK::SEP_DateOrOffset:

                        if (s_dateParsingStates[(int)dps][(int)DTT::YearSpace] > DS::ERROR) {
                            str.Index = indexBeforeSeparator;
                            str.m_current = charBeforeSeparator;
                            dtok.dtt = DTT::YearSpace;
                            break;
                        }
                        goto hebrew_default;
                    default:
                    hebrew_default:

                        result.SetBadDateTimeFailure();

                        return false;
                    }
                } else {

                    result.SetBadDateTimeFailure();

                    return false;
                }
            } else {

                dtok.num = tokenValue;
                raw.AddNumber(dtok.num);

                switch (sep = str.GetSeparatorToken(dtfi, indexBeforeSeparator, charBeforeSeparator)) {

                case TK::SEP_End:
                    dtok.dtt = DTT::NumEnd;
                    break;
                case TK::SEP_Space:
                case TK::SEP_Date:
                    dtok.dtt = DTT::NumDatesep;
                    break;
                case TK::SEP_DateOrOffset:

                    if ((s_dateParsingStates[(int)dps][(int)DTT::NumDatesep] == DS::ERROR) &&
                        (s_dateParsingStates[(int)dps][(int)DTT::NumSpace] > DS::ERROR)) {
                        str.Index = indexBeforeSeparator;
                        str.m_current = charBeforeSeparator;
                        dtok.dtt = DTT::NumSpace;
                    } else {
                        dtok.dtt = DTT::NumDatesep;
                    }
                    break;
                default:

                    result.SetBadDateTimeFailure();

                    return false;
                }
            }
            break;
        case TK::DayOfWeekToken:
            if (raw.dayOfWeek == -1) {

                raw.dayOfWeek = tokenValue;
                dtok.dtt = DTT::DayOfWeek;
            } else {
                result.SetBadDateTimeFailure();

                return false;
            }
            break;
        case TK::MonthToken:
            if (raw.month == -1) {

                switch (sep = str.GetSeparatorToken(dtfi, indexBeforeSeparator, charBeforeSeparator)) {
                case TK::SEP_End:
                    dtok.dtt = DTT::MonthEnd;
                    break;
                case TK::SEP_Space:
                    dtok.dtt = DTT::MonthSpace;
                    break;
                case TK::SEP_Date:
                    dtok.dtt = DTT::MonthDatesep;
                    break;
                case TK::SEP_Time:
                    if (!raw.hasSameDateAndTimeSeparators) {
                        result.SetBadDateTimeFailure();

                        return false;
                    }

                    dtok.dtt = DTT::MonthDatesep;
                    break;
                case TK::SEP_DateOrOffset:

                    if ((s_dateParsingStates[(int)dps][(int)DTT::MonthDatesep] == DS::ERROR) &&
                        (s_dateParsingStates[(int)dps][(int)DTT::MonthSpace] > DS::ERROR)) {
                        str.Index = indexBeforeSeparator;
                        str.m_current = charBeforeSeparator;
                        dtok.dtt = DTT::MonthSpace;
                    } else {
                        dtok.dtt = DTT::MonthDatesep;
                    }
                    break;
                default:

                    result.SetBadDateTimeFailure();

                    return false;
                }
                raw.month = tokenValue;
            } else {
                result.SetBadDateTimeFailure();

                return false;
            }
            break;
        case TK::EraToken:
            if (result.era != -1) {
                result.era = tokenValue;
                dtok.dtt = DTT::Era;
            } else {
                result.SetBadDateTimeFailure();

                return false;
            }
            break;
        case TK::JapaneseEraToken:
            return false;
        case TK::TEraToken:
            return false;
        case TK::TimeZoneToken:

            if ((result.flags & PF::TimeZoneUsed) != 0) {

                result.SetBadDateTimeFailure();

                return false;
            }
            dtok.dtt = DTT::TimeZone;
            result.flags |= PF::TimeZoneUsed;
            result.timeZoneOffset = TimeSpan(0);
            result.flags |= PF::TimeZoneUtc;
            break;
        case TK::EndOfString:
            dtok.dtt = DTT::End;
            break;
        case TK::DateWordToken:
        case TK::IgnorableSymbol:

            break;
        case TK::Am:
        case TK::Pm:
            if (raw.timeMark == TM::NotSet) {
                raw.timeMark = (TM)tokenValue;
            } else {
                result.SetBadDateTimeFailure();

                return false;
            }
            break;
        case TK::UnknownToken:
            if (letter(str.m_current)) {
                result.failure = true;

                return false;
            }

            if ((str.m_current == u'-' || str.m_current == u'+') &&
                ((result.flags & PF::TimeZoneUsed) == 0)) {
                int originalIndex = str.Index;
                if (ParseTimeZone(str, result.timeZoneOffset)) {
                    result.flags |= PF::TimeZoneUsed;

                    return true;
                } else {

                    str.Index = originalIndex;
                }
            }

            if (VerifyValidPunctuation(str)) {

                return true;
            }

            result.SetBadDateTimeFailure();

            return false;
        }

        return true;
    }
    bool VerifyValidPunctuation(DTString& str) {

        char16_t ch = str.Value[str.Index];
        if (ch == u'#') {
            bool foundStart = false;
            bool foundEnd = false;
            for (int i = 0; i < str.Length; i++) {
                ch = str.Value[i];
                if (ch == u'#') {
                    if (foundStart) {
                        if (foundEnd) {

                            return false;
                        } else {
                            foundEnd = true;
                        }
                    } else {
                        foundStart = true;
                    }
                } else if (ch == u'\0') {

                    if (!foundEnd) {
                        return false;
                    }
                } else if (!unicode_whitespace(ch)) {

                    if (!foundStart || foundEnd) {
                        return false;
                    }
                }
            }
            if (!foundEnd) {

                return false;
            }

            str.GetNext();
            return true;
        } else if (ch == u'\0') {

            if (str.Value.Slice(str.Index + 1).ContainsAnyExcept(u'\0')) {
                return false;
            }

            str.Index = str.Length;
            return true;
        }
        return false;
    }
    bool GetYearMonthDayOrder(Text datePattern, int& order) {

        int yearOrder = -1;
        int monthOrder = -1;
        int dayOrder = -1;
        int orderCount = 0;

        bool inQuote = false;

        for (int i = 0; i < datePattern.Length && orderCount < 3; i++) {
            char16_t ch = datePattern[i];
            if (ch == u'\\' || ch == u'%') {
                i++;
                continue;
            }

            if (ch == u'\'' || ch == u'"') {
                inQuote = !inQuote;
            }

            if (!inQuote) {
                if (ch == u'y') {
                    yearOrder = orderCount++;

                    for (; i + 1 < datePattern.Length && datePattern[i + 1] == u'y'; i++) {
                    }
                } else if (ch == u'M') {
                    monthOrder = orderCount++;

                    for (; i + 1 < datePattern.Length && datePattern[i + 1] == u'M'; i++) {
                    }
                } else if (ch == u'd') {
                    int patternCount = 1;

                    for (; i + 1 < datePattern.Length && datePattern[i + 1] == u'd'; i++) {
                        patternCount++;
                    }

                    if (patternCount <= 2) {
                        dayOrder = orderCount++;
                    }
                }
            }
        }

        if (yearOrder == 0 && monthOrder == 1 && dayOrder == 2) {
            order = ORDER_YMD;
            return true;
        }
        if (monthOrder == 0 && dayOrder == 1 && yearOrder == 2) {
            order = ORDER_MDY;
            return true;
        }
        if (dayOrder == 0 && monthOrder == 1 && yearOrder == 2) {
            order = ORDER_DMY;
            return true;
        }
        if (yearOrder == 0 && dayOrder == 1 && monthOrder == 2) {
            order = ORDER_YDM;
            return true;
        }
        order = -1;
        return false;
    }
    bool GetYearMonthOrder(Text pattern, int& order) {

        int yearOrder = -1;
        int monthOrder = -1;
        int orderCount = 0;

        bool inQuote = false;
        for (int i = 0; i < pattern.Length && orderCount < 2; i++) {
            char16_t ch = pattern[i];
            if (ch == u'\\' || ch == u'%') {
                i++;
                continue;
            }

            if (ch == u'\'' || ch == u'"') {
                inQuote = !inQuote;
            }

            if (!inQuote) {
                if (ch == u'y') {
                    yearOrder = orderCount++;

                    for (; i + 1 < pattern.Length && pattern[i + 1] == u'y'; i++) {
                    }
                } else if (ch == u'M') {
                    monthOrder = orderCount++;

                    for (; i + 1 < pattern.Length && pattern[i + 1] == u'M'; i++) {
                    }
                }
            }
        }

        if (yearOrder == 0 && monthOrder == 1) {
            order = ORDER_YM;
            return true;
        }
        if (monthOrder == 0 && yearOrder == 1) {
            order = ORDER_MY;
            return true;
        }
        order = -1;
        return false;
    }
    bool GetMonthDayOrder(Text pattern, int& order) {

        int monthOrder = -1;
        int dayOrder = -1;
        int orderCount = 0;

        bool inQuote = false;
        for (int i = 0; i < pattern.Length && orderCount < 2; i++) {
            char16_t ch = pattern[i];
            if (ch == u'\\' || ch == u'%') {
                i++;
                continue;
            }

            if (ch == u'\'' || ch == u'"') {
                inQuote = !inQuote;
            }

            if (!inQuote) {
                if (ch == u'd') {
                    int patternCount = 1;

                    for (; i + 1 < pattern.Length && pattern[i + 1] == u'd'; i++) {
                        patternCount++;
                    }

                    if (patternCount <= 2) {
                        dayOrder = orderCount++;
                    }
                } else if (ch == u'M') {
                    monthOrder = orderCount++;

                    for (; i + 1 < pattern.Length && pattern[i + 1] == u'M'; i++) {
                    }
                }
            }
        }

        if (monthOrder == 0 && dayOrder == 1) {
            order = ORDER_MD;
            return true;
        }
        if (dayOrder == 0 && monthOrder == 1) {
            order = ORDER_DM;
            return true;
        }
        order = -1;
        return false;
    }
    bool TryAdjustYear(DateTimeResult& result, int year, int& adjustedYear) {

        if (year < 100) {
            try {

                year = result.calendar->ToFourDigitYear(year);
            } catch (const std::out_of_range&) {
                adjustedYear = -1;
                return false;
            }
        }
        adjustedYear = year;
        return true;
    }
    bool SetDateYMD(DateTimeResult& result, int year, int month, int day) {

        if (result.calendar->IsValidDay(year, month, day, result.era)) {
            result.SetDate(year, month, day);
            return true;
        }
        return false;
    }
    bool SetDateMDY(DateTimeResult& result, int month, int day, int year) {

        return SetDateYMD(result, year, month, day);
    }
    bool SetDateDMY(DateTimeResult& result, int day, int month, int year) {

        return SetDateYMD(result, year, month, day);
    }
    bool SetDateYDM(DateTimeResult& result, int year, int day, int month) {

        return SetDateYMD(result, year, month, day);
    }
    void GetDefaultYear(DateTimeResult& result, DateTimeStyles& styles) {

        result.Year = result.calendar->GetYear(GetDateTimeNow(result, styles));
        result.flags |= PF::YearDefault;
    }
    bool GetDayOfNN(DateTimeResult& result, DateTimeStyles& styles, DateTimeRawInfo& raw,
                    DateTimeFormatInfo& dtfi) {
        int order{};

        if ((result.flags & PF::HaveDate) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        int n1 = raw.GetNumber(0);
        int n2 = raw.GetNumber(1);

        GetDefaultYear(result, styles);

        if (!GetMonthDayOrder(dtfi.MonthDayPattern, order)) {
            result.failure = true;
            return false;
        }

        if (order == ORDER_MD) {
            if (SetDateYMD(result, result.Year, n1, n2)) {
                result.flags |= PF::HaveDate;
                return true;
            }
        } else {

            if (SetDateYMD(result, result.Year, n2, n1)) {
                result.flags |= PF::HaveDate;
                return true;
            }
        }
        result.SetBadDateTimeFailure();
        return false;
    }
    bool GetDayOfNNN(DateTimeResult& result, DateTimeRawInfo& raw, DateTimeFormatInfo& dtfi) {
        int order{};

        if ((result.flags & PF::HaveDate) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        int n1 = raw.GetNumber(0);
        int n2 = raw.GetNumber(1);
        int n3 = raw.GetNumber(2);

        if (!GetYearMonthDayOrder(dtfi.ShortDatePattern, order)) {
            result.failure = true;
            return false;
        }
        int year;

        if (order == ORDER_YMD) {
            if (TryAdjustYear(result, n1, year) && SetDateYMD(result, year, n2, n3)) {
                result.flags |= PF::HaveDate;
                return true;
            }
        } else if (order == ORDER_MDY) {
            if (TryAdjustYear(result, n3, year) && SetDateMDY(result, n1, n2, year)) {
                result.flags |= PF::HaveDate;
                return true;
            }
        } else if (order == ORDER_DMY) {
            if (TryAdjustYear(result, n3, year) && SetDateDMY(result, n1, n2, year)) {
                result.flags |= PF::HaveDate;
                return true;
            }
        } else if (order == ORDER_YDM) {
            if (TryAdjustYear(result, n1, year) && SetDateYDM(result, year, n2, n3)) {
                result.flags |= PF::HaveDate;
                return true;
            }
        }
        result.SetBadDateTimeFailure();
        return false;
    }
    bool GetDayOfMN(DateTimeResult& result, DateTimeStyles& styles, DateTimeRawInfo& raw,
                    DateTimeFormatInfo& dtfi) {
        int monthDayOrder{};
        int yearMonthOrder{};
        int year{};

        if ((result.flags & PF::HaveDate) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        if (!GetMonthDayOrder(dtfi.MonthDayPattern, monthDayOrder)) {
            result.failure = true;
            return false;
        }
        if (monthDayOrder == ORDER_DM) {
            if (!GetYearMonthOrder(dtfi.YearMonthPattern, yearMonthOrder)) {
                result.failure = true;
                return false;
            }
            if (yearMonthOrder == ORDER_MY) {
                if (!TryAdjustYear(result, raw.GetNumber(0), year) ||
                    !SetDateYMD(result, year, raw.month, 1)) {
                    result.SetBadDateTimeFailure();
                    return false;
                }
                return true;
            }
        }

        GetDefaultYear(result, styles);
        if (!SetDateYMD(result, result.Year, raw.month, raw.GetNumber(0))) {
            result.SetBadDateTimeFailure();
            return false;
        }
        return true;
    }
    bool GetHebrewDayOfNM(DateTimeResult& result, DateTimeRawInfo& raw, DateTimeFormatInfo& dtfi) {
        int monthDayOrder{};

        if (!GetMonthDayOrder(dtfi.MonthDayPattern, monthDayOrder)) {
            result.failure = true;
            return false;
        }
        result.Month = raw.month;
        if (monthDayOrder == ORDER_DM || monthDayOrder == ORDER_MD) {
            if (result.calendar->IsValidDay(result.Year, result.Month, raw.GetNumber(0), result.era)) {
                result.Day = raw.GetNumber(0);
                return true;
            }
        }
        result.SetBadDateTimeFailure();
        return false;
    }
    bool GetDayOfNM(DateTimeResult& result, DateTimeStyles& styles, DateTimeRawInfo& raw,
                    DateTimeFormatInfo& dtfi) {
        int monthDayOrder{};
        int yearMonthOrder{};
        int year{};

        if ((result.flags & PF::HaveDate) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        if (!GetMonthDayOrder(dtfi.MonthDayPattern, monthDayOrder)) {
            result.failure = true;
            return false;
        }
        if (monthDayOrder == ORDER_MD) {
            if (!GetYearMonthOrder(dtfi.YearMonthPattern, yearMonthOrder)) {
                result.failure = true;
                return false;
            }
            if (yearMonthOrder == ORDER_YM) {
                if (!TryAdjustYear(result, raw.GetNumber(0), year) ||
                    !SetDateYMD(result, year, raw.month, 1)) {
                    result.SetBadDateTimeFailure();
                    return false;
                }
                return true;
            }
        }

        GetDefaultYear(result, styles);
        if (!SetDateYMD(result, result.Year, raw.month, raw.GetNumber(0))) {
            result.SetBadDateTimeFailure();
            return false;
        }
        return true;
    }
    bool GetDayOfMNN(DateTimeResult& result, DateTimeRawInfo& raw, DateTimeFormatInfo& dtfi) {
        int order{};

        if ((result.flags & PF::HaveDate) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        int n1 = raw.GetNumber(0);
        int n2 = raw.GetNumber(1);

        if (!GetYearMonthDayOrder(dtfi.ShortDatePattern, order)) {
            result.failure = true;
            return false;
        }
        int year;

        if (order == ORDER_MDY) {
            if (TryAdjustYear(result, n2, year) &&
                result.calendar->IsValidDay(year, raw.month, n1, result.era)) {
                result.SetDate(year, raw.month, n1);
                result.flags |= PF::HaveDate;
                return true;
            } else if (TryAdjustYear(result, n1, year) &&
                       result.calendar->IsValidDay(year, raw.month, n2, result.era)) {
                result.SetDate(year, raw.month, n2);
                result.flags |= PF::HaveDate;
                return true;
            }
        } else if (order == ORDER_YMD) {
            if (TryAdjustYear(result, n1, year) &&
                result.calendar->IsValidDay(year, raw.month, n2, result.era)) {
                result.SetDate(year, raw.month, n2);
                result.flags |= PF::HaveDate;
                return true;
            } else if (TryAdjustYear(result, n2, year) &&
                       result.calendar->IsValidDay(year, raw.month, n1, result.era)) {
                result.SetDate(year, raw.month, n1);
                result.flags |= PF::HaveDate;
                return true;
            }
        } else if (order == ORDER_DMY) {
            if (TryAdjustYear(result, n2, year) &&
                result.calendar->IsValidDay(year, raw.month, n1, result.era)) {
                result.SetDate(year, raw.month, n1);
                result.flags |= PF::HaveDate;
                return true;
            } else if (TryAdjustYear(result, n1, year) &&
                       result.calendar->IsValidDay(year, raw.month, n2, result.era)) {
                result.SetDate(year, raw.month, n2);
                result.flags |= PF::HaveDate;
                return true;
            }
        }

        result.SetBadDateTimeFailure();
        return false;
    }
    bool GetDayOfYNN(DateTimeResult& result, DateTimeRawInfo& raw, DateTimeFormatInfo& dtfi) {
        int order{};

        if ((result.flags & PF::HaveDate) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        int n1 = raw.GetNumber(0);
        int n2 = raw.GetNumber(1);
        Text pattern = dtfi.ShortDatePattern;

        if (GetYearMonthDayOrder(pattern, order) && order == ORDER_YDM) {
            if (SetDateYMD(result, raw.year, n2, n1)) {
                result.flags |= PF::HaveDate;
                return true;
            }
        } else {
            if (SetDateYMD(result, raw.year, n1, n2)) {
                result.flags |= PF::HaveDate;
                return true;
            }
        }
        result.SetBadDateTimeFailure();
        return false;
    }
    bool GetDayOfNNY(DateTimeResult& result, DateTimeRawInfo& raw, DateTimeFormatInfo& dtfi) {
        int order{};

        if ((result.flags & PF::HaveDate) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        int n1 = raw.GetNumber(0);
        int n2 = raw.GetNumber(1);

        if (!GetYearMonthDayOrder(dtfi.ShortDatePattern, order)) {
            result.failure = true;
            return false;
        }

        if (order == ORDER_MDY || order == ORDER_YMD) {
            if (SetDateYMD(result, raw.year, n1, n2)) {
                result.flags |= PF::HaveDate;
                return true;
            }
#if TARGET_BROWSER

            if (GlobalizationMode.Hybrid && SetDateYDM(result, raw.year, n1, n2)) {
                result.flags |= PF::HaveDate;
                return true;
            }
#endif
        } else {
            if (SetDateYDM(result, raw.year, n1, n2)) {
                result.flags |= PF::HaveDate;
                return true;
            }
#if TARGET_BROWSER
            if (GlobalizationMode.Hybrid && SetDateYMD(result, raw.year, n1, n2)) {
                result.flags |= PF::HaveDate;
                return true;
            }
#endif
        }
        result.SetBadDateTimeFailure();
        return false;
    }
    bool GetDayOfYMN(DateTimeResult& result, DateTimeRawInfo& raw) {

        if ((result.flags & PF::HaveDate) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        if (SetDateYMD(result, raw.year, raw.month, raw.GetNumber(0))) {
            result.flags |= PF::HaveDate;
            return true;
        }
        result.SetBadDateTimeFailure();
        return false;
    }
    bool GetDayOfYN(DateTimeResult& result, DateTimeRawInfo& raw) {

        if ((result.flags & PF::HaveDate) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        if (SetDateYMD(result, raw.year, raw.GetNumber(0), 1)) {
            result.flags |= PF::HaveDate;
            return true;
        }
        result.SetBadDateTimeFailure();
        return false;
    }
    bool GetDayOfYM(DateTimeResult& result, DateTimeRawInfo& raw) {

        if ((result.flags & PF::HaveDate) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        if (SetDateYMD(result, raw.year, raw.month, 1)) {
            result.flags |= PF::HaveDate;
            return true;
        }
        result.SetBadDateTimeFailure();
        return false;
    }
    void AdjustTimeMark(DateTimeFormatInfo& dtfi, DateTimeRawInfo& raw) {

        if (raw.timeMark == TM::NotSet) {
            if (true) // invariant designators are non-null
            {
                if (dtfi.AMDesignator.Length == 0 && dtfi.PMDesignator.Length != 0) {
                    raw.timeMark = TM::AM;
                }
                if (dtfi.PMDesignator.Length == 0 && dtfi.AMDesignator.Length != 0) {
                    raw.timeMark = TM::PM;
                }
            }
        }
    }
    bool AdjustHour(int& hour, TM timeMark) {

        if (timeMark != TM::NotSet) {
            if (timeMark == TM::AM) {
                if (hour < 0 || hour > 12) {
                    return false;
                }
                hour = (hour == 12) ? 0 : hour;
            } else {
                if (hour < 0 || hour > 23) {
                    return false;
                }
                if (hour < 12) {
                    hour += 12;
                }
            }
        }
        return true;
    }
    bool GetTimeOfN(DateTimeResult& result, DateTimeRawInfo& raw) {

        if ((result.flags & PF::HaveTime) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        if (raw.timeMark == TM::NotSet) {
            result.SetBadDateTimeFailure();
            return false;
        }
        result.Hour = raw.GetNumber(0);
        result.flags |= PF::HaveTime;
        return true;
    }
    bool GetTimeOfNN(DateTimeResult& result, DateTimeRawInfo& raw) {

        if ((result.flags & PF::HaveTime) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        result.Hour = raw.GetNumber(0);
        result.Minute = raw.GetNumber(1);
        result.flags |= PF::HaveTime;
        return true;
    }
    bool GetTimeOfNNN(DateTimeResult& result, DateTimeRawInfo& raw) {

        if ((result.flags & PF::HaveTime) != 0) {

            result.SetBadDateTimeFailure();
            return false;
        }

        result.Hour = raw.GetNumber(0);
        result.Minute = raw.GetNumber(1);
        result.Second = raw.GetNumber(2);
        result.flags |= PF::HaveTime;
        return true;
    }
    bool GetDateOfDSN(DateTimeResult& result, DateTimeRawInfo& raw) {

        if (raw.numCount != 1 || result.Day != -1) {
            result.SetBadDateTimeFailure();
            return false;
        }
        result.Day = raw.GetNumber(0);
        return true;
    }
    bool GetDateOfNDS(DateTimeResult& result, DateTimeRawInfo& raw) {

        if (result.Month == -1) {

            result.SetBadDateTimeFailure();
            return false;
        }
        if (result.Year != -1) {

            result.SetBadDateTimeFailure();
            return false;
        }
        if (!TryAdjustYear(result, raw.GetNumber(0), result.Year)) {

            result.SetBadDateTimeFailure();
            return false;
        }
        result.Day = 1;
        return true;
    }
    bool GetDateOfNNDS(DateTimeResult& result, DateTimeRawInfo& raw, DateTimeFormatInfo& dtfi) {
        int order{};

        if ((result.flags & PF::HaveYear) != 0) {
            if (((result.flags & PF::HaveMonth) == 0) && ((result.flags & PF::HaveDay) == 0)) {
                if (TryAdjustYear(result, raw.year, result.Year) &&
                    SetDateYMD(result, result.Year, raw.GetNumber(0), raw.GetNumber(1))) {
                    return true;
                }
            }
        } else if ((result.flags & PF::HaveMonth) != 0) {
            if (((result.flags & PF::HaveYear) == 0) && ((result.flags & PF::HaveDay) == 0)) {
                if (!GetYearMonthDayOrder(dtfi.ShortDatePattern, order)) {
                    result.failure = true;
                    return false;
                }
                int year;
                if (order == ORDER_YMD) {
                    if (TryAdjustYear(result, raw.GetNumber(0), year) &&
                        SetDateYMD(result, year, result.Month, raw.GetNumber(1))) {
                        return true;
                    }
                } else {
                    if (TryAdjustYear(result, raw.GetNumber(1), year) &&
                        SetDateYMD(result, year, result.Month, raw.GetNumber(0))) {
                        return true;
                    }
                }
            }
        }
        result.SetBadDateTimeFailure();
        return false;
    }
    bool ProcessDateTimeSuffix(DateTimeResult& result, DateTimeRawInfo& raw, DateTimeToken& dtok) {

        switch (dtok.suffix) {
        case TK::SEP_YearSuff:
            if ((result.flags & PF::HaveYear) != 0) {
                return false;
            }
            result.flags |= PF::HaveYear;
            result.Year = raw.year = dtok.num;
            break;
        case TK::SEP_MonthSuff:
            if ((result.flags & PF::HaveMonth) != 0) {
                return false;
            }
            result.flags |= PF::HaveMonth;
            result.Month = raw.month = dtok.num;
            break;
        case TK::SEP_DaySuff:
            if ((result.flags & PF::HaveDay) != 0) {
                return false;
            }
            result.flags |= PF::HaveDay;
            result.Day = dtok.num;
            break;
        case TK::SEP_HourSuff:
            if ((result.flags & PF::HaveHour) != 0) {
                return false;
            }
            result.flags |= PF::HaveHour;
            result.Hour = dtok.num;
            break;
        case TK::SEP_MinuteSuff:
            if ((result.flags & PF::HaveMinute) != 0) {
                return false;
            }
            result.flags |= PF::HaveMinute;
            result.Minute = dtok.num;
            break;
        case TK::SEP_SecondSuff:
            if ((result.flags & PF::HaveSecond) != 0) {
                return false;
            }
            result.flags |= PF::HaveSecond;
            result.Second = dtok.num;
            break;
        }
        return true;
    }
    bool ProcessTerminalState(DS dps, DateTimeResult& result, DateTimeStyles& styles, DateTimeRawInfo& raw,
                              DateTimeFormatInfo& dtfi) {

        bool passed = true;
        switch (dps) {
        case DS::DX_NN:
            passed = GetDayOfNN(result, styles, raw, dtfi);
            break;
        case DS::DX_NNN:
            passed = GetDayOfNNN(result, raw, dtfi);
            break;
        case DS::DX_MN:
            passed = GetDayOfMN(result, styles, raw, dtfi);
            break;
        case DS::DX_NM:
            passed = GetDayOfNM(result, styles, raw, dtfi);
            break;
        case DS::DX_MNN:
            passed = GetDayOfMNN(result, raw, dtfi);
            break;
        case DS::DX_DS:

            passed = true;
            break;
        case DS::DX_YNN:
            passed = GetDayOfYNN(result, raw, dtfi);
            break;
        case DS::DX_NNY:
            passed = GetDayOfNNY(result, raw, dtfi);
            break;
        case DS::DX_YMN:
            passed = GetDayOfYMN(result, raw);
            break;
        case DS::DX_YN:
            passed = GetDayOfYN(result, raw);
            break;
        case DS::DX_YM:
            passed = GetDayOfYM(result, raw);
            break;
        case DS::TX_N:
            passed = GetTimeOfN(result, raw);
            break;
        case DS::TX_NN:
            passed = GetTimeOfNN(result, raw);
            break;
        case DS::TX_NNN:
            passed = GetTimeOfNNN(result, raw);
            break;
        case DS::TX_TS:

            passed = true;
            break;
        case DS::DX_DSN:
            passed = GetDateOfDSN(result, raw);
            break;
        case DS::DX_NDS:
            passed = GetDateOfNDS(result, raw);
            break;
        case DS::DX_NNDS:
            passed = GetDateOfNNDS(result, raw, dtfi);
            break;
        }

        if (!passed) {
            return false;
        }

        if (dps > DS::ERROR) {

            raw.numCount = 0;
        }
        return true;
    }
    bool ParseISO8601(DateTimeRawInfo& raw, DTString& str, DateTimeStyles styles, DateTimeResult& result) {
        int hour{};
        int minute{};
        DateTime time{};

        str.Index--;
        int second = 0;
        double partSecond = 0;

        str.SkipWhiteSpaces();
        if (!ParseDigits(str, 2, hour)) {
            result.SetBadDateTimeFailure();
            return false;
        }
        str.SkipWhiteSpaces();
        if (!str.Match(TimeDelimiter)) {
            result.SetBadDateTimeFailure();
            return false;
        }
        str.SkipWhiteSpaces();
        if (!ParseDigits(str, 2, minute)) {
            result.SetBadDateTimeFailure();
            return false;
        }
        str.SkipWhiteSpaces();
        if (str.Match(TimeDelimiter)) {
            str.SkipWhiteSpaces();
            if (!ParseDigits(str, 2, second)) {
                result.SetBadDateTimeFailure();
                return false;
            }
            if ((str.Match(TimeFractionDelimiterDot)) || (str.Match(TimeFractionDelimiterComma))) {
                if (!ParseFraction(str, partSecond)) {
                    result.SetBadDateTimeFailure();
                    return false;
                }
                str.Index--;
            }
            str.SkipWhiteSpaces();
        }
        if (str.GetNext()) {
            char16_t ch = str.GetChar();
            if (ch == u'+' || ch == u'-') {
                result.flags |= PF::TimeZoneUsed;
                if (!ParseTimeZone(str, result.timeZoneOffset)) {
                    result.SetBadDateTimeFailure();
                    return false;
                }
                str.Index--;
            } else if (ch == u'Z' || ch == u'z') {
                result.flags |= PF::TimeZoneUsed;
                result.timeZoneOffset = TimeSpan();
                result.flags |= PF::TimeZoneUtc;
            } else {
                str.Index--;
            }
            str.SkipWhiteSpaces();
            if (str.Match(u'#')) {
                if (!VerifyValidPunctuation(str)) {
                    result.SetBadDateTimeFailure();
                    return false;
                }
                str.SkipWhiteSpaces();
            }
            if (str.Match(u'\0')) {
                if (!VerifyValidPunctuation(str)) {
                    result.SetBadDateTimeFailure();
                    return false;
                }
            }
            if (str.GetNext()) {

                result.SetBadDateTimeFailure();
                return false;
            }
        }

        Gregorian* calendar = &cal;
        if (!calendar->TryToDateTime(raw.year, raw.GetNumber(0), raw.GetNumber(1), hour, minute, second, 0,
                                     result.era, time)) {
            result.failure = true;
            return false;
        }

        if (!time.TryAddTicks((std::int64_t)round_even(partSecond * second_ticks), time)) {
            result.SetBadDateTimeFailure();
            return false;
        }

        result.parsedDate = time;
        return DetermineTimeZoneAdjustments(result, styles, false);
    }
    bool TryParse(Text s, DateTimeFormatInfo& dtfi, DateTimeStyles styles, DateTimeResult& result) {
        DateTime time{};

        if (s.Length == 0) {
            result.failure = true;
            return false;
        }

        DS dps = DS::BEGIN;
        bool reachTerminalState = false;

        DateTimeToken dtok;
        dtok.suffix = TK::SEP_Unk;
        DateTimeRawInfo raw;

        raw.hasSameDateAndTimeSeparators = dtfi.DateSeparator.Equals(dtfi.TimeSeparator);

        result.calendar = dtfi.Calendar;
        result.era = 0;

        DTString str = DTString(s, dtfi);

        str.GetNext();

        do {

            if (!Lex(dps, str, dtok, raw, result, dtfi, styles)) {

                return false;
            }

            if (dtok.dtt != DTT::Unk) {

                if (dtok.suffix != TK::SEP_Unk) {
                    if (!ProcessDateTimeSuffix(result, raw, dtok)) {
                        result.SetBadDateTimeFailure();

                        return false;
                    }

                    dtok.suffix = TK::SEP_Unk;
                }

                if (dtok.dtt == DTT::NumLocalTimeMark) {
                    if (dps == DS::D_YNd || dps == DS::D_YN) {

                        return ParseISO8601(raw, str, styles, result);
                    } else {
                        result.SetBadDateTimeFailure();

                        return false;
                    }
                }

                if (raw.hasSameDateAndTimeSeparators) {
                    if (dtok.dtt == DTT::YearEnd || dtok.dtt == DTT::YearSpace ||
                        dtok.dtt == DTT::YearDateSep) {

                        if (dps == DS::T_Nt) {
                            dps = DS::D_Nd;
                        }
                        if (dps == DS::T_NNt) {
                            dps = DS::D_NNd;
                        }
                    }

                    bool atEnd = str.AtEnd();
                    if (s_dateParsingStates[(int)dps][(int)dtok.dtt] == DS::ERROR || atEnd) {
                        switch (dtok.dtt) {

                        case DTT::YearDateSep:
                            dtok.dtt = atEnd ? DTT::YearEnd : DTT::YearSpace;
                            break;
                        case DTT::NumDatesep:
                            dtok.dtt = atEnd ? DTT::NumEnd : DTT::NumSpace;
                            break;
                        case DTT::NumTimesep:
                            dtok.dtt = atEnd ? DTT::NumEnd : DTT::NumSpace;
                            break;
                        case DTT::MonthDatesep:
                            dtok.dtt = atEnd ? DTT::MonthEnd : DTT::MonthSpace;
                            break;
                        }
                    }
                }

                dps = s_dateParsingStates[(int)dps][(int)dtok.dtt];

                if (dps == DS::ERROR) {
                    result.SetBadDateTimeFailure();

                    return false;
                } else if (dps > DS::ERROR) {
                    if ((dtfi.FormatFlags & DF::UseHebrewRule) != 0) {
                        if (!ProcessHebrewTerminalState(dps, result, styles, raw, dtfi)) {

                            return false;
                        }
                    } else {
                        if (!ProcessTerminalState(dps, result, styles, raw, dtfi)) {

                            return false;
                        }
                    }
                    reachTerminalState = true;

                    dps = DS::BEGIN;
                }
            }
        } while (dtok.dtt != DTT::End && dtok.dtt != DTT::NumEnd && dtok.dtt != DTT::MonthEnd);

        if (!reachTerminalState) {
            result.SetBadDateTimeFailure();

            return false;
        }

        AdjustTimeMark(dtfi, raw);
        if (!AdjustHour(result.Hour, raw.timeMark)) {
            result.SetBadDateTimeFailure();

            return false;
        }

        bool bTimeOnly = (result.Year == -1 && result.Month == -1 && result.Day == -1);

        if (!CheckDefaultDateTime(result, result.calendar, styles)) {

            return false;
        }

        if (!result.calendar->TryToDateTime(result.Year, result.Month, result.Day, result.Hour, result.Minute,
                                            result.Second, 0, result.era, time)) {
            result.failure = true;

            return false;
        }

        if (raw.fraction > 0) {
            if (!time.TryAddTicks((std::int64_t)round_even(raw.fraction * second_ticks), time)) {
                result.SetBadDateTimeFailure();

                return false;
            }
        }

        if (raw.dayOfWeek != -1) {

            if (raw.dayOfWeek != (int)result.calendar->GetDayOfWeek(time)) {
                result.failure = true;

                return false;
            }
        }

        result.parsedDate = time;

        if (!DetermineTimeZoneAdjustments(result, styles, bTimeOnly)) {

            return false;
        }

        return true;
    }

    DateTime GetDateTimeNow(DateTimeResult&, DateTimeStyles&) {
        return DateTime(ctx.now());
    }
    bool CheckDefaultDateTime(DateTimeResult& result, Gregorian*& calendar, DateTimeStyles) {
        if (result.Year == -1 || result.Month == -1 || result.Day == -1) {
            auto now = DateTime(ctx.now());
            if (result.Month == -1 && result.Day == -1) {
                if (result.Year == -1) {
                    result.Year = calendar->GetYear(now);
                    result.Month = calendar->GetMonth(now);
                    result.Day = calendar->GetDayOfMonth(now);
                } else {
                    result.Month = 1;
                    result.Day = 1;
                }
            } else {
                if (result.Year == -1)
                    result.Year = calendar->GetYear(now);
                if (result.Month == -1)
                    result.Month = 1;
                if (result.Day == -1)
                    result.Day = 1;
            }
        }
        if (result.Hour == -1)
            result.Hour = 0;
        if (result.Minute == -1)
            result.Minute = 0;
        if (result.Second == -1)
            result.Second = 0;
        if (result.era == -1)
            result.era = 0;
        return true;
    }
    bool DetermineTimeZoneAdjustments(DateTimeResult& result, DateTimeStyles styles, bool time_only) {
        if (result.timeZoneOffset.Ticks < -504000000000ll || result.timeZoneOffset.Ticks > 504000000000ll) {
            result.failure = true;
            return false;
        }
        if ((result.flags & PF::TimeZoneUsed) == 0)
            return true;
        if ((styles & ST::AdjustToUniversal) != 0) {
            auto ticks = result.parsedDate.Ticks - result.timeZoneOffset.Ticks;
            if (ticks < 0)
                ticks += day_ticks;
            if (ticks < 0 || ticks > maximum_ticks) {
                result.failure = true;
                return false;
            }
            result.parsedDate = DateTime(ticks, DK::Utc);
            return true;
        }
        return AdjustTimeZoneToLocal(result, time_only);
    }
    bool AdjustTimeZoneToLocal(DateTimeResult& result, bool time_only) {
        auto ticks = result.parsedDate.Ticks;
        bool ambiguous = false;
        if (ticks < day_ticks) {
            ticks -= result.timeZoneOffset.Ticks;
            ticks += ctx.local_offset_no_throw(
                time_only ? ctx.now() : AssetDateTime(result.parsedDate.Ticks, result.parsedDate.Kind));
            if (ticks < 0)
                ticks += day_ticks;
        } else {
            ticks -= result.timeZoneOffset.Ticks;
            if (ticks < 0 || ticks > maximum_ticks)
                ticks +=
                    ctx.local_offset_no_throw(AssetDateTime(result.parsedDate.Ticks, result.parsedDate.Kind));
            else {
                auto offset = ctx.utc_offset(ticks);
                ticks += offset.ticks;
                ambiguous = offset.ambiguous_daylight;
            }
        }
        if (ticks < 0 || ticks > maximum_ticks) {
            result.parsedDate = {};
            result.failure = true;
            return false;
        }
        result.parsedDate = DateTime(ticks, DK::Local, ambiguous);
        return true;
    }
    bool ParseDigits(DTString& str, int min, int max, int& result) {
        int local = 0, start = str.Index, length = 0;
        while (length < max) {
            if (!str.GetNextDigit()) {
                --str.Index;
                break;
            }
            local = local * 10 + str.GetDigit();
            ++length;
        }
        result = local;
        if (length < min) {
            str.Index = start;
            return false;
        }
        return true;
    }
    bool ParseDigits(DTString& str, int digits, int& result) {
        return digits == 1 ? ParseDigits(str, 1, 2, result) : ParseDigits(str, digits, digits, result);
    }
    bool ProcessHebrewTerminalState(DS, DateTimeResult&, DateTimeStyles&, DateTimeRawInfo&,
                                    DateTimeFormatInfo&) {
        return false;
    } // unreachable: invariant Gregorian FormatFlags==0
  public:
    explicit Parser(const InvariantDateTimeParseContext& context)
        : ctx(context), cal{context}, format(context, &cal) {
    }
    bool parse(Text text, AssetDateTime& output, int styles = 0) {
        DateTimeResult result;
        bool valid = TryParse(text, format, styles, result);
        if (!valid)
            return false;
        auto bits =
            static_cast<std::uint64_t>(result.parsedDate.Ticks) |
            (static_cast<std::uint64_t>(result.parsedDate.ambiguous ? 3 : result.parsedDate.Kind) << 62);
        output = AssetDateTime::from_serialized(bits);
        return true;
    }
};
}
namespace invariant_datetime_test {
std::vector<std::tuple<std::u16string, int, int>> token_profile(bool mode) {
    InvariantDateTimeParseContext ctx;
    ctx.globalization_invariant_mode = mode;
    Gregorian calendar{ctx};
    DateTimeFormatInfo format(ctx, &calendar);
    std::vector<std::tuple<std::u16string, int, int>> out;
    for (const auto& token : format.table)
        out.emplace_back(token.text, token.type, token.value);
    return out;
}
}
bool try_parse_invariant_datetime(std::optional<std::u16string_view> text, AssetDateTime& output,
                                  const InvariantDateTimeParseContext& context) {
    output = {};
    if (!text || text->empty())
        return false;
    if (!context.now || !context.two_digit_year_max || !context.local_offset_no_throw ||
        !context.utc_offset || !context.valid_leap_second)
        throw std::invalid_argument("Incomplete invariant DateTime parser context");
    Parser parser(context);
    return parser.parse(Text(*text), output);
}
bool try_parse_invariant_datetime_cookie_expiry(std::optional<std::u16string_view> text,
                                                AssetDateTime& output,
                                                const InvariantDateTimeParseContext& context) {
    output = {};
    if (!text || text->empty())
        return false;
    if (!context.now || !context.two_digit_year_max || !context.local_offset_no_throw ||
        !context.utc_offset || !context.valid_leap_second)
        throw std::invalid_argument("Incomplete invariant DateTime parser context");
    Parser parser(context);
    return parser.parse(Text(*text), output, 23);
}
bool try_parse_invariant_datetime_utf8(const std::optional<std::string>& text, AssetDateTime& output,
                                       const InvariantDateTimeParseContext& context) {
    if (!text)
        return try_parse_invariant_datetime(std::nullopt, output, context);
    auto units = utf16_replacing(*text);
    return try_parse_invariant_datetime(std::u16string_view(units), output, context);
}
}
