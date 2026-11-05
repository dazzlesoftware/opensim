// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace opensim::framework {
// Original CommandConsole.Parser.Parse, preserving UTF16 code units, quote
// segments and --option="value" reconstruction, including unmatched quotes.
std::vector<std::u16string> parse_console_command(std::optional<std::u16string_view>);
// UTF8 console boundary. Invalid input bytes use the existing replacement
// decoder; the UTF16 parser itself never normalizes or repairs code units.
std::vector<std::string> parse_console_command_utf8(std::string_view);
}
