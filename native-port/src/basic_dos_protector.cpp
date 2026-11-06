/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// Copyright (c) Contributors, http://opensimulator.org/
// C++ adaptation; BSD license: native-port/LICENSE.txt.
#include "opensim/framework/basic_dos_protector.hpp"
#include "opensim/framework/tick_count.hpp"
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace opensim::framework {
namespace {
std::int32_t capacity(const std::shared_ptr<BasicDosProtectorOptions>& options) {
    if (!options)
        throw std::invalid_argument("Null DOS protector options");
    return wrapping_add(options->max_requests_in_timeframe, 1);
}
std::string milliseconds(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(15) << value;
    return out.str();
}
std::int32_t to_int32(double value) {
    if (!std::isfinite(value) || value >= 2147483648.0 || value < -2147483648.0)
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}
}
BasicDOSProtector::BasicDOSProtector(std::shared_ptr<BasicDosProtectorOptions> options,
    Clock clock, bool automatic_cleanup, Logger logger)
    : options_(std::move(options)), clock_(clock ? std::move(clock) : Clock{environment_tick_count}),
      logger_(std::move(logger)), general_request_times_(capacity(options_), true),
      timer_interval_ms_(options_->forget_time_span_ms) {
    general_request_times_.put(0);
    if (!(timer_interval_ms_ > 0) || timer_interval_ms_ > 2147483647.0)
        throw std::invalid_argument("Invalid cleanup timer interval");
    if (automatic_cleanup)
        timer_ = std::thread([this] { timer_loop(); });
}
BasicDOSProtector::~BasicDOSProtector() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        timer_changed_.notify_all();
    }
    if (timer_.joinable())
        timer_.join();
}
void BasicDOSProtector::timer_enabled(bool enabled) {
    if (timer_enabled_ == enabled)
        return;
    timer_enabled_ = enabled;
    ++timer_generation_;
    timer_changed_.notify_all();
}
bool BasicDOSProtector::cleanup_enabled() const {
    std::lock_guard lock(mutex_);
    return timer_enabled_;
}
void BasicDOSProtector::timer_loop() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        timer_changed_.wait(lock, [this] { return stopping_ || timer_enabled_; });
        if (stopping_)
            break;
        const auto generation = timer_generation_;
        const auto interval = std::chrono::milliseconds(static_cast<std::int64_t>(std::ceil(timer_interval_ms_)));
        if (timer_changed_.wait_for(lock, interval, [this, generation] {
                return stopping_ || !timer_enabled_ || timer_generation_ != generation;
            }))
            continue;
        // System.Timers.Timer suppresses exceptions raised by Elapsed handlers.
        try {
            expire_blocks();
        } catch (...) {
        }
    }
}
void BasicDOSProtector::log(const std::string& level, const std::string& message) const {
    if (logger_)
        logger_(level, message);
}
bool BasicDOSProtector::is_blocked(const std::string& key) const {
    std::lock_guard lock(mutex_);
    return temp_blocked_.contains(key);
}
bool BasicDOSProtector::throttled() const {
    if (options_->throttled_action == ThrottleAction::DoThrottledMethod)
        return false;
    throw ThrottledError();
}
std::int32_t BasicDOSProtector::deadline() const {
    return wrapping_add(clock_(), to_int32(options_->forget_time_span_ms));
}
bool BasicDOSProtector::process(const std::string& key, const std::string& endpoint) {
    std::lock_guard lock(mutex_);
    if (options_->max_requests_in_timeframe < 1 || options_->request_time_span_ms < 1)
        return true;
    if (temp_blocked_.contains(key))
        return throttled();
    general_request_times_.put(clock_());
    if (options_->max_concurrent_sessions > 0) {
        auto found = sessions_.find(key);
        const auto sessions = found == sessions_.end() ? 0 : found->second;
        if (sessions > options_->max_concurrent_sessions) {
            const bool already_blocked = temp_blocked_.contains(key);
            temp_blocked_[key] = deadline();
            if (!already_blocked) {
                timer_enabled(true);
                log("warn", "[" + options_->reporting_name + "]: client: " + key + " is blocked for "
                    + milliseconds(options_->forget_time_span_ms) + "ms based on concurrency, endpoint: " + endpoint);
            }
        } else {
            sessions_[key] = wrapping_add(sessions, 1);
        }
    }
    if (general_request_times_.size() == general_request_times_.capacity() &&
        environment_tick_count_subtract(clock_(), general_request_times_.get()) < options_->request_time_span_ms) {
        if (deeper_inspection(key, endpoint))
            return true;
        return throttled();
    }
    return true;
}
void BasicDOSProtector::process_end(const std::string& key, const std::string& /*endpoint*/) {
    std::lock_guard lock(mutex_);
    auto found = sessions_.find(key);
    if (found == sessions_.end()) {
        sessions_.emplace(key, 1);
    } else {
        found->second = wrapping_add(found->second, -1);
        if (found->second <= 0)
            sessions_.erase(found);
    }
}
bool BasicDOSProtector::deeper_inspection(const std::string& key, const std::string& endpoint) {
    auto found = deeper_inspection_.find(key);
    if (found == deeper_inspection_.end()) {
        auto buffer = std::make_unique<CircularBuffer<std::int32_t>>(capacity(options_), true);
        buffer->put(clock_());
        deeper_inspection_.emplace(key, std::move(buffer));
        timer_enabled(true);
    } else {
        auto& buffer = *found->second;
        buffer.put(clock_());
        if (buffer.size() == buffer.capacity() &&
            environment_tick_count_subtract(clock_(), buffer.get()) < options_->request_time_span_ms) {
            temp_blocked_[key] = deadline();
            log("warn", "[" + options_->reporting_name + "]: client: " + key + " is blocked for "
                + milliseconds(options_->forget_time_span_ms) + "ms, endpoint:" + endpoint);
            return false;
        }
    }
    return true;
}
void BasicDOSProtector::expire_blocks() {
    std::lock_guard lock(mutex_);
    timer_enabled(false);
    std::vector<std::string> removes;
    for (const auto& [key, when] : temp_blocked_)
        if (environment_tick_count_subtract(clock_(), when) > 0)
            removes.push_back(key);
    for (const auto& key : removes) {
        temp_blocked_.erase(key);
        deeper_inspection_.erase(key);
        sessions_.erase(key);
        log("info", "[" + options_->reporting_name + "] client: " + key + " is no longer blocked.");
    }
    if (!temp_blocked_.empty())
        timer_enabled(true);
}
}
