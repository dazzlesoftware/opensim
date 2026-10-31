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
// C++ adaptation of the tick arithmetic in OpenSim/Framework/Util.cs.
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <bit>
#include <chrono>
#include <cstdint>

namespace opensim::framework {
inline constexpr std::int32_t EnvironmentTickCountMask = 0x3fffffff;
inline std::int32_t wrapping_add(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
inline std::int32_t environment_tick_count() noexcept {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return static_cast<std::int32_t>(static_cast<std::uint64_t>(ms) & EnvironmentTickCountMask);
}
inline std::int32_t environment_tick_count_subtract(std::int32_t now, std::int32_t before) noexcept {
    auto diff = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(before));
    return diff >= 0 ? diff : wrapping_add(diff, EnvironmentTickCountMask + 1);
}
inline std::int32_t environment_tick_count_compare(std::int32_t a, std::int32_t b, std::int32_t now) noexcept {
    if (static_cast<std::int64_t>(now) - a >= 0) a = wrapping_add(a, EnvironmentTickCountMask + 1);
    if (static_cast<std::int64_t>(now) - b >= 0) b = wrapping_add(b, EnvironmentTickCountMask + 1);
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
}
