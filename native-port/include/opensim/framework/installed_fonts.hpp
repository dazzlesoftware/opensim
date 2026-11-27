// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace opensim::framework {
// Windows follows the calling thread's UI language; the Unix simulator's
// invariant UI culture uses LCID/LANGID 127. This does not change the process
// locale and remains inspectable independently of enumeration.
std::uint16_t installed_font_ui_language();
// Installed GDI+ family order, with names selected by the caller's UI language
// (LANGID, not the numeric-format culture). Backend failures throw; an absent
// provider is never reported as an empty collection.
std::vector<std::string> installed_font_families(std::uint16_t ui_language_id);
} // namespace opensim::framework
