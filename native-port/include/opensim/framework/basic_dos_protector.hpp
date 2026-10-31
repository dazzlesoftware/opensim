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
// C++ adaptation of OpenSim/Framework/BasicDOSProtector.cs.
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/circular_buffer.hpp"
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace opensim::framework {
enum class ThrottleAction { DoThrottledMethod, DoThrow };
struct BasicDosProtectorOptions {
    std::int32_t max_requests_in_timeframe = 0;
    double request_time_span_ms = 0;
    double forget_time_span_ms = 0;
    std::string reporting_name = "BASICDOSPROTECTOR";
    ThrottleAction throttled_action = ThrottleAction::DoThrottledMethod;
    std::int32_t max_concurrent_sessions = 0;
};
class ThrottledError final : public std::runtime_error {
public:
    ThrottledError() : std::runtime_error("Throttled") { }
};
class BasicDOSProtector {
public:
    using Clock = std::function<std::int32_t()>;
    using Logger = std::function<void(const std::string& level, const std::string& message)>;
    // Set automatic_cleanup=false only when a host scheduler calls expire_blocks.
    // Shared options preserve the reference's live configuration object semantics;
    // callers must quiesce operations before changing them.
    explicit BasicDOSProtector(std::shared_ptr<BasicDosProtectorOptions> options,
        Clock clock = {}, bool automatic_cleanup = true, Logger logger = {});
    ~BasicDOSProtector();
    BasicDOSProtector(const BasicDOSProtector&) = delete;
    BasicDOSProtector& operator=(const BasicDOSProtector&) = delete;
    bool is_blocked(const std::string& key) const;
    bool process(const std::string& key, const std::string& endpoint);
    void process_end(const std::string& key, const std::string& endpoint);
    // Executes the source cleanup callback, retaining its tick-wrap semantics.
    void expire_blocks();
    [[nodiscard]] bool cleanup_enabled() const;
private:
    bool deeper_inspection(const std::string& key, const std::string& endpoint);
    void timer_enabled(bool enabled);
    void timer_loop();
    bool throttled() const;
    std::int32_t deadline() const;
    void log(const std::string& level, const std::string& message) const;
    std::shared_ptr<BasicDosProtectorOptions> options_;
    Clock clock_;
    Logger logger_;
    CircularBuffer<std::int32_t> general_request_times_;
    std::unordered_map<std::string, std::unique_ptr<CircularBuffer<std::int32_t>>> deeper_inspection_;
    std::unordered_map<std::string, std::int32_t> temp_blocked_, sessions_;
    mutable std::recursive_mutex mutex_;
    std::condition_variable_any timer_changed_;
    bool timer_enabled_ = false, stopping_ = false;
    std::uint64_t timer_generation_ = 0;
    double timer_interval_ms_;
    std::thread timer_;
};
}
