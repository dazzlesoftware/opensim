// Copyright (c) Contributors, http://opensimulator.org/
// Portions derived from .NET Runtime v8.0.31, .NET Foundation, MIT license.
// See third_party/dotnet_collation/LICENSE for the .NET MIT license.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include <functional>
#include <optional>
#include <string_view>
namespace opensim::framework {
struct InvariantDateTimeParseContext {
 using Date=AssetDateTime;
 bool globalization_invariant_mode=false;
 struct UtcOffset {
     std::int64_t ticks = 0;
     bool ambiguous_daylight = false;
 };
 // Keep each source clock read separate. Missing fields and very early local
 // timezone adjustment can consult Now independently of the caller's comparison.
 std::function<Date()> now;
 std::function<int()> two_digit_year_max;
 std::function<std::int64_t(Date)> local_offset_no_throw;
 std::function<UtcOffset(std::int64_t utc_ticks)> utc_offset;
 // Gregorian DateTime.TryCreate delegates second60 validity to the platform.
 // Hosts whose runtime/platform does not support leap seconds return false.
 std::function<bool(int year,int month,int day,int hour,int minute)> valid_leap_second;
};
// Full invariant-culture DateTime.TryParse with DateTimeStyles.None. Other
// cultures and custom mutable DateTimeFormatInfo instances are separate providers.
// Failure resets output to DateTime.MinValue, as does the original string API.
bool try_parse_invariant_datetime(std::optional<std::u16string_view>,AssetDateTime&,const InvariantDateTimeParseContext&);
// Fixed cookie Expires styles: AllowWhiteSpaces | AdjustToUniversal (23).
bool try_parse_invariant_datetime_cookie_expiry(std::optional<std::u16string_view>,AssetDateTime&,const InvariantDateTimeParseContext&);
bool try_parse_invariant_datetime_utf8(const std::optional<std::string>&,AssetDateTime&,const InvariantDateTimeParseContext&);
}
