// Copyright (c) Contributors, http://opensimulator.org/
// .NET Runtime source-derived platform model; MIT notice in implementation.
#pragma once
#include "opensim/framework/invariant_datetime_context.hpp"
#include <string>
namespace opensim::framework::invariant_datetime_platform {
AssetDateTime native_utc_now();
AssetDateTime native_from_file_time_utc(std::int64_t);
std::int64_t native_to_file_time_utc(std::int64_t utc_ticks);
bool native_valid_leap_second(int year,int month,int day,int hour,int minute);
int native_two_digit_year_max(bool globalization_invariant);
struct ZoneModel {
    std::int64_t base_offset = 0;
    std::vector<InvariantTimeZoneRule> rules;
};
ZoneModel unix_tzif_local_data(const std::vector<std::uint8_t>&,const std::string&,std::function<std::int64_t()> utc_now);
ZoneModel unix_tzif_zone(const std::vector<std::uint8_t>&,const std::string&,bool disabled,std::int64_t utc_now);
ZoneModel unix_tzif_zone(const std::vector<std::uint8_t>&,const std::string&,bool disabled,std::function<std::int64_t()> utc_now);
#ifndef _WIN32
ZoneModel unix_local_zone(std::optional<bool> disable_file_locking={});
#endif
#ifdef _WIN32
ZoneModel windows_registry_zone(const std::wstring&,bool disabled=false);
std::optional<ZoneModel> windows_registry_zone_lookup(const std::wstring&);
std::optional<std::u16string> windows_system_iana_to_windows(std::u16string_view);
ZoneModel windows_local_zone(bool invariant=false,bool use_nls=false,std::function<std::optional<std::u16string>(std::u16string_view)> converter={});
ZoneModel windows_current_year_zone();
#endif
}
