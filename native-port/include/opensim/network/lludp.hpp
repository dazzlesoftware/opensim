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
#pragma once
#include "opensim/framework/uuid.hpp"
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>
#include <variant>
#include <vector>
namespace opensim::network::lludp {
enum class Frequency : std::uint8_t { Low, Medium, High };
enum class FieldType {
    U8,
    S8,
    U16,
    S16,
    U32,
    S32,
    U64,
    S64,
    F32,
    F64,
    BOOL,
    LLUUID,
    LLVector3,
    LLVector3d,
    LLVector4,
    LLQuaternion,
    IPADDR,
    IPPORT,
    Fixed,
    Variable
};
struct FieldSpec {
    std::string name;
    FieldType type;
    std::size_t size;
};
struct BlockSpec {
    std::string name;
    std::size_t count;
    bool variable;
    std::vector<FieldSpec> fields;
};
struct PacketSpec {
    std::string name;
    std::uint32_t enum_value;
    Frequency frequency;
    std::uint16_t id;
    bool reliable, zerocoded, validate_ids;
    std::vector<BlockSpec> blocks;
};
using FieldValue = std::variant<std::uint64_t, std::int64_t, double, bool, framework::Uuid,
                                std::vector<double>, std::vector<std::uint8_t>>;
using Block = std::map<std::string, FieldValue>;
struct Packet {
    const PacketSpec* spec = nullptr;
    std::uint32_t sequence = 0;
    bool reliable = true, resent = false, zerocoded = false, appended_acks = false;
    std::vector<std::uint8_t> extra_header;
    std::vector<std::uint32_t> acknowledgements;
    std::map<std::string, std::vector<Block>> blocks;
    Block& block(std::string_view name, std::size_t index = 0);
    const Block& block(std::string_view name, std::size_t index = 0) const;
    bool valid_ids(const framework::Uuid& session, const framework::Uuid& agent) const;
};
std::span<const PacketSpec> packet_catalog();
const PacketSpec& packet_spec(std::string_view name);
const PacketSpec& packet_spec(Frequency frequency, std::uint16_t id);
Block make_block(const BlockSpec& schema);
Packet make_packet(std::string_view name);
// ToBytes equivalent: the zerocoded flag may be set, but the body is raw.
std::vector<std::uint8_t> encode_packet(const Packet& packet);
std::size_t packet_length(const Packet& packet);
// Original ToBytesMultiple behavior: split variable block groups before zero
// coding, carry ACKs only on the first fragment, and update AppendedAcks.
std::vector<std::vector<std::uint8_t>> encode_packets(Packet& packet, std::size_t mtu = 1200);
// Wire representation: zero-compress when the header requests it.
std::vector<std::uint8_t> encode_datagram(const Packet& packet);
class BaselineDecodeError final : public std::runtime_error {
  public:
    BaselineDecodeError()
        : std::runtime_error("Baseline fixed-block decoder leaves array elements uninitialized") {}
};
// The original fresh-packet decoder throws for fixed multiple-block messages.
// Preserve that observable behavior by default; migration diagnostics can opt
// into wire decoding to inspect those otherwise valid layouts.
Packet decode_datagram(std::span<const std::uint8_t> bytes, std::size_t maximum_decoded = 65536,
                       bool baseline_quirks = true);
std::vector<std::uint8_t> zero_encode(std::span<const std::uint8_t> bytes,
                                      bool preserve_appended_acks = true);
std::vector<std::uint8_t> zero_decode(std::span<const std::uint8_t> bytes,
                                      std::size_t maximum_decoded = 65536,
                                      bool preserve_appended_acks = true);
}
