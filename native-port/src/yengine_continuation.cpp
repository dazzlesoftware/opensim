// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/yengine_continuation.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include <charconv>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string_view>

namespace opensim::script {
namespace {
using Kind = YEngineObjectRecordKind;
using Record = YEngineObjectRecord;
using Storage = YEngineSavedStorage;

[[noreturn]] void invalid(const char *message) {
    throw std::invalid_argument(message);
}

bool op(const Record &record, std::uint16_t value) {
    return record.opcode && static_cast<std::uint16_t>(*record.opcode) == value;
}

bool external(const Record &record, std::string_view name) {
    return record.kind == Kind::EmitExternalMethod && record.strings.size() >= 2 && record.strings[0] == name &&
           record.strings[1] == "xmrinst";
}

std::optional<std::int32_t> integer(const Record &record) {
    if (!record.opcode)
        return {};
    const auto code = static_cast<std::uint16_t>(*record.opcode);
    if (code >= 0x15 && code <= 0x1e)
        return static_cast<std::int32_t>(code) - 0x16;
    if ((code == 0x1f || code == 0x20) && record.integers.size() == 1)
        return record.integers.front();
    return {};
}

std::optional<std::pair<Storage, std::int32_t>> variable(const Record &record, bool store) {
    if (!record.opcode)
        return {};
    const auto code = static_cast<std::uint16_t>(*record.opcode);
    if (!store && code >= 0x02 && code <= 0x05)
        return std::pair{Storage::Argument, static_cast<std::int32_t>(code - 0x02)};
    if (!store && code >= 0x06 && code <= 0x09)
        return std::pair{Storage::Local, static_cast<std::int32_t>(code - 0x06)};
    if (store && code >= 0x0a && code <= 0x0d)
        return std::pair{Storage::Local, static_cast<std::int32_t>(code - 0x0a)};
    const bool argument = store ? code == 0x10 || code == 0xfe0b : code == 0x0e || code == 0xfe09;
    const bool local = store ? code == 0x13 || code == 0xfe0e : code == 0x11 || code == 0xfe0c;
    if ((argument || local) && record.integers.size() == 1 && record.integers.front() >= 0)
        return std::pair{argument ? Storage::Argument : Storage::Local, record.integers.front()};
    return {};
}

std::size_t next_op(const std::vector<Record> &records, std::size_t index) {
    while (index < records.size() && !records[index].opcode)
        ++index;
    return index;
}

std::size_t previous_op(const std::vector<Record> &records, std::size_t index) {
    while (index > 0) {
        --index;
        if (records[index].opcode)
            return index;
    }
    invalid("Missing continuation instruction");
}

std::optional<std::int32_t> call_number(std::string_view name) {
    constexpr std::string_view prefix = "__call_";
    if (!name.starts_with(prefix))
        return {};
    name.remove_prefix(prefix.size());
    const auto separator = name.find('_');
    if (separator == std::string_view::npos)
        invalid("Invalid continuation label name");
    std::int32_t number = -1, sequence = -1;
    const auto first = std::from_chars(name.data(), name.data() + separator, number);
    const auto second = std::from_chars(name.data() + separator + 1, name.data() + name.size(), sequence);
    if (first.ec != std::errc{} || first.ptr != name.data() + separator || second.ec != std::errc{} ||
        second.ptr != name.data() + name.size() || number < 0 || sequence < 0)
        invalid("Invalid continuation label number");
    return number;
}

YEngineSavedRole role(const YEngineSavedSlot &slot) {
    if (slot.type == "xmrsuper" || slot.type == "xmrinst")
        return YEngineSavedRole::Instance;
    if (slot.name == "__curHeap")
        return YEngineSavedRole::Heap;
    if (slot.name.starts_with("__") && slot.name.find("CallNo") != std::string::npos)
        return YEngineSavedRole::CallNumber;
    if (slot.name.starts_with("tmp$"))
        return YEngineSavedRole::Temporary;
    if (slot.name.starts_with("__"))
        invalid("Unsupported hidden continuation local");
    return slot.storage == Storage::Argument ? YEngineSavedRole::Parameter : YEngineSavedRole::Local;
}

YEngineMethodContinuation analyze(const std::string &name, const YEngineObjectMethod &method, bool generated_classes) {
    if (!method.has_body)
        invalid("Method has no generated body");
    YEngineMethodContinuation result;
    const auto &records = method.records;
    std::optional<std::size_t> capture, restore;
    unsigned exceptions = 0, finalizers = 0;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto &record = records[i];
        exceptions += record.kind == Kind::BeginException;
        finalizers += record.kind == Kind::BeginFinally;
        if (record.kind == Kind::BeginCatch)
            invalid("Catch continuations require exception context mapping");
        if (external(record, "CaptureStackFrame")) {
            if (capture)
                invalid("Multiple frame capture blocks");
            capture = i;
        }
        if (external(record, "RestoreStackFrame")) {
            if (restore)
                invalid("Multiple frame restore blocks");
            restore = i;
        }
    }
    for (const auto &[label, text] : method.labels) {
        const auto number = call_number(text);
        if (!number)
            continue;
        const auto marked = method.label_records.find(label);
        if (marked == method.label_records.end() || marked->second >= records.size())
            invalid("Continuation label is not marked");
        const auto resume = next_op(records, marked->second + 1);
        if (resume == records.size())
            invalid("Continuation label has no instruction");
        YEngineCallContinuation call{*number, label, marked->second, resume, text, {}, records[resume].location};
        for (std::size_t i = resume; i < records.size(); ++i) {
            const auto &record = records[i];
            if (record.kind == Kind::MarkLabel && i != marked->second)
                break;
            if (record.kind == Kind::EmitInternalMethod) {
                if (record.strings.empty())
                    invalid("Missing internal callee");
                call.callee = record.strings.at(0);
                call.kind = YEngineContinuationKind::InternalCall;
                break;
            }
            if (record.kind == Kind::EmitExternalMethod) {
                if (record.strings.size() < 2)
                    invalid("Missing external callee");
                call.callee = record.strings.at(1) + ":" + record.strings.at(0);
                if (external(record, "CheckRunStack"))
                    call.kind = YEngineContinuationKind::EntryCheck;
                else if (external(record, "CheckRunQuick") || external(record, "CheckRun"))
                    call.kind = YEngineContinuationKind::BudgetCheck;
                break;
            }
        }
        if (call.callee.empty() || !result.calls.emplace(*number, std::move(call)).second)
            invalid("Ambiguous continuation call label");
    }
    if (!capture && !restore && result.calls.empty()) {
        result.supported = true;
        return result;
    }
    if (!capture || !restore || *restore >= *capture || exceptions != 1 || finalizers != 1)
        invalid("Unsupported generated exception/frame layout");
    const auto count_record = previous_op(records, *capture);
    const auto count = integer(records[count_record]);
    if (!count || *count < 0 || *count > 65536)
        invalid("Invalid generated frame length");
    result.slot_count = static_cast<std::size_t>(*count);
    // Validate the compiler's capture function name and call-number local.
    const auto call_record = previous_op(records, count_record);
    const auto call_variable = variable(records[call_record], false);
    const auto name_record = previous_op(records, call_record);
    if (!call_variable || call_variable->first != Storage::Local || records[name_record].kind != Kind::EmitString ||
        records[name_record].strings != std::vector{name})
        invalid("Invalid frame capture identity");
    const auto restore_address = previous_op(records, *restore);
    const auto restore_name = previous_op(records, restore_address);
    if (!(op(records[restore_address], 0xfe0d) || op(records[restore_address], 0x12)) ||
        records[restore_address].integers != std::vector{call_variable->second} ||
        records[restore_name].kind != Kind::EmitString || records[restore_name].strings != std::vector{name})
        invalid("Restore identity disagrees with capture");
    for (const auto &[number, call] : result.calls) {
        const auto assignment = previous_op(records, call.label_record);
        const auto value = previous_op(records, assignment);
        if (variable(records[assignment], true) != call_variable || integer(records[value]) != number)
            invalid("Call label disagrees with generated call-number assignment");
    }
    std::set<std::size_t> captured;
    auto cursor = next_op(records, *capture + 1);
    while (cursor < records.size() && op(records[cursor], 0x25)) { // dup array
        const auto index_record = next_op(records, cursor + 1);
        if (index_record >= records.size())
            invalid("Incomplete frame capture slot");
        const auto index = integer(records[index_record]);
        const auto value_record = next_op(records, index_record + 1);
        if (!index || *index < 0 || static_cast<std::size_t>(*index) >= result.slot_count ||
            value_record >= records.size() || !captured.insert(static_cast<std::size_t>(*index)).second)
            invalid("Invalid frame capture slot index");
        const auto storage = variable(records[value_record], false);
        if (!storage)
            invalid("Unsupported frame slot expression");
        YEngineSavedSlot slot;
        slot.index = static_cast<std::size_t>(*index);
        slot.storage = storage->first;
        slot.number = storage->second;
        const YEngineObjectVariable *declaration = nullptr;
        if (slot.storage == Storage::Argument) {
            if (static_cast<std::size_t>(slot.number) < method.arguments.size())
                declaration = &method.arguments[slot.number];
        } else if (const auto local = method.locals.find(slot.number); local != method.locals.end()) {
            declaration = &local->second;
        }
        if (!declaration)
            invalid("Unknown saved argument/local");
        slot.name = declaration->name;
        slot.type = declaration->type;
        if (slot.type == "sdtclobj" && !generated_classes)
            invalid("Script-class frame storage requires validated generated class context");
        slot.role = role(slot);
        std::size_t end = next_op(records, value_record + 1);
        while (end < records.size() && !op(records[end], 0xa2)) { // stelem.ref
            const auto &conversion = records[end];
            if (slot.type == "sdtclobj" && op(conversion, 0x8c))
                invalid("Script-class reference cannot be boxed in capture storage");
            if (!(op(conversion, 0x8c) || // box
                  (conversion.kind == Kind::EmitField && op(conversion, 0x7b) && conversion.strings.size() == 2 &&
                   conversion.strings[1] == "value" &&
                   (conversion.strings[0] == "htlist" || conversion.strings[0] == "htstring" ||
                    conversion.strings[0] == "htobject"))))
                invalid("Unsupported saved value conversion");
            end = next_op(records, end + 1);
        }
        if (end == records.size())
            invalid("Missing frame slot store");
        for (std::size_t i = 0; i < *restore; ++i)
            if (variable(records[i], true) == storage) {
                slot.definitions.push_back(records[i].location);
                slot.definition_records.push_back(i);
            }
        result.slots.push_back(std::move(slot));
        cursor = next_op(records, end + 1);
    }
    if (cursor == records.size() || !op(records[cursor], 0x26)) // pop array
        invalid("Missing frame capture terminator");
    bool unused = false;
    for (std::size_t i = 0; i < result.slot_count; ++i) {
        if (!captured.contains(i)) {
            unused = true;
            result.unused_slots.push_back(i);
        } else if (unused) {
            invalid("Non-tail unused frame slots");
        }
    }
    // The original restore switch is the authoritative callNo -> label map.
    const Record *restore_switch = nullptr;
    for (std::size_t i = *restore + 1; i < *capture; ++i)
        if (records[i].kind == Kind::EmitLabels && op(records[i], 0x45)) {
            if (restore_switch)
                invalid("Multiple restore switches");
            restore_switch = &records[i];
        }
    if (!restore_switch || restore_switch->integers.size() != result.calls.size())
        invalid("Incomplete restore call-number table");
    for (std::size_t i = 0; i < restore_switch->integers.size(); ++i) {
        const auto call = result.calls.find(static_cast<std::int32_t>(i));
        if (call == result.calls.end() || call->second.label_number != restore_switch->integers[i])
            invalid("Restore switch disagrees with call labels");
    }
    const auto array_store = next_op(records, *restore + 1);
    if (array_store >= records.size())
        invalid("Missing restored array local");
    const auto array_variable = variable(records[array_store], true);
    if (!array_variable || array_variable->first != Storage::Local)
        invalid("Invalid restored array local");
    std::set<std::size_t> restored;
    std::size_t segment = array_store + 1;
    for (std::size_t i = segment; i < *capture; ++i) {
        if (&records[i] == restore_switch)
            break;
        if (!op(records[i], 0x9a)) // ldelem.ref
            continue;
        const auto index_record = previous_op(records, i);
        const auto array_load = previous_op(records, index_record);
        const auto index = integer(records[index_record]);
        if (!index || *index < 0 || variable(records[array_load], false) != array_variable ||
            !restored.insert(static_cast<std::size_t>(*index)).second)
            invalid("Invalid restore slot read");
        const YEngineSavedSlot *saved = nullptr;
        for (const auto &slot : result.slots)
            if (slot.index == static_cast<std::size_t>(*index))
                saved = &slot;
        if (!saved)
            invalid("Restore reads an uncaptured slot");
        std::optional<std::pair<Storage, std::int32_t>> destination;
        bool restored_class_reference = false;
        std::size_t end = next_op(records, i + 1);
        for (; end < *capture; end = next_op(records, end + 1)) {
            destination = variable(records[end], true);
            if (destination)
                break;
            const auto &conversion = records[end];
            if (conversion.kind == Kind::EmitExternalMethod && conversion.strings.size() >= 2 &&
                conversion.strings[0] == "Restore" &&
                (conversion.strings[1] == "htlist" || conversion.strings[1] == "htstring" ||
                 conversion.strings[1] == "htobject")) {
                const auto receiver_record = previous_op(records, array_load);
                if (receiver_record < segment)
                    invalid("Missing restored heap tracker receiver");
                destination = variable(records[receiver_record], false);
                break;
            }
            if (saved->type == "sdtclobj") {
                if (!generated_classes || restored_class_reference || conversion.kind != Kind::EmitType ||
                    !op(conversion, 0x74) || conversion.strings != std::vector<std::string>{"sdtclobj"})
                    invalid("Invalid generated class restore cast");
                restored_class_reference = true;
            }
            if (!(op(conversion, 0xa5) || op(conversion, 0x74) || // unbox.any/castclass
                  (conversion.kind == Kind::EmitExternalMethod && conversion.strings.size() >= 2 &&
                   conversion.strings[1] == "typecast")))
                invalid("Unsupported restore value conversion");
        }
        if (saved->type == "sdtclobj" && !restored_class_reference)
            invalid("Missing generated class restore cast");
        if (!destination || destination->first != saved->storage || destination->second != saved->number)
            invalid("Restore destination disagrees with capture");
        segment = end + 1;
        i = end;
    }
    if (restored != captured)
        invalid("Captured slots are not all restored");
    result.supported = true;
    return result;
}
} // namespace

