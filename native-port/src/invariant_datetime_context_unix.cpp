// Copyright (c) Contributors, http://opensimulator.org/
// Consumed TZif/POSIX ingestion ported from dotnet/runtime v8.0.31
// TimeZoneInfo.Unix.cs / Unix.NonAndroid.cs and TimeSpanParse.cs.
// .NET Foundation, MIT license; see third_party/dotnet_collation/LICENSE.
#include "invariant_datetime_context_platform.hpp"
#include "invariant_datetime_context_digit_data.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/runtime_environment.hpp"
#include "opensim/framework/file_stream_policy.hpp"
#include "opensim/framework/full_path.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#ifndef _WIN32
#include <dirent.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace opensim::framework::invariant_datetime_platform {
namespace {
constexpr std::int64_t minute_ticks = 600000000ll, day_ticks = 864000000000ll,
                       maximum_ticks = 3155378975999999999ll, epoch_ticks = 621355968000000000ll;
struct InvalidZone : std::runtime_error {
    using std::runtime_error::runtime_error;
};
using Text = std::u16string_view;
std::int64_t span_add(std::int64_t a, std::int64_t b) {
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
        throw std::overflow_error("TimeSpan addition overflow");
    return a + b;
}
std::int64_t span_subtract(std::int64_t a, std::int64_t b) {
    if ((b > 0 && a < std::numeric_limits<std::int64_t>::min() + b) ||
        (b < 0 && a > std::numeric_limits<std::int64_t>::max() + b))
        throw std::overflow_error("TimeSpan subtraction overflow");
    return a - b;
}
bool ascii(char16_t c) {
    return c >= u'0' && c <= u'9';
}
bool digit(char16_t c) {
    for (auto r : decimal_digit_ranges) {
        if (c < r[0])
            return false;
        if (c <= r[1])
            return true;
    }
    return false;
}
bool integer(Text input, int& result) {
    auto white = [](char16_t c) { return c == u' ' || (c >= u'\t' && c <= u'\r'); };
    while (!input.empty() && white(input.front()))
        input.remove_prefix(1);
    bool negative = false;
    if (!input.empty() && (input.front() == u'-' || input.front() == u'+')) {
        negative = input.front() == u'-';
        input.remove_prefix(1);
    }
    std::uint64_t number = 0;
    bool digits = false, overflow = false;
    auto limit = negative ? 2147483648ull : 2147483647ull;
    while (!input.empty() && ascii(input.front())) {
        digits = true;
        auto d = static_cast<unsigned>(input.front() - u'0');
        if (number > (limit - d) / 10)
            overflow = true;
        else if (!overflow)
            number = number * 10 + d;
        input.remove_prefix(1);
    }
    while (!input.empty() && white(input.front()))
        input.remove_prefix(1);
    while (!input.empty() && input.front() == 0)
        input.remove_prefix(1);
    if (!digits || overflow || !input.empty())
        return false;
    result = negative ? static_cast<int>(-static_cast<std::int64_t>(number)) : static_cast<int>(number);
    return true;
}
// TimeSpan.TryParseExact("g", invariant) specialized only to the consumed
// POSIX scanner alphabet: decimal digits, colon, plus/minus. Source matching
// order is HM, HMS then DHM, DHMS. Fraction/whitespace cannot reach this helper.
std::optional<std::int64_t> general_span(Text value) {
    bool negative = false;
    if (!value.empty() && value.front() == u'-') {
        negative = true;
        value.remove_prefix(1);
    }
    std::vector<int> n;
    for (;;) {
        auto colon = value.find(u':');
        auto token = value.substr(0, colon);
        if (token.empty())
            return {};
        std::uint64_t v = 0;
        for (auto c : token) {
            if (!ascii(c) || v > (2147483647ull - static_cast<unsigned>(c - u'0')) / 10)
                return {};
            v = v * 10 + static_cast<unsigned>(c - u'0');
        }
        n.push_back(static_cast<int>(v));
        if (colon == Text::npos)
            break;
        value.remove_prefix(colon + 1);
    }
    auto convert = [&](int d, int h, int m, int s) -> std::optional<std::int64_t> {
        if (d > 10675199 || h > 23 || m > 59 || s > 59)
            return {};
        auto seconds = std::int64_t(d) * 86400 + std::int64_t(h) * 3600 + std::int64_t(m) * 60 + s;
        if (seconds > 922337203685ll)
            return {};
        auto t = seconds * 10000000ll;
        return negative ? -t : t;
    };
    if (n.size() == 2)
        return convert(0, n[0], n[1], 0);
    if (n.size() == 3) {
        auto hms = convert(0, n[0], n[1], n[2]);
        if (hms)
            return hms;
        return convert(n[0], n[1], n[2], 0);
    }
    if (n.size() == 4)
        return convert(n[0], n[1], n[2], n[3]);
    return {};
}
std::optional<std::int64_t> offset(Text value) {
    if (value.empty())
        return {};
    bool negative = value.front() == u'-';
    if (negative || value.front() == u'+')
        value.remove_prefix(1);
    std::optional<std::int64_t> result;
    int hours;
    if (integer(value, hours)) {
        auto seconds = std::int64_t(hours) * 3600;
        if (seconds < -922337203685ll || seconds > 922337203685ll)
            throw InvalidZone("TimeSpan hours out of range");
        result = seconds * 10000000ll;
    } else
        result = general_span(value);
    if (result && negative)
        *result = -*result;
    return result;
}
std::int64_t time_of_day(Text value) {
    auto t = offset(value);
    if (!t)
        return 72000000000ll;
    auto seconds = (*t / 10000000ll) % 86400;
    if (seconds < 0)
        seconds += 86400;
    return seconds * 10000000ll;
}
void validate_transition(const InvariantTimeZoneTransition& t) {
    if (t.time_of_day_ticks < 0 || t.time_of_day_ticks >= day_ticks || t.time_of_day_ticks % 10000 ||
        t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 || t.week < 1 || t.week > 5 ||
        t.day_of_week < 0 || t.day_of_week > 6)
        throw InvalidZone("Invalid POSIX transition");
}
std::optional<InvariantTimeZoneTransition> posix_transition(Text date, Text time) {
    if (date.empty())
        return {};
    InvariantTimeZoneTransition t;
    t.time_of_day_ticks = time_of_day(time);
    if (date.front() == u'M') {
        auto dot = date.find(u'.');
        if (dot == Text::npos || dot == 0)
            throw InvalidZone("Invalid M-date");
        auto tail = date.substr(dot + 1);
        auto second = tail.find(u'.');
        if (second == Text::npos || second == 0 || !integer(date.substr(1, dot - 1), t.month) ||
            !integer(tail.substr(0, second), t.week) || !integer(tail.substr(second + 1), t.day_of_week))
            throw InvalidZone("Invalid M-date");
        t.fixed_date = false;
        t.day = 1;
    } else if (date.front() != u'J') {
        int n;
        if (!integer(date, n) || n >= 59)
            return {};
        t.fixed_date = true;
        if (n <= 30) {
            t.month = 1;
            t.day = n + 1;
        } else {
            t.month = 2;
            t.day = n - 30;
        }
    } else {
        std::size_t i = 1;
        if (i >= date.size() || !ascii(date[i]))
            throw InvalidZone("Invalid Julian day");
        std::uint32_t bits = 0;
        do {
            bits = bits * 10 + static_cast<unsigned>(date[i] - u'0');
            ++i;
        } while (i < date.size() && ascii(date[i]));
        auto n = std::bit_cast<std::int32_t>(bits);
        static constexpr int before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
        if (n == 0 || n > 365)
            throw InvalidZone("Invalid Julian day");
        int m = 1;
        while (m < 13 && n > before[m])
            ++m;
        t.month = m;
        t.day = n - before[m - 1];
    }
    validate_transition(t);
    return t;
}
std::int64_t truncate_minutes(std::int64_t ticks) {
    return ticks % minute_ticks ? (ticks / minute_ticks % 1440) * minute_ticks : ticks;
}
bool has_dst(const InvariantTimeZoneRule& r) {
    return r.daylight_delta_ticks != 0 ||
           (r.daylight_start.month != 0 && r.daylight_start.time_of_day_ticks != 0) ||
           (r.daylight_end.month != 0 && r.daylight_end.time_of_day_ticks != 10000);
}
InvariantTimeZoneTransition empty_transition() {
    return {0, 0, 0, 0, 0, false};
}
InvariantTimeZoneRule rule(AssetDateTime start, AssetDateTime end, std::int64_t delta,
                           InvariantTimeZoneTransition begin, InvariantTimeZoneTransition finish,
                           std::int64_t base, bool no) {
    if (!no && begin.time_of_day_ticks == finish.time_of_day_ticks && begin.month == finish.month &&
        begin.fixed_date == finish.fixed_date &&
        (begin.fixed_date ? begin.day == finish.day
                          : begin.week == finish.week && begin.day_of_week == finish.day_of_week))
        throw InvalidZone("Identical adjustment transitions");
    if (delta > 720 * minute_ticks) {
        delta = span_subtract(delta, day_ticks);
        base = span_add(base, day_ticks);
    } else if (delta < -720 * minute_ticks) {
        delta = span_add(delta, day_ticks);
        base = span_subtract(base, day_ticks);
    }
    if (delta < -1380 * minute_ticks || delta > 840 * minute_ticks || delta % minute_ticks ||
        start.ticks() > end.ticks() || start.kind() == 2 || end.kind() == 2 ||
        (start.kind() == 0 && start.ticks() != 0 && start.ticks() % day_ticks) ||
        (end.kind() == 0 && end.ticks() != maximum_ticks && end.ticks() % day_ticks))
        throw InvalidZone("Invalid adjustment rule");
    return {start, end, delta, base, begin, finish, no};
}
void normalize(InvariantTimeZoneRule& r, std::int64_t base) {
    auto actual =
        span_add(span_add(base, r.base_offset_delta_ticks), has_dst(r) ? r.daylight_delta_ticks : 0);
    auto adjustment = actual > 840 * minute_ticks    ? span_subtract(840 * minute_ticks, actual)
                      : actual < -840 * minute_ticks ? span_subtract(-840 * minute_ticks, actual)
                                                     : 0;
    if (adjustment)
        r = rule(r.start, r.end, r.daylight_delta_ticks, r.daylight_start, r.daylight_end,
                 span_add(r.base_offset_delta_ticks, adjustment), r.no_daylight_transitions);
}
std::optional<InvariantTimeZoneRule> posix(Text format, AssetDateTime start, std::int64_t base) {
    std::size_t i = 0;
    auto scan = [&](auto stop) {
        auto begin = i;
        while (i < format.size() && !stop(format[i]))
            ++i;
        return format.substr(begin, i - begin);
    };
    auto name = [&]() {
        if (i < format.size() && format[i] == u'<') {
            ++i;
            auto n = scan([](auto c) { return c == u'>'; });
            if (i < format.size() && format[i] == u'>')
                ++i;
            return n;
        }
        return scan([](auto c) { return digit(c) || c == u'+' || c == u'-' || c == u','; });
    };
    auto off = [&]() {
        return scan([](auto c) { return !digit(c) && c != u'+' && c != u'-' && c != u':'; });
    };
    auto standard = name(), standard_offset = off(), daylight = name();
    Text daylight_offset, begin, begin_time, end, end_time;
    if (!daylight.empty()) {
        daylight_offset = off();
        auto date_time = [&](Text& d, Text& t) {
            d = scan([](auto c) { return c == u'/' || c == u','; });
            if (i < format.size() && format[i] == u'/') {
                ++i;
                t = scan([](auto c) { return c == u','; });
            }
        };
        if (i < format.size() && format[i] == u',') {
            ++i;
            date_time(begin, begin_time);
            if (i < format.size() && format[i] == u',') {
                ++i;
                date_time(end, end_time);
            }
        }
    }
    if (standard.empty() || standard_offset.empty())
        return {};
    auto standard_parsed = offset(standard_offset);
    if (!standard_parsed)
        return {};
    auto base_delta = truncate_minutes(span_subtract(-*standard_parsed, base));
    if (daylight.empty())
        return rule(start, AssetDateTime(maximum_ticks, 0), 0, empty_transition(), empty_transition(),
                    base_delta, true);
    auto daylight_parsed = offset(daylight_offset);
    auto daylight_delta = daylight_parsed
                              ? truncate_minutes(span_subtract(
                                    truncate_minutes(span_subtract(-*daylight_parsed, base)), base_delta))
                              : 60 * minute_ticks;
    auto a = posix_transition(begin, begin_time), b = posix_transition(end, end_time);
    if (!a || !b)
        return {};
    return rule(start, AssetDateTime(maximum_ticks, 0), daylight_delta, *a, *b, base_delta, false);
}
struct Type {
    std::int64_t offset;
    bool dst;
    std::uint8_t abbreviation;
};
struct Raw {
    std::vector<AssetDateTime> transitions;
    std::vector<std::uint8_t> indices;
    std::vector<Type> types;
    std::u16string abbreviations, future;
};
struct Reader {
    const std::vector<std::uint8_t>& data;
    std::uint32_t u32(std::size_t p) const {
        if (p > data.size() || data.size() - p < 4)
            throw InvalidZone("Truncated TZif integer");
        return (std::uint32_t(data[p]) << 24) | (std::uint32_t(data[p + 1]) << 16) |
               (std::uint32_t(data[p + 2]) << 8) | data[p + 3];
    }
    std::uint8_t byte(std::size_t p) const {
        if (p >= data.size())
            throw std::out_of_range("TZif array index");
        return data[p];
    }
    struct Header {
        int version;
        std::uint32_t gmt, std, leap, time, type, chars;
    };
    Header header(std::size_t p) {
        if (data.size() < 44 || u32(p) != 0x545a6966)
            throw InvalidZone("Invalid TZif header");
        auto v = byte(p + 4);
        return {v == '2'   ? 2
                : v == '3' ? 3
                           : 1,
                u32(p + 20),
                u32(p + 24),
                u32(p + 28),
                u32(p + 32),
                u32(p + 36),
                u32(p + 40)};
    }
};
std::int32_t add_index(std::int32_t index, std::uint64_t amount) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(index) +
                                       static_cast<std::uint32_t>(amount));
}
Raw parse(const std::vector<std::uint8_t>& bytes) {
    Reader r{bytes};
    auto h = r.header(0);
    std::int32_t i = 44;
    int width = 4;
    if (h.version != 1) {
        i = add_index(i, std::uint64_t(4) * h.time + h.time + std::uint64_t(6) * h.type +
                             std::uint64_t(8) * h.leap + h.std + h.gmt + h.chars);
        h = r.header(i);
        i = add_index(i, 44);
        width = 8;
    }
    if (h.time > 2147483647u || h.type > 2147483647u)
        throw std::overflow_error("TZif array length overflow");
    Raw out;
    for (std::uint32_t k = 0; k < h.time; ++k) {
        std::int64_t unix_time;
        if (h.version == 1)
            unix_time = std::bit_cast<std::int32_t>(r.u32(i));
        else
            unix_time = std::bit_cast<std::int64_t>((std::uint64_t(r.u32(i)) << 32) | r.u32(i + 4));
        i = add_index(i, width);
        if (unix_time < -62135596800ll)
            out.transitions.emplace_back(0, 0);
        else if (unix_time > 253402300799ll)
            out.transitions.emplace_back(maximum_ticks, 0);
        else
            out.transitions.emplace_back(epoch_ticks + unix_time * 10000000ll, 1);
    }
    for (std::uint32_t k = 0; k < h.time; ++k) {
        out.indices.push_back(r.byte(i));
        i = add_index(i, 1);
    }
    for (std::uint32_t k = 0; k < h.type; ++k) {
        if (add_index(i, 6) > static_cast<std::int64_t>(bytes.size()))
            throw InvalidZone("Truncated TZif type");
        out.types.push_back({std::int64_t(std::bit_cast<std::int32_t>(r.u32(i))) * 10000000ll,
                             r.byte(i + 4) != 0, r.byte(i + 5)});
        i = add_index(i, 6);
    }
    if (i < 0 || static_cast<std::size_t>(i) > bytes.size() || h.chars > 2147483647u ||
        h.chars > bytes.size() - static_cast<std::size_t>(i))
        throw InvalidZone("Truncated TZif abbreviations");
    out.abbreviations =
        utf16_replacing(std::string_view(reinterpret_cast<const char*>(bytes.data() + i), h.chars));
    i = add_index(i, h.chars);
    i = add_index(i, std::uint64_t(h.leap) * (width + 4));
    i = add_index(i, std::min(h.std, h.type));
    i = add_index(i, std::min(h.gmt, h.type));
    if (h.version != 1) {
        auto initial = r.byte(i);
        i = add_index(i, 1);
        if (initial == '\n' && r.byte(bytes.size() - 1) == '\n') {
            if (i < 0 || static_cast<std::size_t>(i) > bytes.size() - 1)
                throw InvalidZone("Invalid TZif footer");
            out.future = utf16_replacing(
                std::string_view(reinterpret_cast<const char*>(bytes.data() + i), bytes.size() - i - 1));
        }
    }
    return out;
}
const Type& type(const Raw& raw, std::size_t index) {
    if (index >= raw.types.size())
        throw std::out_of_range("TZif type array index");
    return raw.types[index];
}
void abbreviation(const Raw& raw, unsigned index) {
    if (index > raw.abbreviations.size())
        throw InvalidZone(
            "Invalid TZif abbreviation index"); /* Source substring only validates starting index; names are not exposed by context. */
}
bool utc_alias(const std::string& id) {
    for (auto s : {"Etc/UTC", "Etc/UCT", "Etc/Universal", "Etc/Zulu", "UCT", "UTC", "Universal", "Zulu"})
        if (ordinal_equals_ignore_case(id, s))
            return true;
    return false;
}
}
ZoneModel unix_tzif_zone(const std::vector<std::uint8_t>& bytes, const std::string& id, bool disabled,
                         std::function<std::int64_t()> clock) {
    if (id.empty())
        throw InvalidZone("Empty timezone identifier");
    if (utc_alias(id))
        return {};
    auto raw = parse(bytes);
    auto utc_now = clock();
    ZoneModel m;
    for (std::size_t i = 0; i < raw.transitions.size() && raw.transitions[i].ticks() <= utc_now; ++i) {
        auto& t = type(raw, raw.indices[i]);
        if (!t.dst)
            m.base_offset = t.offset;
        abbreviation(raw, t.abbreviation);
    }
    if (raw.transitions.empty())
        for (auto& t : raw.types) {
            if (!t.dst)
                m.base_offset = t.offset;
            abbreviation(raw, t.abbreviation);
        }
    m.base_offset = truncate_minutes(m.base_offset);
    if (!disabled && !raw.transitions.empty()) {
        std::size_t i = 0;
        while (i <= raw.transitions.size()) {
            while (i < raw.transitions.size() && raw.transitions[i].ticks() == 0)
                ++i;
            InvariantTimeZoneRule next;
            if (m.rules.empty() && i < raw.transitions.size()) {
                auto early = std::find_if(raw.types.begin(), raw.types.end(), [](auto t) { return !t.dst; });
                auto& t = early == raw.types.end() ? type(raw, 0) : *early;
                auto delta = truncate_minutes(t.offset - m.base_offset);
                next =
                    rule(AssetDateTime(0, 0),
                         AssetDateTime(raw.transitions[i].ticks() - 1, raw.transitions[i].kind()),
                         t.dst ? delta : 0, empty_transition(), empty_transition(), t.dst ? 0 : delta, true);
            } else if (i < raw.transitions.size()) {
                auto start = raw.transitions[i - 1];
                auto& t = type(raw, raw.indices[i - 1]);
                auto delta = truncate_minutes(t.offset - m.base_offset);
                auto marker = empty_transition();
                if (t.dst)
                    marker = {20000, 1, 1, 1, 0, true};
                next = rule(start, AssetDateTime(raw.transitions[i].ticks() - 1, raw.transitions[i].kind()),
                            t.dst ? delta : 0, marker, empty_transition(), t.dst ? 0 : delta, true);
            } else {
                if (i == 0)
                    throw InvalidZone("Missing TZif transitions");
                auto start = raw.transitions[i - 1];
                auto future = raw.future.empty() ? std::optional<InvariantTimeZoneRule>{}
                                                 : posix(raw.future, start, m.base_offset);
                if (future)
                    next = *future;
                else {
                    auto& t = type(raw, raw.indices[i - 1]);
                    auto delta = truncate_minutes(t.offset - m.base_offset);
                    next = rule(start, AssetDateTime(maximum_ticks, 0), t.dst ? delta : 0, empty_transition(),
                                empty_transition(), t.dst ? 0 : delta, true);
                }
            }
            normalize(next, m.base_offset);
            m.rules.push_back(next);
            ++i;
        }
    }
    if (m.base_offset < -840 * minute_ticks || m.base_offset > 840 * minute_ticks)
        throw InvalidZone("TZif base offset out of range");
    for (std::size_t i = 0; i < m.rules.size(); ++i) {
        auto& r = m.rules[i];
        auto combined = span_add(span_add(m.base_offset, r.base_offset_delta_ticks),
                                 has_dst(r) ? r.daylight_delta_ticks : 0);
        if (combined < -840 * minute_ticks || combined > 840 * minute_ticks ||
            (i && r.start.ticks() <= m.rules[i - 1].end.ticks()))
            throw InvalidZone("Invalid TZif adjustment rules");
    }
    return m;
}
ZoneModel unix_tzif_local_data(const std::vector<std::uint8_t>& bytes, const std::string& id,
                               std::function<std::int64_t()> clock) {
    try {
        return unix_tzif_zone(bytes, id, false, clock);
    } catch (const InvalidZone&) {
        try {
            return unix_tzif_zone(bytes, id, true, clock);
        } catch (const InvalidZone&) {
            return {};
        }
    }
}
ZoneModel unix_tzif_zone(const std::vector<std::uint8_t>& bytes, const std::string& id, bool disabled,
                         std::int64_t now) {
    return unix_tzif_zone(bytes, id, disabled, [now] { return now; });
}
#ifndef _WIN32
namespace {
bool io_path_error(FullPathFailure kind) {
    switch (kind) {
    case FullPathFailure::Io:
    case FullPathFailure::PathTooLong:
    case FullPathFailure::MissingFile:
    case FullPathFailure::MissingDirectory:
    case FullPathFailure::Unauthorized:
        return true;
    default:
        return false;
    }
}
struct File {
    int fd = -1;
    bool locked = false, random_access = true;
    File(const std::filesystem::path& path, std::optional<bool> disabled) {
        std::filesystem::path full;
        try {
            full = get_full_path(path.native());
        } catch (const FullPathError& e) {
            if (io_path_error(e.kind))
                return;
            throw;
        }
        do {
            fd = ::open(full.c_str(), O_RDONLY | O_CLOEXEC);
        } while (fd < 0 && errno == EINTR);
        if (fd < 0)
            return;
        struct stat st{};
        if (([&]() {
                decltype(::fstat(fd, &st)) result;
                do {
                    result = ::fstat(fd, &st);
                } while (result < 0 && errno == EINTR);
                return result;
            }()) < 0 ||
            S_ISDIR(st.st_mode)) {
            ::close(fd);
            fd = -1;
            return;
        }
        if (!file_stream_disable_locking(disabled)) {
            int result;
            do {
                result = ::flock(fd, LOCK_SH | LOCK_NB);
            } while (result < 0 && errno == EINTR);
            locked = result >= 0;
            if (result < 0 && errno == EWOULDBLOCK) {
                ::close(fd);
                fd = -1;
            }
        }
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() {
        if (fd >= 0) {
            if (locked)
                ::flock(fd, LOCK_UN);
            ::close(fd);
        }
    }
    ssize_t read(std::uint8_t* buffer, std::size_t count, std::int64_t offset) {
        ssize_t n;
        do {
            n = random_access ? ::pread(fd, buffer, count, static_cast<off_t>(offset))
                              : ::read(fd, buffer, count);
        } while (n < 0 && errno == EINTR);
        if (n < 0 && random_access && (errno == ENXIO || errno == ESPIPE)) {
            random_access = false;
            do {
                n = ::read(fd, buffer, count);
            } while (n < 0 && errno == EINTR);
        }
        return n;
    }
};
std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path& path,
                                                   std::optional<bool> disabled) {
    std::filesystem::path full;
    try {
        full = get_full_path(path.native());
    } catch (const FullPathError& e) {
        if (io_path_error(e.kind) || e.kind == FullPathFailure::Argument ||
            e.kind == FullPathFailure::ArgumentNull)
            return {};
        throw;
    }
    struct stat exists{};
    if (([&]() {
            decltype(::stat(full.c_str(), &exists)) result;
            do {
                result = ::stat(full.c_str(), &exists);
            } while (result < 0 && errno == EINTR);
            return result;
        }()) < 0 ||
        S_ISDIR(exists.st_mode))
        return {};
    File file(path, disabled);
    if (file.fd < 0)
        return {};
    file.random_access = ([&]() {
                             decltype(::lseek(file.fd, 0, SEEK_CUR)) result;
                             do {
                                 result = ::lseek(file.fd, 0, SEEK_CUR);
                             } while (result < 0 && errno == EINTR);
                             return result;
                         }()) >= 0;
    std::int64_t length = 0;
    if (file.random_access) {
        struct stat st{};
        if (([&]() {
                decltype(::fstat(file.fd, &st)) result;
                do {
                    result = ::fstat(file.fd, &st);
                } while (result < 0 && errno == EINTR);
                return result;
            }()) < 0 ||
            st.st_size > 2147483591ll)
            return {};
        length = st.st_size;
    }
    if (length > 0) {
        std::vector<std::uint8_t> out(static_cast<std::size_t>(length));
        std::size_t index = 0;
        while (index < out.size()) {
            auto n = file.read(out.data() + index, out.size() - index, index);
            if (n <= 0)
                return {};
            index += static_cast<std::size_t>(n);
        }
        return out;
    }
    std::vector<std::uint8_t> out(512);
    std::size_t count = 0;
    for (;;) {
        if (count == out.size()) {
            auto size = std::max<std::size_t>(2147483591ull, count + 1);
            out.resize(out.size() * 2 > 2147483591ull ? size : out.size() * 2);
        }
        auto n = file.read(out.data() + count, out.size() - count, count);
        if (n < 0)
            return {};
        if (n == 0) {
            out.resize(count);
            return out;
        }
        count += static_cast<std::size_t>(n);
    }
}
bool compare_file(const std::string& path, const std::vector<std::uint8_t>& data,
                  std::vector<std::uint8_t>& buffer, std::optional<bool> disabled) {
    File file(path, disabled);
    if (file.fd < 0)
        return false;
    file.random_access = ([&]() {
                             decltype(::lseek(file.fd, 0, SEEK_CUR)) result;
                             do {
                                 result = ::lseek(file.fd, 0, SEEK_CUR);
                             } while (result < 0 && errno == EINTR);
                             return result;
                         }()) >= 0;
    if (!file.random_access)
        throw std::logic_error("Timezone comparison file does not support seeking");
    struct stat st{};
    if (([&]() {
            decltype(::fstat(file.fd, &st)) result;
            do {
                result = ::fstat(file.fd, &st);
            } while (result < 0 && errno == EINTR);
            return result;
        }()) < 0 ||
        st.st_size != static_cast<std::int64_t>(data.size()))
        return false;
    std::size_t count = data.size();
    while (count) {
        auto n = file.read(buffer.data(), count, 0);
        if (n <= 0 || !std::equal(buffer.begin(), buffer.begin() + n, data.begin()))
            return false;
        count -= static_cast<std::size_t>(n);
    }
    return true;
}
}
std::string timezone_directory() {
    auto value = runtime_environment_get(std::make_shared<const std::u16string>(u"TZDIR"));
    auto out = value ? utf8(*value) : std::string("/usr/share/zoneinfo/");
    if (!out.ends_with('/'))
        out += '/';
    return out;
}
std::string find_id(const std::filesystem::path& path, const std::vector<std::uint8_t>& data,
                    const std::string& directory, std::optional<bool> disabled) {
    std::vector<std::uint8_t> buffer(data.size());
    std::error_code ec;
    auto link = std::filesystem::read_symlink(path, ec);
    if (!ec) {
        auto absolute = std::filesystem::absolute(link.is_absolute() ? link : path.parent_path() / link, ec)
                            .lexically_normal()
                            .string();
        if (!ec && absolute.starts_with(directory))
            return absolute.substr(directory.size());
    }
    std::vector<std::string> stack;
    std::string current = directory;
    for (;;) {
        DIR* handle = ([&]() {
            decltype(::opendir(current.c_str())) result;
            do {
                result = ::opendir(current.c_str());
            } while (!result && errno == EINTR);
            return result;
        }());
        if (!handle)
            return "Local";
        bool found = false;
        std::string id;
        while (auto entry = ::readdir(handle)) {
            std::string name = entry->d_name;
            if (name == "." || name == "..")
                continue;
            name = utf8(utf16_replacing(name));
            auto full = current + (current.ends_with('/') ? "" : "/") + name;
            bool is_directory = entry->d_type == DT_DIR;
            if (entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN) {
                struct stat info{};
                is_directory = ([&]() {
                                   decltype(::stat(full.c_str(), &info)) result;
                                   do {
                                       result = ::stat(full.c_str(), &info);
                                   } while (result < 0 && errno == EINTR);
                                   return result;
                               }()) >= 0 &&
                               S_ISDIR(info.st_mode);
            }
            if (is_directory) {
                stack.push_back(full);
                continue;
            }
            if (ordinal_equals_ignore_case(full, directory + "localtime") ||
                ordinal_equals_ignore_case(full, directory + "posixrules"))
                continue;
            if (compare_file(full, data, buffer, disabled)) {
                id = full.starts_with(directory) ? full.substr(directory.size()) : full;
                found = true;
                break;
            }
        }
        ::closedir(handle);
        if (found)
            return id;
        if (stack.empty())
            return "Local";
        current = std::move(stack.back());
        stack.pop_back();
    }
}
ZoneModel unix_local_zone(std::optional<bool> disabled) {
    auto tz = runtime_environment_get(std::make_shared<const std::u16string>(u"TZ"));
    auto directory = timezone_directory();
    std::optional<std::vector<std::uint8_t>> data;
    std::optional<std::string> id;
    std::filesystem::path loaded_path;
    if (!tz) {
        loaded_path = "/etc/localtime";
        data = read_file(loaded_path, disabled);
        if (!data) {
            loaded_path = std::filesystem::path(directory) / "localtime";
            data = read_file(loaded_path, disabled);
        }
    } else {
        auto name = utf8(*tz);
        if (!name.empty() && name.front() == ':')
            name.erase(0, 1);
        if (name.empty())
            return {};
        auto path = std::filesystem::path(name);
        if (!path.is_absolute())
            id = name;
        loaded_path = path.is_absolute() ? path : std::filesystem::path(directory) / path;
        data = read_file(loaded_path, disabled);
    }
    if (!data)
        return {};
    if (!id)
        id = find_id(loaded_path, *data, directory, disabled);
    auto clock = []() { return native_utc_now().ticks(); };
    return unix_tzif_local_data(*data, *id, clock);
}
#endif
}
