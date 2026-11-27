// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/lsl_runtime.hpp"

namespace opensim::script {
struct PrimitiveParameterRuleBackend {
    bool avatar = false;
    // The caller prepares its part/appearance snapshot, including the source
    // face-count lookup. This callback supplies the actual typed property values.
    std::function<void(std::int32_t, std::optional<std::int32_t>, LslList &)> append;
};

// Append values to the existing result and return a pending link-target tail.
// Unknown rules, including prim-only rules applied to avatars, are ignored as
// in the original getters; their following entries remain independent rules.
LslList read_primitive_parameter_rules(const PrimitiveParameterRuleBackend &backend, const LslList &rules,
                                       LslList &result);
} // namespace opensim::script