YEngineContinuationMetadata analyze_yengine_continuations(const YEngineObjectCode &code) {
    return analyze_yengine_continuations(code, {});
}

YEngineContinuationMetadata
analyze_yengine_continuations(const YEngineObjectCode &code,
                              std::span<const std::int32_t> validated_generated_class_indices) {
    YEngineContinuationMetadata result;
    result.compiled_version = code.compiled_version;
    result.source_hash = code.source_hash;
    std::string context_error;
    std::set<std::int32_t> allowed, indices;
    std::set<std::string> generated_names;
    try {
        for (const auto index : validated_generated_class_indices)
            if (index < 0 || !allowed.insert(index).second)
                invalid("Invalid or duplicate generated class allowlist index");
        if (!allowed.empty() &&
            (code.source_hash.size() != 32 || !std::ranges::all_of(code.source_hash, [](unsigned char c) {
                 return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
             })))
            invalid("Generated class context requires a valid source hash");
        std::set<std::int32_t> matched;
        for (const auto &[name, type] : code.types) {
            if (type.index < 0 || !indices.insert(type.index).second)
                invalid("Duplicate script-defined type index");
            if (type.kind == 1) {
                if (allowed.contains(type.index))
                    invalid("Allowlisted generated class index identifies a delegate");
                continue;
            }
            if (!allowed.contains(type.index))
                invalid("Unsupported script-defined class/interface/array");
            std::array<std::int32_t, 11> expected{};
            expected[3] = type.array_rank;
            expected[5] = 1;
            if (type.kind != 0 || !supported_lsl_primitive_array_descriptor(name, type.element_type, type.array_rank) ||
                type.instance_sizes != expected || !type.base_type.empty() || type.virtual_methods != 0 ||
                !type.bindings.empty() || !type.interfaces.empty())
                invalid("Allowlisted type is not a supported generated primitive array class");
            matched.insert(type.index);
            generated_names.insert(name);
        }
        if (matched != allowed)
            invalid("Generated class allowlist index is absent from type table");
    } catch (const std::invalid_argument &error) {
        context_error = error.what();
    }
    for (const auto &[name, method] : code.methods) {
        try {
            if (code.compiled_version != -11)
                invalid("Unsupported compiled version");
            if (!context_error.empty())
                throw std::invalid_argument(context_error);
            for (const auto &generated : generated_names)
                if (name.starts_with(generated + "."))
                    invalid("Generated class helper frames require a separate continuation mapping");
            result.methods.emplace(name, analyze(name, method, !allowed.empty()));
        } catch (const std::invalid_argument &error) {
            result.methods.emplace(name, YEngineMethodContinuation{false, error.what(), 0, {}, {}, {}});
        }
    }
    return result;
}
} // namespace opensim::script
