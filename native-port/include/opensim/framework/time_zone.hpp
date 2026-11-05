// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
namespace opensim::framework {
using TimeZoneText = std::shared_ptr<const std::u16string>;
TimeZoneText time_zone_utc_id_text();
class NativeInvariantDateTimeContext;
class NativeTimeZoneError : public std::runtime_error {
  public:
    NativeTimeZoneError(std::string type, std::uint32_t hresult, std::u16string message,
                        std::optional<std::u16string> parameter = {},
                        std::shared_ptr<const NativeTimeZoneError> inner = {});
    std::string source_type;
    std::uint32_t hresult;
    std::u16string message;
    std::optional<std::u16string> parameter;
    std::shared_ptr<const NativeTimeZoneError> inner;
    std::optional<std::u16string> file_name;
};
class TimeZone {
  public:
    bool daylight_at(std::int64_t utc_seconds) const;
    bool daylight_at_ticks(std::int64_t utc_ticks) const;
    const std::string& id() const {
        return id_;
    }
    const std::u16string& id_utf16() const {
        return *raw_id_;
    }
    const TimeZoneText& id_text() const {
        return raw_id_;
    }

  private:
    friend class TimeZoneDatabase;
    TimeZone(std::string id, std::function<bool(std::int64_t)> daylight);
    TimeZone(TimeZoneText id, std::function<bool(std::int64_t)> daylight);
    std::string id_;
    std::function<bool(std::int64_t)> daylight_;
    TimeZoneText raw_id_;
};
struct NativeTimeZoneLookupOptions {
    // Bind selected AppLocalICU Windows-ID mapping explicitly when it differs from system ICU.
    std::function<std::optional<std::u16string>(std::u16string_view)> windows_to_iana;
    // Captured by the shared epoch on first successful insert; bind selected ICU ordinal casing with its mapper.
    std::function<bool(std::u16string_view, std::u16string_view)> ordinal_ignore_case_equal;
};
class TimeZoneDatabase {
  public:
    TimeZoneDatabase();
    explicit TimeZoneDatabase(std::shared_ptr<NativeInvariantDateTimeContext>);
    TimeZoneDatabase(std::shared_ptr<NativeInvariantDateTimeContext>, NativeTimeZoneLookupOptions);
    ~TimeZoneDatabase();
    std::shared_ptr<TimeZone> find(const std::optional<std::string>& id);
    std::shared_ptr<TimeZone> find_utf16(const std::optional<std::u16string>& id);
    std::shared_ptr<TimeZone> find_text(TimeZoneText id);
    std::shared_ptr<TimeZone> local();
    void clear();

  private:
    struct State;
    std::unique_ptr<State> state_;
};
TimeZoneDatabase& time_zones();
}
