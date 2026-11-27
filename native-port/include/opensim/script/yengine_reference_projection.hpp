// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/lsl_runtime.hpp"
#include <functional>
#include <map>

namespace opensim::script {
struct LslScriptClassDescriptor;
enum class YEngineReferenceType {
    Object,
    Integer,
    Float,
    String,
    Key,
    List,
    Vector,
    Rotation,
    Character,
    Boolean,
    ScriptClass
};

struct YEngineScriptClassProjectionContext {
    std::map<std::int32_t, std::shared_ptr<const LslScriptClassDescriptor>> classes;
    std::function<bool(std::int32_t)> check_heap_total;
};

// Use one projection for every global, argument and captured frame. Raw graph
// node identity, rather than equal payload values, determines live references.
// This is separate from the legacy value-only projection used by old programs.
class YEngineReferenceProjection {
    struct State;
    std::shared_ptr<State> state_;

  public:
    YEngineReferenceProjection();
    explicit YEngineReferenceProjection(YEngineScriptClassProjectionContext);
    std::optional<LslValue> project(const YEngineManagedRef &, YEngineReferenceType);
};
} // namespace opensim::script
