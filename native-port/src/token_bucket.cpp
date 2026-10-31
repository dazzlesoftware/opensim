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
#include "opensim/network/token_bucket.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace opensim::network {
namespace {
float minimum(float a, float b) { return std::isnan(a) ? a : (a < b ? a : b); }
float maximum(float a, float b) { return std::isnan(a) ? a : (a > b ? a : b); }
double timestamp() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
TokenBucket::TokenBucket(TokenBucket* parent, float rate, float max_burst, Clock clock)
    : parent_(nullptr), clock_(clock ? std::move(clock) : Clock{timestamp}) {
    this->parent(parent);
    requested_drip_rate(rate);
    requested_burst(max_burst);
    last_drip_ = clock_() + 1000; // Source uses seconds and an actual +1000.
}
TokenBucket::~TokenBucket() {
    // Native lifetime protection: callers must quiesce access before destruction.
    // Explicit unregister remains available; no dangling child keys are retained.
    if (auto* p = parent()) p->unregister_request(this);
    for (const auto& entry : children_)
        if (!entry.lifetime.expired() && entry.child->parent_ == this) entry.child->parent_ = nullptr;
}
void TokenBucket::parent(TokenBucket* value) {
    for (auto* ancestor = value; ancestor; ancestor = ancestor->parent())
        if (ancestor == this) throw std::invalid_argument("Token bucket parent cycle");
    parent_ = value;
    parent_lifetime_ = value ? value->lifetime_ : std::weak_ptr<int>{};
}
float TokenBucket::max_drip_rate() const noexcept { return max_drip_rate_; }
void TokenBucket::max_drip_rate(float value) noexcept { max_drip_rate_ = value; }
void TokenBucket::requested_burst(float value) noexcept {
    burst_ = value < 0 ? 0 : value;
    if (burst_ > MaxBurst) burst_ = MaxBurst;
}
float TokenBucket::requested_drip_rate() const noexcept {
    return drip_rate_ == 0 ? total_drip_request_ : drip_rate_;
}
void TokenBucket::requested_drip_rate(float value) {
    drip_rate_ = value < 0 ? 0 : value;
    total_drip_request_ = drip_rate_;
    if (auto* p = parent()) p->register_request(this, drip_rate_);
}
float TokenBucket::drip_rate() const {
    float rate = minimum(requested_drip_rate(), total_drip_request_);
    auto* p = parent();
    if (!p) return rate;
    rate *= p->drip_rate_modifier();
    return rate < MinDripRate ? MinDripRate : rate;
}
float TokenBucket::drip_rate_modifier() const {
    const float rate = drip_rate();
    return rate >= total_drip_request_ ? 1.0f : rate / total_drip_request_;
}
float TokenBucket::burst() const { return requested_burst() * drip_rate_modifier(); }
void TokenBucket::register_request(TokenBucket* child, float request) {
    if (!child) throw std::invalid_argument("Null token bucket child");
    {
        std::lock_guard lock(children_mutex_);
        auto found = std::find_if(children_.begin(), children_.end(),
            [child](const auto& entry) { return entry.child == child; });
        if (found == children_.end()) children_.push_back({child, request, child->lifetime_});
        else { found->request = request; found->lifetime = child->lifetime_; }
        total_drip_request_ = 0;
        for (const auto& entry : children_) total_drip_request_ += entry.request;
    }
    if (auto* p = parent()) p->register_request(this, minimum(requested_drip_rate(), total_drip_request_));
}
void TokenBucket::unregister_request(TokenBucket* child) {
    {
        std::lock_guard lock(children_mutex_);
        std::erase_if(children_, [child](const auto& entry) { return entry.child == child; });
        total_drip_request_ = 0;
        for (const auto& entry : children_) total_drip_request_ += entry.request;
    }
    if (auto* p = parent()) p->register_request(this, minimum(requested_drip_rate(), total_drip_request_));
}
bool TokenBucket::remove_tokens(std::int32_t amount) {
    drip();
    if (token_count_ <= 0 || std::isnan(token_count_)) return false;
    token_count_ -= static_cast<float>(amount);
    return true;
}
bool TokenBucket::check_tokens(std::int32_t /*amount*/) const noexcept { return token_count_ > 0; }
std::int32_t TokenBucket::get_cat_bytes_can_send(std::int32_t time_ms) const {
    // The source multiplies int * float first, then promotes to double for 1e-3.
    const double count = static_cast<float>(time_ms) * drip_rate() * 1e-3;
    if (!std::isfinite(count) || count >= 2147483648.0 || count < -2147483648.0)
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(count);
}
void TokenBucket::drip() {
    if (drip_rate() == 0) return;
    const double now = clock_();
    const double delta = now - last_drip_;
    last_drip_ = now;
    if (delta <= 0) return;
    token_count_ += static_cast<float>(delta) * drip_rate();
    const float limit = burst();
    if (token_count_ > limit) token_count_ = limit;
}
AdaptiveTokenBucket::AdaptiveTokenBucket(TokenBucket* parent, float rate, float max_burst,
    bool enabled, Clock clock) : TokenBucket(parent, rate, max_burst, std::move(clock)), enabled_(enabled) {
    max_drip_rate_ = rate == 0 ? total_drip_request_ : maximum(rate, MinimumFlow);
    drip_rate_ = enabled ? max_drip_rate_ * .5f : max_drip_rate_;
    if (auto* p = this->parent()) p->register_request(this, drip_rate_);
}
float AdaptiveTokenBucket::max_drip_rate() const noexcept {
    return max_drip_rate_ == 0 ? total_drip_request_ : max_drip_rate_;
}
void AdaptiveTokenBucket::max_drip_rate(float value) noexcept {
    max_drip_rate_ = value == 0 ? total_drip_request_ : maximum(value, MinimumFlow);
}
void AdaptiveTokenBucket::adjusted_drip_rate(float value) {
    const float max_rate = max_drip_rate();
    if (MinimumFlow > max_rate) throw std::invalid_argument("Minimum flow exceeds maximum drip rate");
    drip_rate_ = value < MinimumFlow ? MinimumFlow : (value > max_rate ? max_rate : value);
    if (auto* p = parent()) p->register_request(this, drip_rate_);
}
void AdaptiveTokenBucket::expire_packets(std::int32_t count) {
    if (!enabled_) return;
    const double result = adjusted_drip_rate() / std::pow(2.0, count);
    const auto integer = !std::isfinite(result) || result >= 9223372036854775808.0 || result < -9223372036854775808.0
        ? std::numeric_limits<std::int64_t>::min() : static_cast<std::int64_t>(result);
    adjusted_drip_rate(static_cast<float>(integer));
}
void AdaptiveTokenBucket::acknowledge_packets(std::int32_t count) {
    if (enabled_) adjusted_drip_rate(adjusted_drip_rate() + static_cast<float>(count));
}
}
