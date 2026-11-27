// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/yengine_object_code.hpp"

namespace opensim::script {
enum class YEngineSavedStorage { Argument, Local };
enum class YEngineSavedRole { Parameter, Local, Temporary, Instance, Heap, CallNumber };

struct YEngineSavedSlot {
    std::size_t index = 0;
    YEngineSavedStorage storage = YEngineSavedStorage::Local;
    std::int32_t number = 0;
    std::string name, type;
    YEngineSavedRole role = YEngineSavedRole::Local;
    // Actual stores in the generated body, excluding the restore prologue.
    std::vector<YEngineObjectLocation> definitions;
    std::vector<std::size_t> definition_records;
};

enum class YEngineContinuationKind { InternalCall, ExternalCall, EntryCheck, BudgetCheck };

struct YEngineCallContinuation {
    std::int32_t call_no = 0, label_number = 0;
    std::size_t label_record = 0, resume_record = 0;
    std::string label, callee;
    YEngineObjectLocation location;
    YEngineContinuationKind kind = YEngineContinuationKind::ExternalCall;
};

struct YEngineMethodContinuation {
    bool supported = false;
    std::string error;
    std::size_t slot_count = 0;
    std::vector<YEngineSavedSlot> slots;
    std::vector<std::size_t> unused_slots;
    std::map<std::int32_t, YEngineCallContinuation> calls;
};

struct YEngineContinuationMetadata {
    std::int32_t compiled_version = 0;
    std::string source_hash;
    std::map<std::string, YEngineMethodContinuation> methods;
};

// Decodes the compiler's CaptureStackFrame/RestoreStackFrame contract. An
// unsupported method has an explicit error and supplies no partial mappings.
YEngineContinuationMetadata analyze_yengine_continuations(const YEngineObjectCode &);
// Enable only independently validated generated integer[] class indices. The
// analyzer rechecks their shape and keeps generated helper frames unsupported.
YEngineContinuationMetadata
analyze_yengine_continuations(const YEngineObjectCode &,
                              std::span<const std::int32_t> validated_generated_class_indices);
} // namespace opensim::script
