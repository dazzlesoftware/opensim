// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <span>
#include <string>
#include <string_view>

namespace opensim::script {
// LSL boxed values do not implement IFormattable. Their ToString result is
// inserted verbatim even when a composite-format specifier is supplied.
std::string ossl_format_string(std::string_view format, std::span<const std::string> arguments);
std::u16string ossl_format_string_utf16(std::u16string_view format, std::span<const std::u16string> arguments);
} // namespace opensim::script
