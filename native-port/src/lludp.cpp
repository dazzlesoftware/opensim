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
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
namespace opensim::network::lludp {
namespace {
#include "lludp_schema.inc"
constexpr std::size_t MaxEncoded = 16 * 1024 * 1024;
std::size_t ack_start(std::span<const std::uint8_t> data, bool preserve) {
    if (data.size() < 6)
        throw std::invalid_argument("LLUDP packet header is truncated");
    if (!preserve || (data[0] & 0x10) == 0)
        return data.size();
    const std::size_t length = static_cast<std::size_t>(data.back()) * 4 + 1;
    if (length > data.size() - 6)
        throw std::invalid_argument("LLUDP appended ACK list is truncated");
    return data.size() - length;
}
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t position = 0;
    std::span<const std::uint8_t> take(std::size_t count) {
        if (position > bytes.size() || count > bytes.size() - position)
            throw std::invalid_argument("Truncated LLUDP field");
        auto result = bytes.subspan(position, count);
        position += count;
        return result;
    }
    std::uint64_t integer(std::size_t count, bool big = false) {
        auto data = take(count);
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < count; ++i)
            value |= static_cast<std::uint64_t>(data[i]) << ((big ? count - 1 - i : i) * 8);
        return value;
    }
    float f32() {
        return std::bit_cast<float>(static_cast<std::uint32_t>(integer(4)));
    }
    double f64() {
        return std::bit_cast<double>(integer(8));
    }
};
struct Writer {
    std::vector<std::uint8_t> bytes;
    void append(std::span<const std::uint8_t> data) {
        if (data.size() > MaxEncoded - bytes.size())
            throw std::length_error("LLUDP packet exceeds codec capacity");
        bytes.insert(bytes.end(), data.begin(), data.end());
    }
    void integer(std::uint64_t value, std::size_t count, bool big = false) {
        if (count > MaxEncoded - bytes.size())
            throw std::length_error("LLUDP packet exceeds codec capacity");
        for (std::size_t i = 0; i < count; ++i)
            bytes.push_back(static_cast<std::uint8_t>(value >> ((big ? count - 1 - i : i) * 8)));
    }
    void f32(float value) {
        integer(std::bit_cast<std::uint32_t>(value), 4);
    }
    void f64(double value) {
        integer(std::bit_cast<std::uint64_t>(value), 8);
    }
};
std::size_t width(FieldType type) {
    switch (type) {
    case FieldType::U8:
    case FieldType::S8:
    case FieldType::BOOL:
        return 1;
    case FieldType::U16:
    case FieldType::S16:
    case FieldType::IPPORT:
        return 2;
    case FieldType::U32:
    case FieldType::S32:
    case FieldType::IPADDR:
    case FieldType::F32:
        return 4;
    case FieldType::U64:
    case FieldType::S64:
    case FieldType::F64:
        return 8;
    default:
        return 0;
    }
}
FieldValue decode_field(Reader& reader, const FieldSpec& field) {
    switch (field.type) {
    case FieldType::S8:
        return static_cast<std::int64_t>(
            std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(reader.integer(1))));
    case FieldType::S16:
        return static_cast<std::int64_t>(
            std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(reader.integer(2))));
    case FieldType::S32:
        return static_cast<std::int64_t>(
            std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(reader.integer(4))));
    case FieldType::S64:
        return std::bit_cast<std::int64_t>(reader.integer(8));
    case FieldType::U8:
    case FieldType::U16:
    case FieldType::U32:
    case FieldType::U64:
    case FieldType::IPADDR:
        return reader.integer(width(field.type));
    case FieldType::IPPORT:
        return reader.integer(2, true);
    case FieldType::BOOL:
        return reader.integer(1) != 0;
    case FieldType::F32:
        return static_cast<double>(reader.f32());
    case FieldType::F64:
        return reader.f64();
    case FieldType::LLUUID:
        return framework::Uuid::from_bytes(reader.take(16));
    case FieldType::LLVector3:
        return std::vector<double>{reader.f32(), reader.f32(), reader.f32()};
    case FieldType::LLVector3d:
        return std::vector<double>{reader.f64(), reader.f64(), reader.f64()};
    case FieldType::LLVector4:
        return std::vector<double>{reader.f32(), reader.f32(), reader.f32(), reader.f32()};
    case FieldType::LLQuaternion: {
        const float x = reader.f32(), y = reader.f32(), z = reader.f32();
        const float w_squared = 1.f - x * x - y * y - z * z;
        return std::vector<double>{x, y, z, w_squared > 1e-6f ? std::sqrt(w_squared) : 0.f};
    }
    case FieldType::Variable:
    case FieldType::Fixed: {
        const auto length = field.type == FieldType::Fixed
                                ? field.size
                                : static_cast<std::size_t>(reader.integer(field.size));
        auto data = reader.take(length);
        return std::vector<std::uint8_t>(data.begin(), data.end());
    }
    }
    throw std::invalid_argument("Unknown LLUDP field type");
}
// The SDK profile packet classes cast their variable byte counts to the prefix
// width while copying the complete payload, including overlong charter/interest/email data.
bool profile_prefix_wrap(const Packet& packet, const FieldSpec& field) {
    return (packet.spec->name == "AvatarPropertiesReply" && field.name == "CharterMember") ||
           packet.spec->name == "AvatarInterestsReply" ||
           (packet.spec->name == "UserInfoReply" && field.name == "EMail") ||
           (packet.spec->name == "AvatarClassifiedReply" && field.name == "Name") ||
           packet.spec->name == "ClassifiedInfoReply" ||
           (packet.spec->name == "PickInfoReply" && field.name == "SimName");
}
void encode_field(Writer& writer, const FieldSpec& field, const FieldValue& value, bool prefix_wrap) {
    switch (field.type) {
    case FieldType::S8:
    case FieldType::S16:
    case FieldType::S32:
    case FieldType::S64: {
        const auto number = std::get<std::int64_t>(value);
        const auto bytes = width(field.type);
        if (bytes < 8) {
            const auto limit = std::int64_t{1} << (bytes * 8 - 1);
            if (number < -limit || number >= limit)
                throw std::out_of_range("LLUDP signed field range");
        }
        writer.integer(static_cast<std::uint64_t>(number), bytes);
        return;
    }
    case FieldType::U8:
    case FieldType::U16:
    case FieldType::U32:
    case FieldType::U64:
    case FieldType::IPADDR:
    case FieldType::IPPORT: {
        const auto number = std::get<std::uint64_t>(value);
        const auto bytes = width(field.type);
        if (bytes < 8 && number >= (std::uint64_t{1} << (bytes * 8)))
            throw std::out_of_range("LLUDP unsigned field range");
        writer.integer(number, bytes, field.type == FieldType::IPPORT);
        return;
    }
    case FieldType::BOOL:
        writer.integer(std::get<bool>(value) ? 1 : 0, 1);
        return;
    case FieldType::F32:
        writer.f32(static_cast<float>(std::get<double>(value)));
        return;
    case FieldType::F64:
        writer.f64(std::get<double>(value));
        return;
    case FieldType::LLUUID:
        writer.append(std::get<framework::Uuid>(value).bytes());
        return;
    case FieldType::LLVector3:
    case FieldType::LLVector3d:
    case FieldType::LLVector4:
    case FieldType::LLQuaternion: {
        const auto& vector = std::get<std::vector<double>>(value);
        const std::size_t count =
            (field.type == FieldType::LLVector4 || field.type == FieldType::LLQuaternion) ? 4 : 3;
        if (vector.size() != count)
            throw std::invalid_argument("LLUDP vector component count");
        if (field.type == FieldType::LLQuaternion) {
            const float x = static_cast<float>(vector[0]), y = static_cast<float>(vector[1]),
                        z = static_cast<float>(vector[2]), w = static_cast<float>(vector[3]);
            const float length = (x * x + y * y) + (z * z + w * w);
            if (std::abs(w) > 0.9999f || !(length > 1e-6f)) {
                writer.integer(0, 8);
                writer.integer(0, 4);
            } else {
                const float scale = (w < 0 ? -1.f : 1.f) / std::sqrt(length);
                writer.f32(scale * x);
                writer.f32(scale * y);
                writer.f32(scale * z);
            }
        } else
            for (auto number : vector) {
                if (field.type == FieldType::LLVector3d)
                    writer.f64(number);
                else
                    writer.f32(static_cast<float>(number));
            }
        return;
    }
    case FieldType::Fixed:
    case FieldType::Variable: {
        const auto& data = std::get<std::vector<std::uint8_t>>(value);
        if (field.type == FieldType::Fixed) {
            if (data.size() != field.size)
                throw std::invalid_argument("LLUDP fixed byte field length");
        } else {
            if ((field.size != 1 && field.size != 2) ||
                (!prefix_wrap && data.size() >= (std::size_t{1} << (field.size * 8))))
                throw std::out_of_range("LLUDP variable byte field length");
            writer.integer(data.size(), field.size);
        }
        writer.append(data);
        return;
    }
    }
}
}
std::span<const PacketSpec> packet_catalog() {
    return schemas;
}
const PacketSpec& packet_spec(std::string_view name) {
    static const auto index = [] {
        std::unordered_map<std::string_view, const PacketSpec*> result;
        for (const auto& spec : schemas)
            result[spec.name] = &spec;
        return result;
    }();
    const auto found = index.find(name);
    if (found == index.end())
        throw std::invalid_argument("Unknown LLUDP message");
    return *found->second;
}
const PacketSpec& packet_spec(Frequency frequency, std::uint16_t id) {
    static const auto index = [] {
        std::unordered_map<std::uint32_t, const PacketSpec*> result;
        for (const auto& spec : schemas)
            result[(static_cast<std::uint32_t>(spec.frequency) << 16) | spec.id] = &spec;
        return result;
    }();
    const auto found = index.find((static_cast<std::uint32_t>(frequency) << 16) | id);
    if (found == index.end())
        throw std::invalid_argument("Unknown LLUDP message identifier");
    return *found->second;
}
Block make_block(const BlockSpec& schema) {
    Block result;
    for (const auto& field : schema.fields) {
        FieldValue value = std::uint64_t{0};
        switch (field.type) {
        case FieldType::S8:
        case FieldType::S16:
        case FieldType::S32:
        case FieldType::S64:
            value = std::int64_t{0};
            break;
        case FieldType::BOOL:
            value = false;
            break;
        case FieldType::F32:
        case FieldType::F64:
            value = 0.0;
            break;
        case FieldType::LLUUID:
            value = framework::Uuid{};
            break;
        case FieldType::LLVector3:
        case FieldType::LLVector3d:
            value = std::vector<double>(3);
            break;
        case FieldType::LLVector4:
        case FieldType::LLQuaternion:
            value = std::vector<double>(4);
            break;
        case FieldType::Fixed:
            value = std::vector<std::uint8_t>(field.size);
            break;
        case FieldType::Variable:
            value = std::vector<std::uint8_t>{};
            break;
        default:
            break;
        }
        result[field.name] = std::move(value);
    }
    return result;
}
Packet make_packet(std::string_view name) {
    Packet packet;
    packet.spec = &packet_spec(name);
    packet.reliable = packet.spec->reliable;
    packet.zerocoded = packet.spec->zerocoded;
    for (const auto& block : packet.spec->blocks) {
        auto& values = packet.blocks[block.name];
        for (std::size_t i = 0; i < block.count; ++i)
            values.push_back(make_block(block));
    }
    return packet;
}
Block& Packet::block(std::string_view name, std::size_t index) {
    return blocks.at(std::string(name)).at(index);
}
const Block& Packet::block(std::string_view name, std::size_t index) const {
    return blocks.at(std::string(name)).at(index);
}
bool Packet::valid_ids(const framework::Uuid& session, const framework::Uuid& agent) const {
    if (!spec || !spec->validate_ids)
        return true;
    try {
        const auto& data = block("AgentData");
        return std::get<framework::Uuid>(data.at("SessionID")) == session &&
               std::get<framework::Uuid>(data.at("AgentID")) == agent;
    } catch (const std::exception&) {
        return false;
    }
}
std::vector<std::uint8_t> zero_encode(std::span<const std::uint8_t> input, bool preserve_acks) {
    const auto end = ack_start(input, preserve_acks);
    Writer writer;
    writer.append(input.first(6));
    std::uint8_t zeros = 0;
    for (std::size_t i = 6; i < end; ++i) {
        if (input[i] == 0) {
            if (zeros == 255) {
                writer.integer(0, 1);
                writer.integer(255, 1);
                zeros = 1;
            } else
                ++zeros;
        } else {
            if (zeros) {
                writer.integer(0, 1);
                writer.integer(zeros, 1);
                zeros = 0;
            }
            writer.integer(input[i], 1);
        }
    }
    if (zeros) {
        writer.integer(0, 1);
        writer.integer(zeros, 1);
    }
    writer.append(input.subspan(end));
    return writer.bytes;
}
std::vector<std::uint8_t> zero_decode(std::span<const std::uint8_t> input, std::size_t limit,
                                      bool preserve_acks) {
    const auto end = ack_start(input, preserve_acks);
    if (limit < 6)
        throw std::length_error("LLUDP decode limit smaller than header");
    std::vector<std::uint8_t> output(input.begin(), input.begin() + 6);
    for (std::size_t i = 6; i < end; ++i) {
        if (input[i] == 0) {
            if (++i == end)
                throw std::invalid_argument("Truncated LLUDP zero run");
            if (input[i] > limit - output.size())
                throw std::length_error("LLUDP zero decode expansion limit");
            output.insert(output.end(), input[i], 0);
        } else {
            if (output.size() == limit)
                throw std::length_error("LLUDP zero decode expansion limit");
            output.push_back(input[i]);
        }
    }
    if (input.size() - end > limit - output.size())
        throw std::length_error("LLUDP ACK decode expansion limit");
    output.insert(output.end(), input.begin() + end, input.end());
    return output;
}
std::vector<std::uint8_t> encode_packet(const Packet& packet) {
    if (!packet.spec)
        throw std::invalid_argument("LLUDP packet has no schema");
    if (packet.extra_header.size() > 255 || packet.acknowledgements.size() > 255)
        throw std::out_of_range("LLUDP header collection limit");
    Writer writer;
    const auto flags = (packet.reliable ? 0x40 : 0) | (packet.resent ? 0x20 : 0) |
                       (packet.zerocoded ? 0x80 : 0) | (packet.appended_acks ? 0x10 : 0);
    writer.integer(flags, 1);
    writer.integer(packet.sequence, 4, true);
    writer.integer(packet.extra_header.size(), 1);
    writer.append(packet.extra_header);
    if (packet.spec->frequency == Frequency::High)
        writer.integer(packet.spec->id, 1);
    else if (packet.spec->frequency == Frequency::Medium) {
        writer.integer(255, 1);
        writer.integer(packet.spec->id, 1);
    } else {
        writer.integer(65535, 2);
        writer.integer(packet.spec->id, 2, true);
    }
    for (const auto& block : packet.spec->blocks) {
        const auto& values = packet.blocks.at(block.name);
        if (block.variable) {
            writer.integer(static_cast<std::uint8_t>(values.size()), 1);
        } else if (values.size() != block.count)
            throw std::invalid_argument("LLUDP fixed block count");
        for (const auto& value : values)
            for (const auto& field : block.fields)
                encode_field(writer, field, value.at(field.name), profile_prefix_wrap(packet, field));
    }
    if (!packet.acknowledgements.empty()) {
        for (auto ack : packet.acknowledgements)
            writer.integer(ack, 4, true);
        writer.integer(packet.acknowledgements.size(), 1);
    }
    return writer.bytes;
}
std::vector<std::uint8_t> encode_datagram(const Packet& packet) {
    auto bytes = encode_packet(packet);
    return packet.zerocoded ? zero_encode(bytes) : bytes;
}
std::size_t packet_length(const Packet& packet) {
    if (!packet.spec)
        throw std::invalid_argument("LLUDP packet has no schema");
    std::size_t result = (packet.spec->frequency == Frequency::High     ? 7
                          : packet.spec->frequency == Frequency::Medium ? 8
                                                                        : 10) +
                         packet.extra_header.size();
    for (const auto& block : packet.spec->blocks) {
        if (block.variable)
            ++result;
        for (const auto& value : packet.blocks.at(block.name)) {
            Writer row;
            for (const auto& field : block.fields)
                encode_field(row, field, value.at(field.name), profile_prefix_wrap(packet, field));
            result += row.bytes.size();
        }
    }
    return result;
}
std::vector<std::vector<std::uint8_t>> encode_packets(Packet& packet, std::size_t mtu) {
    if (!packet.spec)
        throw std::invalid_argument("LLUDP packet has no schema");
    // These hand-written baseline overrides deliberately bypass generated
    // splitting (their fixed/variable blocks are interleaved on the wire).
    if (packet.spec->name == "AgentSetAppearance" || packet.spec->name == "SimStats" ||
        packet.spec->name == "AvatarGroupsReply" || packet.spec->name == "CoarseLocationUpdate")
        return {encode_packet(packet)};
    if (std::none_of(packet.spec->blocks.begin(), packet.spec->blocks.end(),
                     [](const auto& block) { return block.variable; }))
        return {encode_packet(packet)};
    if (packet.extra_header.size() > 255 || packet.acknowledgements.size() > 255)
        throw std::out_of_range("LLUDP header collection limit");
    if (!packet.acknowledgements.empty())
        packet.appended_acks = true;
    // ToBytesMultiple serializes fixed blocks into a repeated prefix, then
    // consumes each variable group independently in template order.
    auto fixed = packet;
    fixed.acknowledgements.clear();
    fixed.blocks.clear();
    PacketSpec fixed_spec = *packet.spec;
    fixed_spec.blocks.clear();
    std::vector<const BlockSpec*> variable;
    for (const auto& block : packet.spec->blocks) {
        if (block.variable)
            variable.push_back(&block);
        else {
            fixed_spec.blocks.push_back(block);
            fixed.blocks[block.name] = packet.blocks.at(block.name);
        }
    }
    fixed.spec = &fixed_spec;
    const auto prefix = encode_packet(fixed);
    std::vector<std::vector<std::vector<std::uint8_t>>> groups;
    for (const auto* block : variable) {
        auto& rows = groups.emplace_back();
        for (const auto& value : packet.blocks.at(block->name)) {
            Writer row;
            for (const auto& field : block->fields)
                encode_field(row, field, value.at(field.name), profile_prefix_wrap(packet, field));
            rows.push_back(std::move(row.bytes));
        }
    }
    Writer ack;
    for (auto number : packet.acknowledgements)
        ack.integer(number, 4, true);
    if (!packet.acknowledgements.empty())
        ack.integer(packet.acknowledgements.size(), 1);
    std::vector<std::size_t> starts(groups.size());
    std::vector<std::vector<std::uint8_t>> result;
    const auto fixed_length = prefix.size() + groups.size();
    bool remaining;
    do {
        std::vector<std::size_t> counts(groups.size());
        std::size_t variable_length = 0, progress = 0;
        for (std::size_t g = 0; g < groups.size(); ++g) {
            for (auto i = starts[g];
                 fixed_length + variable_length + ack.bytes.size() < mtu && i < groups[g].size(); ++i) {
                const auto size = groups[g][i].size();
                if (fixed_length + variable_length + size + ack.bytes.size() <= mtu || i == starts[g]) {
                    variable_length += size;
                    ++counts[g];
                    ++progress;
                } else
                    break;
            }
        }
        Writer output;
        output.append(prefix);
        if (!result.empty())
            output.bytes[0] &= static_cast<std::uint8_t>(~0x10);
        remaining = false;
        for (std::size_t g = 0; g < groups.size(); ++g) {
            output.integer(static_cast<std::uint8_t>(counts[g]), 1);
            for (std::size_t i = 0; i < counts[g]; ++i)
                output.append(groups[g][starts[g]++]);
            remaining |= starts[g] < groups[g].size();
        }
        output.append(ack.bytes);
        ack.bytes.clear();
        result.push_back(std::move(output.bytes));
        // The managed generator loops forever if the repeated prefix alone
        // fills the MTU. Native callers get an explicit failure instead.
        if (remaining && progress == 0)
            throw std::length_error("Baseline LLUDP splitter cannot make progress with this fixed prefix");
    } while (remaining);
    return result;
}
Packet decode_datagram(std::span<const std::uint8_t> input, std::size_t limit, bool baseline_quirks) {
    if (input.size() > limit)
        throw std::length_error("LLUDP datagram length limit");
    ack_start(input, true);
    std::vector<std::uint8_t> storage;
    if (input[0] & 0x80) {
        storage = zero_decode(input, limit);
        input = storage;
    }
    const auto end = ack_start(input, true);
    Reader reader{input.first(end)};
    Packet packet;
    const auto flags = reader.integer(1);
    packet.reliable = (flags & 0x40) != 0;
    packet.resent = (flags & 0x20) != 0;
    packet.zerocoded = (flags & 0x80) != 0;
    packet.appended_acks = (flags & 0x10) != 0;
    packet.sequence = static_cast<std::uint32_t>(reader.integer(4, true));
    const auto extra = reader.take(static_cast<std::size_t>(reader.integer(1)));
    packet.extra_header.assign(extra.begin(), extra.end());
    auto id = reader.integer(1);
    Frequency frequency = Frequency::High;
    if (id == 255) {
        id = reader.integer(1);
        frequency = Frequency::Medium;
        if (id == 255) {
            id = reader.integer(2, true);
            frequency = Frequency::Low;
        }
    }
    packet.spec = &packet_spec(frequency, static_cast<std::uint16_t>(id));
    if (baseline_quirks && std::any_of(packet.spec->blocks.begin(), packet.spec->blocks.end(),
                                       [](const auto& block) { return !block.variable && block.count > 1; }))
        throw BaselineDecodeError();
    if (packet.appended_acks) {
        Reader acks{input.subspan(end, input.size() - end - 1)};
        while (acks.position < acks.bytes.size())
            packet.acknowledgements.push_back(static_cast<std::uint32_t>(acks.integer(4, true)));
        // Header.BuildHeader in the baseline reads trailers backwards.
        std::reverse(packet.acknowledgements.begin(), packet.acknowledgements.end());
    }
    for (const auto& block : packet.spec->blocks) {
        const auto count = block.variable ? static_cast<std::size_t>(reader.integer(1)) : block.count;
        auto& values = packet.blocks[block.name];
        values.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            Block value;
            for (const auto& field : block.fields)
                value[field.name] = decode_field(reader, field);
            values.push_back(std::move(value));
        }
    }
    return packet;
}
}
