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
#include "opensim/framework/fake_parcel.hpp"
namespace opensim::framework {
Uuid build_fake_parcel_id(std::uint64_t handle, std::uint32_t x, std::uint32_t y) {
    return build_fake_parcel_id(handle, x, y, 0);
}
Uuid build_fake_parcel_id(std::uint64_t handle, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    std::array<std::uint8_t, 16> bytes{};
    for (unsigned i = 0; i < 8; ++i)
        bytes[i] = static_cast<std::uint8_t>(handle >> (i * 8));
    bytes[8] = static_cast<std::uint8_t>(x);
    bytes[9] = static_cast<std::uint8_t>(x >> 8);
    bytes[10] = static_cast<std::uint8_t>(z);
    bytes[11] = static_cast<std::uint8_t>(z >> 8);
    bytes[12] = static_cast<std::uint8_t>(y);
    bytes[13] = static_cast<std::uint8_t>(y >> 8);
    return Uuid::from_bytes(bytes);
}
bool parse_fake_parcel_id(Uuid id, FakeParcelPosition& position) {
    const auto& bytes = id.bytes();
    position = {};
    for (unsigned i = 0; i < 8; ++i)
        position.region_handle |= std::uint64_t{bytes[i]} << (i * 8);
    position.x = bytes[8] | (std::uint32_t{bytes[9]} << 8);
    position.z = bytes[10] | (std::uint32_t{bytes[11]} << 8);
    position.y = bytes[12] | (std::uint32_t{bytes[13]} << 8);
    return bytes[0] == 0 && bytes[4] == 0 && bytes[9] < 64 && bytes[13] < 64 && bytes[14] == 0 &&
           bytes[15] == 0;
}
}
