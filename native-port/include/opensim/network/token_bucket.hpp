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
// C++ adaptation of OpenSim/Region/ClientStack/Linden/UDP/TokenBucket.cs.
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace opensim::network {
class TokenBucket {
public:
    using Clock = std::function<double()>; // Monotonic seconds, as in Util.GetTimeStamp.
    TokenBucket(TokenBucket* parent, float drip_rate, float max_burst, Clock clock = {});
    virtual ~TokenBucket();
    TokenBucket(const TokenBucket&) = delete;
    TokenBucket& operator=(const TokenBucket&) = delete;
    TokenBucket(TokenBucket&&) = delete;
    TokenBucket& operator=(TokenBucket&&) = delete;

    [[nodiscard]] TokenBucket* parent() const noexcept {
        return parent_ && !parent_lifetime_.expired() ? parent_ : nullptr;
    }
    // Like the C# property, assigning Parent does not migrate registered requests.
    void parent(TokenBucket* value);
    virtual float max_drip_rate() const noexcept;
    virtual void max_drip_rate(float value) noexcept;
    [[nodiscard]] float requested_burst() const noexcept { return burst_; }
    void requested_burst(float value) noexcept;
    [[nodiscard]] float burst() const;
    [[nodiscard]] float requested_drip_rate() const noexcept;
    void requested_drip_rate(float value);
    [[nodiscard]] float drip_rate() const;
    [[nodiscard]] float total_drip_request() const noexcept { return total_drip_request_; }
    void total_drip_request(float value) noexcept { total_drip_request_ = value; }
    void register_request(TokenBucket* child, float request);
    void unregister_request(TokenBucket* child);
    bool remove_tokens(std::int32_t amount);
    [[nodiscard]] bool check_tokens(std::int32_t amount) const noexcept;
    [[nodiscard]] std::int32_t get_cat_bytes_can_send(std::int32_t time_ms) const;
    [[nodiscard]] float token_count() const noexcept { return token_count_; }
protected:
    static constexpr float MinDripRate = 500;
    static constexpr float MaxBurst = 7500;
    [[nodiscard]] float drip_rate_modifier() const;
    void drip();
    TokenBucket* parent_;
    float max_drip_rate_ = 0;
    float drip_rate_ = 0;
    float total_drip_request_ = 0;
private:
    Clock clock_;
    double last_drip_;
    float token_count_ = 0;
    float burst_ = 0;
    std::mutex children_mutex_;
    struct Registration {
        TokenBucket* child;
        float request;
        std::weak_ptr<int> lifetime;
    };
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
    std::weak_ptr<int> parent_lifetime_;
    std::vector<Registration> children_;
};

class AdaptiveTokenBucket final : public TokenBucket {
public:
    AdaptiveTokenBucket(TokenBucket* parent, float max_drip_rate, float max_burst,
        bool enabled, Clock clock = {});
    // Preserved as a separate, initially false property: the C# implementation
    // actually gates adaptation with its private constructor flag.
    bool adaptive_enabled = false;
    float max_drip_rate() const noexcept override;
    void max_drip_rate(float value) noexcept override;
    [[nodiscard]] float adjusted_drip_rate() const noexcept { return drip_rate_; }
    void adjusted_drip_rate(float value);
    void expire_packets(std::int32_t count);
    void acknowledge_packets(std::int32_t count);
private:
    static constexpr float MinimumFlow = 50000;
    bool enabled_;
};
}
