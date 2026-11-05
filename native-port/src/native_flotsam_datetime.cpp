// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/native_flotsam_datetime.hpp"
#include "opensim/framework/culture_prefix.hpp"
namespace opensim::services {
void bind_native_invariant_flotsam_datetime(
    FlotsamConsoleBackend& backend, std::shared_ptr<framework::NativeInvariantDateTimeContext> owner) {
    if (!owner)
        owner = framework::shared_native_invariant_datetime_context();
    struct Binding {
        std::shared_ptr<framework::NativeInvariantDateTimeContext> owner;
        framework::InvariantDateTimeParseContext parse;
    };
    auto binding = std::make_shared<Binding>(Binding{owner, owner->callbacks()});
    backend.now = [binding] { return binding->owner->now(); };
    backend.try_parse = [binding](const FlotsamConsoleBackend::Text& text, framework::AssetDateTime& output) {
        return framework::try_parse_invariant_datetime_utf8(text, output, binding->parse);
    };
    backend.equals_now = [](const FlotsamConsoleBackend::Text& text) {
        return text && framework::invariant_equals_ignore_case(*text, "now");
    };
}
}
