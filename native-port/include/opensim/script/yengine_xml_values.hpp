// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/lsl_runtime.hpp"
#include <span>

namespace opensim::script {
// Original GetXMLObject / ExtractXMLObjectValue scalar boundary. Lists are
// reconstructed by the length-aware DOM reader, independently for each node.
LslValue parse_yengine_xml_scalar(std::string_view clr_type, std::u16string_view text);
std::vector<LslValue> normalize_yengine_xml_event_arguments(std::span<const LslValue>);
// Restore XML values into an Object-aware VM without inferring aliases from
// equal scalar contents. Already reference-aware values keep their identities.
std::vector<LslValue> reference_yengine_xml_event_arguments(std::span<const LslValue>);
std::u16string yengine_xml_raw_single_text(float);
} // namespace opensim::script
