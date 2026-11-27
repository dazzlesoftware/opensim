// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace opensim::framework {
// Original SDK osUTF8 backing conversion. This is not replacement UTF-8 encoding.
std::vector<std::uint8_t> sdk_utf8_no_term(std::u16string_view input,
                                           std::optional<std::size_t> maximum_bytes = std::nullopt);
} // namespace opensim::framework
