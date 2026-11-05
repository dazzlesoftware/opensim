// Copyright (c) Contributors, http://opensimulator.org/
// Portions derived from .NET Runtime v8.0.31, .NET Foundation, MIT license.
// See third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/invariant_datetime_parse.hpp"
#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <stdexcept>
namespace opensim::framework {
class TimeZone;
class TimeZoneDatabase;
// Source TimeZoneInfo consumed rule model. Tick precision is intentional.
struct InvariantTimeZoneTransition {
    std::int64_t time_of_day_ticks = 0;
    int month = 1, week = 1, day = 1, day_of_week = 0;
    bool fixed_date = true;
};
struct InvariantTimeZoneRule {
    AssetDateTime start, end;
    std::int64_t daylight_delta_ticks = 0, base_offset_delta_ticks = 0;
    InvariantTimeZoneTransition daylight_start, daylight_end;
    bool no_daylight_transitions = false;
};
class InvariantTimeZoneRules {
    struct State;
    std::shared_ptr<const State> state_;

  public:
    InvariantTimeZoneRules(std::int64_t base_offset_ticks, std::vector<InvariantTimeZoneRule>);
    InvariantDateTimeParseContext::UtcOffset utc_offset(std::int64_t ticks) const;
    bool utc_is_daylight(std::int64_t ticks) const;
    std::int64_t local_offset_no_throw(AssetDateTime) const;
    // Dedicated Windows DateTime.Now path evaluates first current-year rule using UTC year.
    InvariantDateTimeParseContext::UtcOffset current_year_offset(
        std::int64_t utc_ticks,
        std::function<std::shared_ptr<const InvariantTimeZoneRules>()> historical_local) const;
};
struct NativeInvariantDateTimeContextOptions {
    bool globalization_invariant_mode = false;
    bool use_nls = false;
    // Required when hosting configured AppLocalIcu: bind the exact selected ICU
    // converter. Empty selects the source Windows system-ICU loader. Other
    // globalization configurations must be supplied explicitly by the host.
    std::function<std::optional<std::u16string>(std::u16string_view)> iana_to_windows;
    // Null uses the shared lazy source FileStream policy on Unix; an explicit
    // host override bypasses its initialization and does not reset the latch.
    std::optional<bool> disable_file_locking = {};
};
class NativeDateTimeError : public std::out_of_range {
  public:
    std::optional<std::string> parameter;
    explicit NativeDateTimeError(std::string message, std::optional<std::string> name = {})
        : out_of_range(std::move(message)), parameter(std::move(name)) {}
    const char* source_type() const noexcept {
        return "System.ArgumentOutOfRangeException";
    }
    std::uint32_t hresult() const noexcept {
        return 0x80131502u;
    }
};
// One owner models a source runtime's shared timezone/calendar caches. Share
// its callbacks/copies across consumers; do not construct a fresh owner per parse.
// Options are the source runtime configuration, supplied by the native host.
class NativeInvariantDateTimeContext {
    struct State;
    std::shared_ptr<State> state_;
    friend class TimeZoneDatabase;
    std::optional<bool> legacy_file_locking_override() const;
    bool legacy_name_mapping_allowed() const;
    std::optional<std::string> legacy_iana_to_windows(std::string_view) const;
    std::optional<std::u16string> legacy_iana_to_windows_raw(std::u16string_view) const;
    std::shared_ptr<TimeZone> cached_legacy_local(
        std::function<std::shared_ptr<TimeZone>(std::shared_ptr<const InvariantTimeZoneRules>)>) const;
    std::shared_ptr<TimeZone> cached_legacy_named(std::string,
                                                  std::function<std::shared_ptr<TimeZone>()>) const;
    std::shared_ptr<TimeZone>
        cached_legacy_named_raw(std::u16string, std::function<std::shared_ptr<TimeZone>()>,
                                std::function<bool(std::u16string_view, std::u16string_view)> = {},
                                std::shared_ptr<void> = {}) const;
    std::shared_ptr<void> legacy_named_cache_lease() const;

  public:
    explicit NativeInvariantDateTimeContext(bool globalization_invariant_mode = false);
    explicit NativeInvariantDateTimeContext(NativeInvariantDateTimeContextOptions);
    InvariantDateTimeParseContext callbacks() const;
    std::shared_ptr<const InvariantTimeZoneRules> local_rule_snapshot() const;
    bool utc_is_daylight(std::int64_t ticks) const;
    AssetDateTime now() const;
    AssetDateTime utc_now() const;
    AssetDateTime to_local_time(AssetDateTime) const;
    AssetDateTime from_file_time_utc(std::int64_t file_time) const;
    std::int64_t to_file_time_utc(AssetDateTime) const;
    void clear_cached_time_zone_data();
};
// Shared default source-runtime owner. Constructed lazily without clock reads.
std::shared_ptr<NativeInvariantDateTimeContext> shared_native_invariant_datetime_context();
}
