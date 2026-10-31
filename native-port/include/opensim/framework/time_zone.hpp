// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
namespace opensim::framework {
class TimeZone {
public:
    bool daylight_at(std::int64_t utc_seconds)const;
    const std::string& id()const{return id_;}
private:
    friend class TimeZoneDatabase;
    TimeZone(std::string id,std::function<bool(std::int64_t)> daylight):id_(std::move(id)),daylight_(std::move(daylight)){}
    std::string id_;
    std::function<bool(std::int64_t)> daylight_;
};
class TimeZoneDatabase {
public:
    TimeZoneDatabase();
    ~TimeZoneDatabase();
    std::shared_ptr<TimeZone> find(const std::optional<std::string>& id);
    std::shared_ptr<TimeZone> local();
    void clear();
private:
    struct State;
    std::unique_ptr<State> state_;
};
TimeZoneDatabase& time_zones();
}
