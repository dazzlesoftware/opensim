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
#include "opensim/network/lludp.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace network::lludp;
namespace {
const framework::Uuid FixtureId("12345678-1234-5678-90ab-123456789abc");
std::string hex(std::span<const std::uint8_t> bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto byte : bytes)
        out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}
FieldValue fixture(const FieldSpec& field, std::uint32_t index, int seed) {
    const bool zero = seed == 0;
    switch (field.type) {
    case FieldType::U8:
        return std::uint64_t{zero ? 0 : index % 251};
    case FieldType::S8:
        return std::int64_t{zero ? 0 : -static_cast<std::int64_t>(index % 100 + 1)};
    case FieldType::U16:
    case FieldType::IPPORT:
        return std::uint64_t{zero ? 0 : (index * 19) % 65536};
    case FieldType::S16:
        return std::int64_t{zero ? 0 : -static_cast<std::int64_t>(index % 30000 + 1)};
    case FieldType::U32:
    case FieldType::IPADDR:
        return std::uint64_t{zero ? 0 : 0xab000000u | index};
    case FieldType::S32:
        return std::int64_t{zero ? 0 : -static_cast<std::int64_t>((index % 100000 + 1) * 1000)};
    case FieldType::U64:
        return zero ? std::uint64_t{0} : 0x0123456789abcdefull ^ (static_cast<std::uint64_t>(index) << 32);
    case FieldType::S64:
        return zero ? std::int64_t{0} : -std::int64_t{0x123456789abcde} + index;
    case FieldType::BOOL:
        return !zero && index % 2 == 0;
    case FieldType::F32:
        return zero ? 0.0 : static_cast<double>(index % 1000) / 8.0 - 60.0;
    case FieldType::F64:
        return zero ? 0.0 : static_cast<double>(index % 1000) / 16.0 + 17.0;
    case FieldType::LLUUID:
        return zero ? framework::Uuid{} : FixtureId;
    case FieldType::LLVector3:
        return zero ? std::vector<double>(3) : std::vector<double>{1.25, -2.5, 128.75};
    case FieldType::LLVector3d:
        return zero ? std::vector<double>(3) : std::vector<double>{1.25, -2.5, 128000000.75};
    case FieldType::LLVector4:
        return zero ? std::vector<double>(4) : std::vector<double>{1.25, -2.5, 128.75, -.125};
    case FieldType::LLQuaternion:
        return zero        ? std::vector<double>(4)
               : seed == 1 ? std::vector<double>{0, 0, 0, 1}
               : seed == 2 ? std::vector<double>{.5, .5, .5, .5}
                           : std::vector<double>{.5, .5, .5, -.5};
    case FieldType::Variable:
    case FieldType::Fixed: {
        const auto count = field.type == FieldType::Fixed ? field.size
                           : zero                         ? 0
                                                          : static_cast<std::size_t>(index % 11 + 1);
        std::vector<std::uint8_t> bytes(count);
        for (std::size_t i = 0; i < count; ++i)
            bytes[i] = zero ? 0 : static_cast<std::uint8_t>(index + i * 17);
        return bytes;
    }
    }
    throw std::invalid_argument("Unhandled fixture type");
}
}
int main() {
    std::string name;
    int seed;
    while (std::cin >> name >> seed) {
        try {
            auto packet = make_packet(name);
            packet.sequence = 0xff000000u + seed;
            packet.resent = seed == 2;
            packet.appended_acks = seed % 4 != 0;
            for (int i = 0; i < seed % 4; ++i)
                packet.acknowledgements.push_back(i == 0 ? 0 : 0xabcdef00u + i);
            for (const auto& block : packet.spec->blocks) {
                auto& rows = packet.blocks.at(block.name);
                const auto count = block.variable ? static_cast<std::size_t>(seed < 4    ? seed % 4
                                                                             : seed == 4 ? 0
                                                                             : seed == 5 ? 17
                                                                             : seed == 6 ? 100
                                                                                         : 300)
                                                  : block.count;
                rows.clear();
                for (std::size_t b = 0; b < count; ++b) {
                    auto row = make_block(block);
                    for (std::size_t f = 0; f < block.fields.size(); ++f) {
                        const auto index =
                            static_cast<std::uint32_t>(f + b * 17 + packet.spec->enum_value * 13 + seed * 7);
                        row[block.fields[f].name] = fixture(block.fields[f], index, seed);
                    }
                    rows.push_back(std::move(row));
                }
            }
            if (seed >= 4) {
                auto fragments = encode_packets(packet);
                for (std::size_t i = 0; i < fragments.size(); ++i) {
                    if (i)
                        std::cout << ',';
                    std::cout << hex(fragments[i]);
                }
                std::cout << '|' << packet.appended_acks << '\n';
                continue;
            }
            auto raw = encode_packet(packet), wire = encode_datagram(packet);
            std::string decoded;
            try {
                decoded = hex(encode_packet(decode_datagram(wire)));
            } catch (const BaselineDecodeError&) {
                decoded = "BASELINE_FIXED_BLOCK_ERROR";
            }
            std::cout << hex(raw) << '|' << hex(wire) << '|' << decoded << '|'
                      << packet.valid_ids(FixtureId, FixtureId) << '|'
                      << packet.valid_ids(framework::Uuid{}, FixtureId) << '\n';
        } catch (const std::exception& error) {
            std::cout << "ERROR:" << error.what() << '\n';
        }
    }
}
