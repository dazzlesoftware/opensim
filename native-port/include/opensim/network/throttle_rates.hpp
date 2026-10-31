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
// C++ adaptation of ThrottleRates.cs and Framework/ThrottleOutPacketType.cs.
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string_view>

namespace opensim::network {
enum class ThrottleOutPacketType : std::int32_t {
    Unknown = -1, Resend = 0, Land = 1, Wind = 2, Cloud = 3, Task = 4, Texture = 5, Asset = 6
};
enum class ThrottleOutPacketTypeFlags : std::int32_t {
    Land = 1 << 0, Wind = 1 << 1, Cloud = 1 << 2,
    Task = 1 << 3, Texture = 1 << 4, Asset = 1 << 5
};
struct ThrottleRates {
    using ReadInt = std::function<std::int32_t(std::string_view, std::int32_t)>;
    std::int32_t resend = 6625, land = 9125, wind = 1750, cloud = 1750,
        task = 18500, texture = 18500, asset = 10500, total = 66750;
    bool adaptive_throttles_enabled = false;
    std::int64_t minimum_adaptive_throttle_rate = 0;
    std::int32_t client_max_rate = 640000;
    float burst_time = 10e-3f;

    // Callback reads integers from the ClientStack.LindenUDP configuration section.
    // An empty callback represents an absent section. Error handling matches C#.
    explicit ThrottleRates(const ReadInt& read = {}) {
        try {
            if (read) {
                client_max_rate = read("client_throttle_max_bps", client_max_rate);
                client_max_rate = std::clamp(client_max_rate, 6250, 1000000);
                adaptive_throttles_enabled = false;
                minimum_adaptive_throttle_rate = read("adaptive_throttle_min_bps", 32000);
            }
        } catch (...) { }
    }
    [[nodiscard]] std::int32_t get_rate(ThrottleOutPacketType type) const noexcept {
        switch (type) {
        case ThrottleOutPacketType::Resend: return resend;
        case ThrottleOutPacketType::Land: return land;
        case ThrottleOutPacketType::Wind: return wind;
        case ThrottleOutPacketType::Cloud: return cloud;
        case ThrottleOutPacketType::Task: return task;
        case ThrottleOutPacketType::Texture: return texture;
        case ThrottleOutPacketType::Asset: return asset;
        default: return 0;
        }
    }
};
}
