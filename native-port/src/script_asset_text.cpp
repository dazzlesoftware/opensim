// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/asset_text.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/integer_text.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include <array>
#include "opensim/services/user_account.hpp"
#include <stdexcept>
namespace opensim::script {
std::string asset_text(std::span<const std::uint8_t> bytes) {
    return framework::utf8(framework::sdk_bytes_to_string(bytes));
}
std::string asset_text(const framework::AssetBytes& bytes) {
    if (!bytes)
        throw std::invalid_argument("Null script asset data");
    return asset_text(std::span<const std::uint8_t>(*bytes));
}

std::vector<std::string> notecard_lines(const framework::AssetBytes& bytes) {
    if (!bytes || bytes->size() < 79 || (*bytes)[58] != '0' || (*bytes)[59] != '\n')
        return {};
    const std::string_view raw(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    const auto header = framework::utf16_replacing(raw);
    if (header.size() < 60 || header.substr(0, 21) != u"Linden text version 2" ||
        header.substr(24, 25) != u"LLEmbeddedItems version 1" || header.substr(52, 5) != u"count")
        return {};
    const auto label = header.find(u"Text length", 60);
    if (label == std::u16string::npos || header.size() - label <= 12)
        return {};
    const auto length_start = label + 12;
    const auto newline = header.find(u'\n', length_start + 1);
    std::int32_t length = 0;
    if (newline == std::u16string::npos ||
        !services::parse_account_integer(framework::utf8(header.substr(length_start, newline - length_start)),
                                         length) ||
        length <= 0)
        return {};
    // The original uses the decoded header offset as a byte offset and the stored length in bytes.
    const auto start = newline + 1;
    if (start > raw.size() || static_cast<std::size_t>(length) > raw.size() - start)
        return {};
    const auto text = framework::utf8(framework::utf16_replacing(raw.substr(start, length)));
    std::vector<std::string> lines;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto end = text.find('\n', offset);
        lines.push_back(text.substr(offset, end - offset));
        if (end == std::string::npos)
            break;
        offset = end + 1;
    }
    return lines;
}

framework::Vector3d landmark_position_relative_to(const framework::AssetBytes& bytes,
                                                 std::uint32_t region_x, std::uint32_t region_y) {
    if (!bytes || bytes->empty())
        throw std::invalid_argument("Empty landmark asset");
    const std::string_view raw(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    const auto text = framework::trim_utf8(framework::utf8(framework::utf16_replacing(raw)));
    std::string_view remaining = text;
    std::array<std::string_view, 4> fields;
    for (auto& field : fields) {
        const auto end = remaining.find('\n');
        field = remaining.substr(0, end);
        remaining = end == std::string_view::npos ? std::string_view{} : remaining.substr(end + 1);
    }
    if (fields[0].size() < 18 || fields[1].size() < 46 || fields[2].size() < 10 || fields[3].size() < 14)
        throw std::invalid_argument("Incomplete landmark asset");

    std::array<float, 3> local{};
    auto scalars = fields[2].substr(10);
    for (auto& component : local) {
        const auto end = scalars.find(' ');
        try {
            component = framework::invariant_convert_to_single(framework::utf16_replacing(scalars.substr(0, end)));
        } catch (const framework::InvariantNumberError&) {
            // Single.TryParse leaves invalid components at zero.
        }
        if (end == std::string_view::npos)
            break;
        scalars.remove_prefix(end + 1);
    }
    std::uint64_t handle = 0;
    framework::try_parse_unsigned_integer(fields[3].substr(14), handle);
    return {double(handle >> 32) - region_x + local[0],
            double(static_cast<std::uint32_t>(handle)) - region_y + local[1], local[2]};
}
}
