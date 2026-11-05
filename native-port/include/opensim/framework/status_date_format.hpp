// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
namespace opensim::framework {
inline constexpr std::string_view invariant_status_date_pattern="MM/dd/yyyy hh:mm:ss";
// Exactly the invariant Gregorian status format. AssetDateTime owns raw-tick
// validation; this function neither accepts nor interprets arbitrary patterns.
std::string format_invariant_status_date(AssetDateTime);
struct StatusDateFormatBackend {
 // True only for unchanged invariant formatting data, not merely an empty
 // culture name: modified invariant clones can have other date/time separators.
 std::function<bool()> is_invariant_culture;
 // Required for other calendars/cultures AND for other format patterns. The
 // host owns culture selection, format validation, errors and localization.
 std::function<std::string(AssetDateTime,const std::string&)> current_culture_format;
};
std::string format_status_date(AssetDateTime,const std::string& pattern,const StatusDateFormatBackend&);
}
namespace opensim::services {
struct FlotsamConsoleBackend;
// Binds only formatting. It does not install clocks, date parsers, task queues,
// scene gathering, file I/O or a production cache host.
void bind_flotsam_status_date_format(FlotsamConsoleBackend&,std::shared_ptr<framework::StatusDateFormatBackend>);
}
