// Copyright (c) Contributors, http://opensimulator.org/
// Source-derived .NET Runtime v8.0.31 native provider ownership/cache binding.
// .NET Foundation, MIT license; see third_party/dotnet_collation/LICENSE.
#include "invariant_datetime_context_platform.hpp"
#include <algorithm>
#include "opensim/framework/file_system_name.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <atomic>
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/text.hpp"
#include <chrono>
#include <mutex>
#include <optional>
#include <stdexcept>
namespace opensim::framework {
struct NativeInvariantDateTimeContext::State {
    const bool invariant, use_nls;
    const std::optional<bool> disable_file_locking;
    const std::function<std::optional<std::u16string>(std::u16string_view)> id_converter;
    struct Epoch {
        std::recursive_mutex mutex;
        std::shared_ptr<InvariantTimeZoneRules> local, current_year;
        int year = -1;
        std::shared_ptr<TimeZone> legacy_local;
        std::vector<std::pair<std::u16string, std::shared_ptr<TimeZone>>> named;
        std::function<bool(std::u16string_view, std::u16string_view)> named_equal;
    };
    std::atomic<std::shared_ptr<Epoch>> cache{std::make_shared<Epoch>()};
    std::mutex calendar_mutex;
    std::optional<int> cutoff;
    explicit State(NativeInvariantDateTimeContextOptions o)
        : invariant(o.globalization_invariant_mode), use_nls(o.use_nls),
          disable_file_locking(o.disable_file_locking), id_converter(std::move(o.iana_to_windows)) {
    }
    std::shared_ptr<InvariantTimeZoneRules> local_zone(std::shared_ptr<Epoch> epoch) {
        std::lock_guard lock(epoch->mutex);
        if (!epoch->local) {
#ifdef _WIN32
            auto m = invariant_datetime_platform::windows_local_zone(invariant, use_nls, id_converter);
#else
            auto m = invariant_datetime_platform::unix_local_zone(disable_file_locking);
#endif
            epoch->local = std::make_shared<InvariantTimeZoneRules>(m.base_offset, std::move(m.rules));
        }
        return epoch->local;
    }
    std::shared_ptr<InvariantTimeZoneRules> local_zone() {
        return local_zone(cache.load());
    }
    int calendar_cutoff() {
        std::lock_guard lock(calendar_mutex);
        if (!cutoff)
            cutoff = invariant_datetime_platform::native_two_digit_year_max(invariant);
        return *cutoff;
    }
    AssetDateTime now() {
        auto utc = invariant_datetime_platform::native_utc_now();
        auto epoch = cache.load();
        InvariantDateTimeParseContext::UtcOffset offset;
#ifdef _WIN32
        using namespace std::chrono;
        int utc_year =
            int(year_month_day{sys_days{std::chrono::year(1) / 1 / 1} + days(utc.ticks() / 864000000000ll)}
                    .year());
        std::shared_ptr<InvariantTimeZoneRules> current;
        {
            std::lock_guard lock(epoch->mutex);
            if (!epoch->current_year || epoch->year != utc_year) {
                auto m = invariant_datetime_platform::windows_current_year_zone();
                epoch->current_year =
                    std::make_shared<InvariantTimeZoneRules>(m.base_offset, std::move(m.rules));
                epoch->year = utc_year;
            }
            current = epoch->current_year;
        }
        offset = current->current_year_offset(
            utc.ticks(),
            [this, epoch]() -> std::shared_ptr<const InvariantTimeZoneRules> { return local_zone(epoch); });
#else
        offset = local_zone(epoch)->utc_offset(utc.ticks());
#endif
        auto ticks = utc.ticks() + offset.ticks;
        if (ticks < 0 || ticks > 3155378975999999999ll)
            return AssetDateTime(std::clamp<std::int64_t>(ticks, 0, 3155378975999999999ll), 2);
        return AssetDateTime::from_serialized(
            static_cast<std::uint64_t>(ticks) |
            (offset.ambiguous_daylight ? 0xc000000000000000ull : 0x8000000000000000ull));
    }
};
NativeInvariantDateTimeContext::NativeInvariantDateTimeContext(bool mode)
    : NativeInvariantDateTimeContext(NativeInvariantDateTimeContextOptions{mode, false, {}}) {
}
NativeInvariantDateTimeContext::NativeInvariantDateTimeContext(NativeInvariantDateTimeContextOptions options)
    : state_(std::make_shared<State>(std::move(options))) {
}
InvariantDateTimeParseContext NativeInvariantDateTimeContext::callbacks() const {
    auto s = state_;
    InvariantDateTimeParseContext c;
    c.globalization_invariant_mode = s->invariant;
    c.now = [s] { return s->now(); };
    c.two_digit_year_max = [s] { return s->calendar_cutoff(); };
    c.local_offset_no_throw = [s](AssetDateTime d) { return s->local_zone()->local_offset_no_throw(d); };
    c.utc_offset = [s](std::int64_t ticks) { return s->local_zone()->utc_offset(ticks); };
    c.valid_leap_second = [](int y, int m, int d, int h, int minute) {
        return invariant_datetime_platform::native_valid_leap_second(y, m, d, h, minute);
    };
    return c;
}
AssetDateTime NativeInvariantDateTimeContext::now() const {
    auto state = state_;
    return state->now();
}
AssetDateTime NativeInvariantDateTimeContext::utc_now() const {
    return invariant_datetime_platform::native_utc_now();
}
AssetDateTime NativeInvariantDateTimeContext::to_local_time(AssetDateTime value) const {
    if (value.kind() == 2)
        return value;
    auto state = state_;
    auto offset = state->local_zone()->utc_offset(value.ticks());
    auto ticks = value.ticks() + offset.ticks;
    if (ticks < 0 || ticks > 3155378975999999999ll)
        return AssetDateTime(std::clamp<std::int64_t>(ticks, 0, 3155378975999999999ll), 2);
    return AssetDateTime::from_serialized(
        static_cast<std::uint64_t>(ticks) |
        (offset.ambiguous_daylight ? 0xc000000000000000ull : 0x8000000000000000ull));
}
AssetDateTime NativeInvariantDateTimeContext::from_file_time_utc(std::int64_t file) const {
    if (file < 0 || file > 2650467743999999999ll)
        throw NativeDateTimeError("Not a valid Win32 FileTime. (Parameter 'fileTime')", "fileTime");
    return invariant_datetime_platform::native_from_file_time_utc(file);
}
std::int64_t NativeInvariantDateTimeContext::to_file_time_utc(AssetDateTime value) const {
    auto ticks = value.ticks();
    if (value.kind() == 2) {
        auto state = state_;
        auto offset = state->local_zone()->local_offset_no_throw(value);
        ticks = std::clamp<std::int64_t>(ticks - offset, 0, 3155378975999999999ll);
    }
    return invariant_datetime_platform::native_to_file_time_utc(ticks);
}
std::shared_ptr<NativeInvariantDateTimeContext> shared_native_invariant_datetime_context() {
    static auto owner = std::make_shared<NativeInvariantDateTimeContext>();
    return owner;
}

void NativeInvariantDateTimeContext::clear_cached_time_zone_data() {
    auto state = state_;
    state->cache.store(std::make_shared<State::Epoch>());
}
std::shared_ptr<const InvariantTimeZoneRules> NativeInvariantDateTimeContext::local_rule_snapshot() const {
    auto state = state_;
    return state->local_zone();
}
bool NativeInvariantDateTimeContext::utc_is_daylight(std::int64_t ticks) const {
    auto state = state_;
    return state->local_zone()->utc_is_daylight(ticks);
}
std::shared_ptr<TimeZone> NativeInvariantDateTimeContext::cached_legacy_local(
    std::function<std::shared_ptr<TimeZone>(std::shared_ptr<const InvariantTimeZoneRules>)> factory) const {
    auto state = state_;
    auto epoch = state->cache.load();
    std::lock_guard lock(epoch->mutex);
    if (!epoch->legacy_local)
        epoch->legacy_local = factory(state->local_zone(epoch));
    return epoch->legacy_local;
}
namespace {
bool raw_named_equal(std::u16string_view a, std::u16string_view b) {
    if (a.size() != b.size())
        return false;
    auto surrogate = [](char16_t c) { return c >= 0xd800 && c <= 0xdfff; };
    for (std::size_t i = 0; i < a.size();) {
        if (surrogate(a[i]) || surrogate(b[i])) {
            bool ap = a[i] >= 0xd800 && a[i] <= 0xdbff && i + 1 < a.size() && a[i + 1] >= 0xdc00 &&
                      a[i + 1] <= 0xdfff;
            bool bp = b[i] >= 0xd800 && b[i] <= 0xdbff && i + 1 < b.size() && b[i + 1] >= 0xdc00 &&
                      b[i + 1] <= 0xdfff;
            if (ap != bp)
                return false;
            if (ap) {
                if (!ordinal_equals_ignore_case(utf8(a.substr(i, 2)), utf8(b.substr(i, 2))))
                    return false;
                i += 2;
            } else {
                if (a[i] != b[i])
                    return false;
                ++i;
            }
        } else {
            auto start = i;
            while (i < a.size() && !surrogate(a[i]) && !surrogate(b[i]))
                ++i;
            if (!ordinal_equals_ignore_case(utf8(a.substr(start, i - start)),
                                            utf8(b.substr(start, i - start))))
                return false;
        }
    }
    return true;
}
}
std::shared_ptr<TimeZone> NativeInvariantDateTimeContext::cached_legacy_named(
    std::string key, std::function<std::shared_ptr<TimeZone>()> factory) const {
    return cached_legacy_named_raw(utf16_replacing(key), std::move(factory));
}
std::shared_ptr<TimeZone> NativeInvariantDateTimeContext::cached_legacy_named_raw(
    std::u16string key, std::function<std::shared_ptr<TimeZone>()> factory,
    std::function<bool(std::u16string_view, std::u16string_view)> equal, std::shared_ptr<void> lease) const {
    auto state = state_;
    auto epoch = lease ? std::static_pointer_cast<State::Epoch>(lease) : state->cache.load();
    std::lock_guard lock(epoch->mutex);
    for (auto& entry : epoch->named)
        if (epoch->named_equal(entry.first, key))
            return entry.second;
    auto value = factory();
    if (!epoch->named_equal) {
        if (equal)
            epoch->named_equal = std::move(equal);
        else
            epoch->named_equal = [invariant = state->invariant, nls = state->use_nls](auto a, auto b) {
#ifdef _WIN32
                if (nls)
                    return CompareStringOrdinal(reinterpret_cast<const wchar_t*>(a.data()),
                                                static_cast<int>(a.size()),
                                                reinterpret_cast<const wchar_t*>(b.data()),
                                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
#else
                (void)nls;
#endif
                if (invariant)
                    return a.size() == b.size() && file_system_name::pinned_case_backend(
                                                       file_system_name::CasingMode::GlobalizationInvariant)
                                                       .ordinal_ignore_case_ends_with(a, b);
                return raw_named_equal(a, b);
            };
    }
    epoch->named.emplace_back(std::move(key), value);
    return value;
}
std::shared_ptr<void> NativeInvariantDateTimeContext::legacy_named_cache_lease() const {
    return state_->cache.load();
}
std::optional<bool> NativeInvariantDateTimeContext::legacy_file_locking_override() const {
    return state_->disable_file_locking;
}
bool NativeInvariantDateTimeContext::legacy_name_mapping_allowed() const {
#ifdef _WIN32
    return !state_->invariant && !state_->use_nls;
#else
    return !state_->invariant;
#endif
}
std::optional<std::string> NativeInvariantDateTimeContext::legacy_iana_to_windows(std::string_view id) const {
    auto value = legacy_iana_to_windows_raw(utf16_replacing(id));
    return value ? std::optional<std::string>(utf8(*value)) : std::nullopt;
}
std::optional<std::u16string>
NativeInvariantDateTimeContext::legacy_iana_to_windows_raw(std::u16string_view id) const {
#ifdef _WIN32
    if (!legacy_name_mapping_allowed())
        return {};
    return state_->id_converter ? state_->id_converter(id)
                                : invariant_datetime_platform::windows_system_iana_to_windows(id);
#else
    (void)id;
    return {};
#endif
}
}
