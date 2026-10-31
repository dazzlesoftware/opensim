// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
namespace opensim::framework {
// Exact Util.FieldToString wire-field conversion: printable fields drop their
// final byte and stop at NUL/CR/LF; nonprintable fields use the source hex dump.
std::string field_to_string(std::span<const std::uint8_t>,std::optional<std::string_view> field_name=std::string_view{});
}
