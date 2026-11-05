// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/viewer_name.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/viewer_unicode_data.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::framework {
bool viewer_char_is_letter(char16_t value) {
    auto found = std::lower_bound(viewer_unicode::letters.begin(), viewer_unicode::letters.end(),
                                  static_cast<std::uint32_t>(value),
                                  [](const auto& range, std::uint32_t scalar) { return range[1] < scalar; });
    return found != viewer_unicode::letters.end() && (*found)[0] <= value;
}
std::string viewer_name(const AgentCircuitData* circuit) {
    if (!circuit)
        throw std::invalid_argument("Null agent circuit");
    auto raw = circuit->viewer();
    auto name = raw ? trim_utf8(*raw) : std::string{};
    for (char16_t c : utf16_replacing(name))
        if (viewer_char_is_letter(c))
            return name;
    if (circuit->channel && !circuit->channel->empty())
        name = trim_utf8(*circuit->channel) + " " + name;
    return name;
}
}
