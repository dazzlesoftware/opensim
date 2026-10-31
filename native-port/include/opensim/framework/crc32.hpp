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
// C++ adaptation of OpenSim/Framework/Crc32.cs; BSD license: native-port/LICENSE.txt.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace opensim::framework {
class Crc32 {
public:
    static constexpr std::uint32_t DefaultPolynomial = 0xedb88320u;
    static constexpr std::uint32_t DefaultSeed = 0xffffffffu;
    explicit Crc32(std::uint32_t polynomial = DefaultPolynomial, std::uint32_t seed = DefaultSeed);
    void initialize() noexcept;
    void update(std::span<const std::uint8_t> buffer) noexcept;
    // Mirrors HashCore including its legacy interpretation of length as an end index.
    void hash_core(std::span<const std::uint8_t> buffer, std::int32_t start, std::int32_t length);
    [[nodiscard]] std::uint32_t value() const noexcept;
    [[nodiscard]] std::array<std::uint8_t, 4> hash_final() const noexcept;
    static constexpr int hash_size() noexcept { return 32; }
    static std::uint32_t compute(std::span<const std::uint8_t> buffer);
    static std::uint32_t compute(std::uint32_t seed, std::span<const std::uint8_t> buffer);
    static std::uint32_t compute(std::uint32_t polynomial, std::uint32_t seed,
        std::span<const std::uint8_t> buffer);
private:
    std::array<std::uint32_t, 256> table_{};
    std::uint32_t seed_;
    std::uint32_t hash_;
};
}
