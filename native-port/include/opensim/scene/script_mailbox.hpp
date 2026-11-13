// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/framework/uuid.hpp"
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace opensim::scene {
struct ScriptEmail {
    std::string time, sender, subject, message;
    std::int32_t remaining = 0;
};

class ScriptMailbox {
public:
    using Clock = std::chrono::steady_clock;
    explicit ScriptMailbox(int owner_per_hour = 500, int recipient_per_hour = 50,
                           int smtp_per_day = 100, int smtp_recipient_per_hour = 50);
    bool allow_owner(framework::Uuid owner, Clock::time_point now = Clock::now());
    bool allow_recipient(std::string address, Clock::time_point now = Clock::now());
    bool allow_smtp(std::string address, Clock::time_point now = Clock::now());
    void add(framework::Uuid part);
    void remove(framework::Uuid part);
    void insert(framework::Uuid part, ScriptEmail message, Clock::time_point now = Clock::now());
    std::optional<ScriptEmail> next(framework::Uuid part, std::string_view sender,
                                    std::string_view subject, Clock::time_point now = Clock::now());

private:
    static constexpr auto timeout = std::chrono::minutes(30);
    static constexpr auto throttle_window = std::chrono::hours(1);
    struct Throttle {
        double remaining;
        Clock::time_point last;
    };
    static bool consume(Throttle& throttle, int limit, Clock::time_point now);
    int owner_per_hour_, recipient_per_hour_;
    int smtp_per_day_, smtp_recipient_per_hour_;
    std::mutex mutex_;
    std::map<framework::Uuid, Throttle> owners_;
    std::map<std::string, Throttle> recipients_;
    std::map<std::string, Throttle> smtp_recipients_;
    Throttle smtp_daily_;
    Clock::time_point next_owner_expiry_ = Clock::now() + throttle_window;
    Clock::time_point next_recipient_expiry_ = next_owner_expiry_;
    Clock::time_point next_smtp_recipient_expiry_ = next_owner_expiry_;
    std::map<framework::Uuid, std::deque<ScriptEmail>> queues_;
    std::map<framework::Uuid, Clock::time_point> expires_;
    Clock::time_point next_expiry_ = Clock::now() + timeout;
};
}
