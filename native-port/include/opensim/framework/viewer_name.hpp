// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/agent_circuit.hpp"
#include <string_view>
namespace opensim::framework {
bool viewer_char_is_letter(char16_t value);
std::string viewer_name(const AgentCircuitData* circuit);
inline std::string viewer_name(const AgentCircuitData& circuit){return viewer_name(&circuit);}
std::u16string lower_invariant_culture(std::u16string_view text);
std::string lower_invariant_culture(std::string_view utf8_text);
}
