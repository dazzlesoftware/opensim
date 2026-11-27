// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace opensim::framework {
std::string format_invariant_integer(std::int32_t value, std::string_view format);
std::string format_invariant_double(double value, std::string_view format);
std::u16string format_invariant_integer_utf16(std::int32_t value, std::u16string_view format);
std::u16string format_invariant_double_utf16(double value, std::u16string_view format);
} // namespace opensim::framework
