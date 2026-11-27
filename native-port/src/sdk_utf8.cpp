// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/sdk_utf8.hpp"
#include <limits>
#include <stdexcept>

namespace opensim::framework {
namespace {
bool low(char16_t unit) {
    return unit >= 0xdc00 && unit <= 0xdfff;
}

bool high(char16_t unit) {
    return unit >= 0xd800 && unit <= 0xdbff;
}
} // namespace

std::vector<std::uint8_t> sdk_utf8_no_term(std::u16string_view input, std::optional<std::size_t> maximum_bytes) {
    if (input.empty())
        return {};
    std::size_t consumed = 0, count = 0;
    char16_t last = 0;
    while (consumed < input.size() && (!maximum_bytes || count < *maximum_bytes)) {
        auto unit = input[consumed++];
        if (maximum_bytes && unit == 0)
            break;
        std::size_t bytes;
        if (unit <= 0x7f)
            bytes = 1;
        else if (unit < 0x800)
            bytes = 2;
        else if (low(unit))
            continue;
        else if (high(unit)) {
            if (consumed == input.size())
                break;
            unit = input[consumed++];
            if (!low(unit))
                continue;
            bytes = 4;
        } else
            bytes = 3;
        if (maximum_bytes && bytes > *maximum_bytes - count)
            break;
        if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) - bytes)
            throw std::overflow_error("OverflowException: SDK UTF-8 byte count overflow");
        count += bytes;
        last = unit;
    }
    // The unbounded SDK counter drops one byte/input unit when its last
    // accepted character is NUL, even if all input characters were skipped.
    if (!maximum_bytes && consumed && last == 0) {
        if (!count)
            throw std::overflow_error("OverflowException: Arithmetic operation resulted in an overflow.");
        --count;
        --consumed;
    }
    std::vector<std::uint8_t> output(count, 0);
    std::size_t source = 0, destination = 0;
    while (source < consumed && destination < count) {
        const auto unit = input[source++];
        if (!unit)
            break;
        std::uint32_t scalar = unit;
        std::size_t bytes = unit <= 0x7f ? 1 : unit < 0x800 ? 2 : 3;
        if (low(unit))
            continue;
        if (high(unit)) {
            // Original emitter checks four-byte capacity before consuming even
            // an invalid follower; zero padding can therefore remain untouched.
            if (count - destination < 4)
                break;
            if (source >= consumed)
                break;
            const auto next = input[source++];
            if (!low(next))
                continue;
            scalar = 0x10000 + ((static_cast<std::uint32_t>(unit) - 0xd800) << 10) + (next - 0xdc00);
            bytes = 4;
        }
        if (bytes > count - destination)
            break;
        if (bytes == 1)
            output[destination++] = static_cast<std::uint8_t>(scalar);
        else {
            const auto shift = static_cast<unsigned>((bytes - 1) * 6);
            const auto prefix = bytes == 2 ? 0xc0u : bytes == 3 ? 0xe0u : 0xf0u;
            output[destination++] = static_cast<std::uint8_t>(prefix | (scalar >> shift));
            for (unsigned remaining = shift; remaining; remaining -= 6)
                output[destination++] = static_cast<std::uint8_t>(0x80u | ((scalar >> (remaining - 6)) & 0x3fu));
        }
    }
    // Original StringToBytesNoTerm ignores the emitted length. Preserve its
    // zero-initialized padding rather than shrinking the backing array.
    return output;
}
} // namespace opensim::framework
